class EventLoop;

class FdChannel {
public:
    enum EventType {
        kNone = 0x00,
        kReadable = 0x02,
        kWritable = 0x04,
    };
    typedef std::function<void()> EventCallback;
    typedef std::function<void()> ReadEventCallback;

private:
    ReadEventCallback read_fn_;
    EventCallback write_fn_;

    EventLoop* loop_;
    bool attached_;

    struct event* event_;
    int events_;

    evpp_socket_t fd_;

public:
    FdChannel(EventLoop* l, evpp_socket_t f,
              bool watch_read_event, bool watch_write_event)
              : loop_(l), attached_(false), event_(nullptr), fd_(f) {
                events_ = (r ? kReadable : 0) | (w ? kWritable : 0);
                event_ = new event;
                memset(event_, 0, sizeof(struct event));
              }

    ~FdChannel() {}

    void Close() {
        if (event_) {
            assert(!attached_);
            if (attached_) {
                EventDel(event_);
            }

            delete (event_);
            event_ = nullptr;
        }
        read_fn_ = ReadEventCallback();
        write_fn_ = EventCallback();
    }

    void AttachToLoop() {
        if (attached_) {
            DetachFromLoop();
        }
        // 设置读写回调函数
        ::event_set(event_, fd_, events_ | EV_PERSIST,
                &FdChannel::HandleEvent, this);
        ::event_base_set(loop_->event_base(), event_);

        if (event_add(event_, nullptr) == 0)
            attached_ = true;
    }

    bool attached() const {
        return attached_;
    }

public:
    bool IsReadable() const {
        return (events_ & kReadable) != 0;
    }
    bool IsWritable() const {
        return (events_ & kWritable) != 0;
    }
    bool IsNoneEvent() const {
        return events_ == kNone;
    }

    void EnableReadEvent();
    void EnableWriteEvent();
    void DisableReadEvent();
    void DisableWriteEvent();
    void DisableAllEvent();

public:
    evpp_socket_t fd() const {
        return fd_;
    }
    std::string EventsToString() const;

public:
    void SetReadCallback(const ReadEventCallback& cb) {
        read_fn_ = cb;
    }

    void SetWriteCallback(const EventCallback& cb) {
        write_fn_ = cb;
    }

private:
    void HandleEvent(evpp_socket_t fd, short which) {
        if ((which & kReadable) && read_fn_) {
            read_fn_();
        }

        if ((which & kWritable) && write_fn_) {
            write_fn_();
        }
    }

    static void HandleEvent(evpp_socket_t fd, short which, void* v) {
        FdChannel* c = (FdChannel*)v;
        c->HandleEvent(sockfd, which);
    }

    void Update() {
        if (IsNoneEvent()) {
            DetachFromLoop();
        } else {
            AttachToLoop();
        }
    }

    void DetachFromLoop() {
        if (event_del(event_) == 0)
            attached_ = false;
    }
};
//////////////////////////////////////////////////////////////////////////

/*
"ip:port"
*/
class EVPP_EXPORT Listener {
private:
    evpp_socket_t fd_ = -1;
    EventLoop* loop_;
    std::string addr_;
    std::unique_ptr<FdChannel> chan_;
    NewConnectionCallback new_conn_fn_;

public:
    typedef std::function <
    void(evpp_socket_t sockfd,
         const std::string&,
         const struct sockaddr_in*) > NewConnectionCallback;

    Listener(EventLoop* loop, const std::string& addr)
        : loop_(l), addr_(addr) {}

    ~Listener() {
        chan_.reset();
        EVUTIL_CLOSESOCKET(fd_);
        fd_ = INVALID_SOCKET;
    }

    void Listen(int backlog = SOMAXCONN) {
        fd_ = sock::CreateNonblockingSocket();
        if (fd_ < 0)
            return;
        
        struct sockaddr_storage addr = sock::ParseFromIPPort(addr_.data());
        int ret = ::bind(fd_, sock::sockaddr_cast(&addr), static_cast<socklen_t>(sizeof(struct sockaddr)));
        if (ret < 0)
            return;

        ret = ::listen(fd_, backlog);
        if (ret < 0)
            return;
    }

    void Accept() {
        // 创建管理套接字读写的chan_对象
        chan_.reset(new FdChannel(loop_, fd_, true, false));
        // 设置连接处理函数
        chan_->SetReadCallback(std::bind(&Listener::HandleAccept, this));
        loop_->RunInLoop(std::bind(&FdChannel::AttachToLoop, chan_.get()));
    }

    void Stop();

    void SetNewConnectionCallback(NewConnectionCallback cb) {
        new_conn_fn_ = cb;
    }

private:
    void HandleAccept() {
        struct sockaddr_storage ss;
        socklen_t addrlen = sizeof(ss);
        int nfd = -1;
        if ((nfd = ::accept(fd_, sock::sockaddr_cast(&ss), &addrlen)) == -1)
            return;

        if (evutil_make_socket_nonblocking(nfd) < 0) {
            EVUTIL_CLOSESOCKET(nfd);
            return;
        }

        sock::SetKeepAlive(nfd, true);

        std::string raddr = sock::ToIPPort(&ss);
        if (raddr.empty()) {
            EVUTIL_CLOSESOCKET(nfd);
            return;
        }

        if (new_conn_fn_) {
            new_conn_fn_(nfd, raddr, sock::sockaddr_in_cast(&ss));
        }
    }
};
//////////////////////////////////////////////////////////////////////////


class TCPServer : public ThreadDispatchPolicy, public ServerStatus {
public:
    typedef std::function<void()> DoneCallback;

private:
    EventLoop* loop_;  // the listening loop
    const std::string listen_addr_; // ip:port
    const std::string name_;
    std::unique_ptr<Listener> listener_;
    std::shared_ptr<EventLoopThreadPool> tpool_;
    ConnectionCallback conn_fn_;
    MessageCallback msg_fn_;

    DoneCallback stopped_cb_;

    // These two member variables will always be modified in the listening loop thread
    uint64_t next_conn_id_ = 0;
    typedef std::map<uint64_t/*the id of the connection*/, TCPConnPtr> ConnectionMap;
    ConnectionMap connections_;

public:
    TCPServer(EventLoop* loop, const std::string& listen_addr/*ip:port*/,
              const std::string& name,
              uint32_t thread_num)
            
    ~TCPServer();

    // @brief Do the initialization works here.
    //  It will create a nonblocking TCP socket, and bind with the given address
    //  then listen on it. If there is anything wrong it will return false.
    // @return bool - True if everything goes well
    bool Init();

    // @brief Start the TCP server and we can accept new connections now.
    // @return bool - True if everything goes well
    bool Start();

    // @brief Stop the TCP server
    // @param cb - the callback cb will be invoked when
    //  the TCP server is totally stopped
    void Stop(DoneCallback cb = DoneCallback());

    // @brief Reinitialize some data fields after a fork
    void AfterFork();

public:
    // Set a connection event relative callback which will be invoked when the TCPServer
    // receives a new connection or an exist connection breaks down.
    // When these two events happened, the value of the parameter in the callback is:
    //      1. Received a new connection : TCPConn::IsConnected() == true
    //      2. An exist connection broken down : TCPConn::IsDisconnecting() == true
    void SetConnectionCallback(const ConnectionCallback& cb) {
        conn_fn_ = cb;
    }

    // Set the message callback to handle the messages from remote client
    void SetMessageCallback(MessageCallback cb) {
        msg_fn_ = cb;
    }

public:
    const std::string& listen_addr() const {
        return listen_addr_;
    }
private:
    void StopThreadPool();
    void StopInLoop(DoneCallback on_stopped_cb);
    void RemoveConnection(const TCPConnPtr& conn);
    void HandleNewConn(evpp_socket_t sockfd, const std::string& remote_addr/*ip:port*/, const struct sockaddr_in* raddr);
    EventLoop* GetNextLoop(const struct sockaddr_in* raddr);
};