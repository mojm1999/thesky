class EventLoop;
class EventWatcher {
public:
    typedef std::function<void()> Handler;

protected:
    struct event* event_;
    struct event_base* evbase_;
    bool attached_;
    Handler handler_;
    Handler cancel_callback_;

    EventWatcher(struct event_base* evbase, const Handler& handler)
        : evbase_(evbase), attached_(false), handler_(handler) {
        // 申请内存，创建事件原型
        event_ = new event;
        memset(event_, 0, sizeof(struct event));
    }

    void FreeEvent() {
        if (event_) {
            if (attached_) {
                event_del(event_);
                attached_ = false;
            }

            delete (event_);
            event_ = nullptr;
        }
    }
    
    void Close() {
        DoClose();
    }

    virtual bool DoInit() = 0;
    virtual void DoClose() {}

    bool Watch() {
        if (attached_) {
            event_del(event_);
            attached_ = false;
        }

        // 加入监听
        if (event_add(event_, nullptr) != 0)
            return false;
        
        attached_ = true;
        return true;
    }

public:
    virtual ~EventWatcher() {
        FreeEvent();
        Close();
    }

    bool Init() {
        if (!DoInit()) {
            goto failed;
        }

        // 绑定事件循环
        ::event_base_set(evbase_, event_);
        return true;

    failed:
        Close();
        return false;
    }

    void Cancel() {
        FreeEvent();
        if (cancel_callback_) {
            cancel_callback_();
        }
    }

    void SetCancelCallback(const Handler& cb) {
        cancel_callback_ = cb;
    }

    void ClearHandler() { 
        handler_ = Handler(); 
    }
};
//////////////////////////////////////////////////////////////////////////


class PipeEventWatcher : public EventWatcher {
private:
    virtual bool DoInit() {
        // 创建跨线程通信管道
        if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pipe_) < 0)
            goto failed;

        // 设置非阻塞
        if (evutil_make_socket_nonblocking(pipe_[0]) < 0 ||
            evutil_make_socket_nonblocking(pipe_[1]) < 0) 
            goto failed;

        // 设置可读事件 | 可持续
        ::event_set(event_, pipe_[1], EV_READ | EV_PERSIST,
                &PipeEventWatcher::HandlerFn, this);
        return true;

    failed:
        Close();
        return false;
    }

    virtual void DoClose() {
        if (pipe_[0] > 0) {
            EVUTIL_CLOSESOCKET(pipe_[0]);
            EVUTIL_CLOSESOCKET(pipe_[1]);
            memset(pipe_, 0, sizeof(pipe_[0]) * 2);        
        }
    }

    static void HandlerFn(evpp_socket_t fd, short which, void* v) {
        PipeEventWatcher* e = (PipeEventWatcher*)v;
        char buf[128];
        int n = 0;
        // 如果写通道有数据，就回调设定函数
        if ((n = ::recv(e->pipe_[1], buf, sizeof(buf), 0)) > 0)
            e->handler_();
    }

    evpp_socket_t pipe_[2]; // 写pipe_[0] , 读pipe_[1]

public:
    PipeEventWatcher(EventLoop* loop, const Handler& handler)
        : EventWatcher(loop->event_base(), handler) {
        memset(pipe_, 0, sizeof(pipe_[0]) * 2);
    }

    ~PipeEventWatcher() {
        Close();
    }

    bool AsyncWait() {
        return Watch();
    }

    void Notify() {
        char buf[1] = {};
        // 往写通道写入数据，从而触发监听事件
        ::send(pipe_[0], buf, sizeof(buf), 0);
    }

};
//////////////////////////////////////////////////////////////////////////


#define signal_number_t evutil_socket_t

class SignalEventWatcher : public EventWatcher {
private:
    virtual bool DoInit() {
        signal_set(event_, signo_, SignalEventWatcher::HandlerFn, this);
        return true;
    }

    static void HandlerFn(signal_number_t sn, short which, void* v) {
        SignalEventWatcher* h = (SignalEventWatcher*)v;
        h->handler_();
    }

    int signo_;

public:
    SignalEventWatcher(signal_number_t signo, EventLoop* loop, const Handler& handler)
        : EventWatcher(loop->event_base(), handler)
        , signo_(signo) {}

    bool AsyncWait() {
        return Watch();
    }
};
//////////////////////////////////////////////////////////////////////////

/*
在一个消费者线程中运行一个EventLoop对象loop_，
多个生产者线程不停的调用loop_->QueueInLoop(...)方法
将仿函数执行体放入到消费者的队列中让其消费（执行）
*/
class EventLoop : public ServerStatus {
public:
    typedef std::function<void()> Functor;

private:
    struct event_base* evbase_;
    bool create_evbase_myself_;
    std::thread::id tid_;

    std::shared_ptr<PipeEventWatcher> watcher_;

    std::atomic<bool> notified_;

    std::mutex mutex_;
    std::vector<Functor>* pending_functors_;

    std::atomic<int> pending_functor_count_;

private:
    void Init() {
        status_.store(kInitializing);
        // 申请内存，创建函数组
        this->pending_functors_ = new std::vector<Functor>();
        // 记录初始化线程id
        tid_ = std::this_thread::get_id();
        InitNotifyPipeWatcher();
        status_.store(kInitialized);
    }

    void InitNotifyPipeWatcher() {
        // 创建事件
        watcher_.reset(new PipeEventWatcher(this, std::bind(&EventLoop::DoPendingFunctors, this)));
        // 调用事件初始化
        watcher_->Init();
    }

    void StopInLoop() {
        auto f = [this]() {
            for (;;) {
                DoPendingFunctors();
                if (IsPendingQueueEmpty()) {
                    break;
                }
            }
        };

        f();
        event_base_loopexit(evbase_, nullptr);
        f();
    }

    void DoPendingFunctors() {
        std::vector<Functor> functors;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            notified_.store(false);
            pending_functors_->swap(functors);
        }
        for (size_t i = 0; i < functors.size(); ++i) {
            functors[i]();
            --pending_functor_count_;
        }
    }

    bool IsPendingQueueEmpty() {
        return pending_functors_->empty();
    }

public:
    EventLoop()
        : create_evbase_myself_(true), notified_(false), pending_functor_count_(0) {
        // 创建Dispatch事件循环
        evbase_ = event_base_new();
        Init();
    }

    explicit EventLoop(struct event_base* base)
        : evbase_(base), create_evbase_myself_(false), notified_(false), pending_functor_count_(0) {
        Init();
        // 外部事件循环已启动
        bool rc = watcher_->AsyncWait();
        status_.store(kRunning);
    }

    ~EventLoop() {
        watcher_.reset();
        if (evbase_ != nullptr && create_evbase_myself_) {
            event_base_free(evbase_);
            evbase_ = nullptr;
        }
        delete pending_functors_;
        pending_functors_ = nullptr;
    }

    void Run() {
        status_.store(kStarting);
        // 记录实际运行的线程id
        tid_ = std::this_thread::get_id();

        watcher_->AsyncWait();
        status_.store(kRunning);

        // 启动事件循环
        rc = event_base_dispatch(evbase_);
        watcher_.reset();
        status_.store(kStopped);
    }

    void Stop() {
        status_.store(kStopping);
        QueueInLoop(std::bind(&EventLoop::StopInLoop, this));
    }

    void AfterFork() {
        event_reinit(evbase_);
        InitNotifyPipeWatcher();
    }

    void RunInLoop(const Functor& handler) {
        if (IsRunning() && IsInLoopThread())
            functor();
        else
            QueueInLoop(functor);
    }

    void QueueInLoop(const Functor& handler) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // 待执行函数插入等待队列
            pending_functors_->emplace_back(cb);
        }
        ++pending_functor_count_;

        if (notified_.load() || !watcher_)
            return;

        notified_.store(true);
        watcher_->Notify();
    }

public:
    struct event_base* event_base() {
        return evbase_;
    }
    bool IsInLoopThread() const {
        return tid_ == std::this_thread::get_id();
    }
    int pending_functor_count() const {
        return pending_functor_count_.load();
    }
    const std::thread::id& tid() const {
        return tid_;
    }
};
//////////////////////////////////////////////////////////////////////////


class EventLoopThread : public ServerStatus {
private:
    std::shared_ptr<EventLoop> event_loop_;

    std::mutex mutex_;
    std::shared_ptr<std::thread> thread_;

    std::string name_;

public:
    enum { kOK = 0 };

    typedef std::function<int()> Functor;

    EventLoopThread()
        : event_loop_(new EventLoop) {}

    ~EventLoopThread() {
        Join();
    }

    bool Start(bool wait_thread_started = true, 
            Functor pre = Functor(),
            Functor post = Functor()) {
        status_ = kStarting;
        // 创建新线程
        thread_.reset(new std::thread(std::bind(&EventLoopThread::Run, this, pre, post)));
        if (wait_thread_started) {
            while (status_ < kRunning) {
                usleep(1);
            }
        }
        return true;
    }

    void Stop(bool wait_thread_exit = false) {
        status_ = kStopping;
        event_loop_->Stop();

        if (wait_thread_exit) {
            while (!IsStopped()) {
                usleep(1);
            }
            Join();
        }
    }

    void Join() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (thread_ && thread_->joinable()) {
            thread_->join();
            thread_.reset();
        }
    }

    void AfterFork() {
        loop()->AfterFork();
    }

public:
    void set_name(const std::string& n);
    const std::string& name() const;
    EventLoop* loop() const;
    struct event_base* event_base();
    std::thread::id tid() const;
    bool IsRunning() const;

private:
    void Run(const Functor& pre, const Functor& post) {
        auto fn = [this, pre]() {
            status_ = kRunning;
            if (pre) {
                auto rc = pre();
                if (rc != kOK) {
                    event_loop_->Stop();
                }
            }
        }

        event_loop_->QueueInLoop(std::move(fn));
        event_loop_->Run();

        if (post) {
            post();
        }
        status_ = kStopped;
    }
};
//////////////////////////////////////////////////////////////////////////


class EventLoopThreadPool : public ServerStatus {
private:
    EventLoop* base_loop_;

    uint32_t thread_num_ = 0;
    std::atomic<int64_t> next_ = { 0 };

    DoneCallback stopped_cb_;

    typedef std::shared_ptr<EventLoopThread> EventLoopThreadPtr;
    std::vector<EventLoopThreadPtr> threads_;

public:
    typedef std::function<void()> DoneCallback;

    EventLoopThreadPool(EventLoop* base_loop, uint32_t thread_num)
        : base_loop_(base_loop), thread_num_(thread_number) {}

    ~EventLoopThreadPool() {
        Join();
        threads_.clear();
    }

    bool Start(bool wait_thread_started = false) {
        status_.store(kStarting);

        std::shared_ptr<std::atomic<uint32_t>> started_count(new std::atomic<uint32_t>(0));
        std::shared_ptr<std::atomic<uint32_t>> exited_count(new std::atomic<uint32_t>(0));

        for (uint32_t i = 0; i < thread_num_; ++i) {
            auto prefn = [this, started_count]() {
                this->OnThreadStarted(started_count->fetch_add(1) + 1);
                return EventLoopThread::kOK;
            };

            auto postfn = [this, exited_count]() {
                this->OnThreadExited(exited_count->fetch_add(1) + 1);
                return EventLoopThread::kOK;
            };

            EventLoopThreadPtr t(new EventLoopThread());
            if (!t->Start(wait_thread_started, prefn, postfn))
                return false;

            threads_.push_back(t);
        }

        
        if (wait_thread_started) {
            while (!IsRunning()) {
                usleep(1);
            }
        }

        return true;
    }

    void Stop(bool wait_thread_exited = false);
    void Stop(DoneCallback fn);
    void Join();
    void AfterFork();

public:
    EventLoop* GetNextLoop() {
        EventLoop* loop = base_loop_;

        if (IsRunning() && !threads_.empty()) {
            // No need to lock here
            int64_t next = next_.fetch_add(1);
            next = next % threads_.size();
            loop = (threads_[next])->loop();
        }

        return loop;
    }

    EventLoop* GetNextLoopWithHash(uint64_t hash) {
        EventLoop* loop = base_loop_;

        if (IsRunning() && !threads_.empty()) {
            uint64_t next = hash % threads_.size();
            loop = (threads_[next])->loop();
        }

        return loop;
    }

    uint32_t thread_num() const;

private:
    void Stop(bool wait_thread_exit, DoneCallback fn);
    void OnThreadStarted(uint32_t count);
    void OnThreadExited(uint32_t count);
};  