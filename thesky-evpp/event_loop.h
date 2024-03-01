#include "buffer.h"

class EventWatcher
{
public:
    typedef std::function<void()> Handler;
    virtual ~EventWatcher() {
        FreeEvent();
        DoClose();
    }

protected:
    struct event* event_;
    struct event_base* evbase_;
    bool attached_;
    Handler handler_;

protected:
    EventWatcher(struct event_base* evbase, const Handler& handler)
        : evbase_(evbase), attached_(false), handler_(handler) {
        event_ = new event;
        memset(event_, 0, sizeof(struct event));
    }

    void FreeEvent() {
        if (attached_) {
            event_del(event_);
            attached_ = false;
        }

        delete event_;
        event_ = nullptr;
    }

    virtual bool DoInit() = 0;
    virtual void DoClose() {}

public:
    bool BaseInit() {
        if (!DoInit()) {
            goto failed;
        }

        ::event_base_set(evbase_, event_);
        return true;

    failed:
        DoClose();
        return false;
    }
}


class PipeEventWatcher : public EventWatcher
{
private:
    evutil_socket_t pipe_[2]; // Write to pipe_[0] , Read from pipe_[1]
    static void HandlerFn(evpp_socket_t fd, short which, void* v);

    virtual bool DoInit() {
        if (evutil_socketpair(AF_UNIX, SOCK_STREAM, 0, pipe_) < 0) {
            int err = errno;
            thread_safe_cout() << "create socketpair ERROR errno=" << err << " " << strerror(err) << std::endl;
            goto failed;
        }

        if (evutil_make_socket_nonblocking(pipe_[0]) < 0 ||
            evutil_make_socket_nonblocking(pipe_[1]) < 0) {
            goto failed;
        }

        ::event_set(event_, pipe_[1], EV_READ | EV_PERSIST,
            &PipeEventWatcher::HandlerFn, this);

    failed:
        DoClose();
        return false;
    }

    virtual void DoClose() {
        if (pipe_[0] > 0) {
            EVUTIL_CLOSESOCKET(pipe_[0]);
            EVUTIL_CLOSESOCKET(pipe_[1]);
            memset(pipe_, 0, sizeof(pipe_[0]) * 2);
        }
    }

public:
    PipeEventWatcher(EventLoop* loop, const Handler& handler)
        : EventWatcher(loop->event_base(), handler) {
        memset(pipe_, 0, sizeof(pipe_[0]) * 2);
    }

    ~PipeEventWatcher() {
        DoClose();
    }

    bool AsyncWait() {
        if (attached_) {
            if (event_del(event_) != 0) {
                thread_safe_cout() << "event_del failed. fd=" << this->event_->ev_fd << " event_=" << event_;
            }
            attached_ = false;
        }
        struct timeval* timeout = nullptr;
        if (event_add(event_, timeout) != 0) {
            thread_safe_cout() << "event_add failed. fd=" << this->event_->ev_fd << " event_=" << event_;
            return false;
        }
        attached_ = true;
        return true;
    }

    void Notify() {
        char buf[1] = {};
        if (::send(pipe_[0], buf, sizeof(buf), 0) < 0) {
            return;
        }
    }
}

/**
 * 用于处理客户端请求和其他I/O事件，而不阻塞主线程
*/
class EventLoop
{
public:
    typedef std::function<void()> Functor;

private:
    struct event_base* evbase_;
    bool create_evbase_myself_;

    std::shared_ptr<PipeEventWatcher> watcher_;
    std::vector<Functor>* pending_functors_;

    std::atomic<int> pending_functor_count_;
    std::mutex mutex_;
    std::atomic<bool> notified_;

public:
    EventLoop()
        : create_evbase_myself_(true), notified_(false), pending_functor_count_(0) {
        evbase_ = event_base_new();
        Init();
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
        int rc = watcher_->AsyncWait();
        if (!rc) {
            thread_safe_cout() << "PipeEventWatcher init failed.";
        }

        rc = event_base_dispatch(evbase_);
        if (rc == 1) {
            thread_safe_cout() << "event_base_dispatch error: no event registered";
        } else if (rc == -1) {
            int serrno = errno;
            thread_safe_cout() << "event_base_dispatch error " << serrno << " " << strerror(serrno);
        }

        watcher_.reset();
    }

    void QueueInLoop(const Functor& cb) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_functors_->emplace_back(cb);
        }
        ++pending_functor_count_;
        if (!notified_.load()) {
            notified_.store(true);
            if (watcher_) {
                watcher_->Notify();
            }
        }
    }

private:
    void Init() {
        pending_functors_ = new std::vector<Functor>();
        InitNotifyPipeWatcher();
    }

    void InitNotifyPipeWatcher() {
        watcher_.reset(new PipeEventWatcher(this, std::bind(&EventLoop::DoPendingFunctors, this)));
        int rc = watcher_->BaseInit();
        if (!rc) {
            thread_safe_cout() << "PipeEventWatcher init failed.";
        }
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
}