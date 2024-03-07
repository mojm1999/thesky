/**
 * FdChannel类，处理单个网络连接的事件
 * 封装单个文件描述符
 * 注册读事件和写事件处理回调函数
 * 与一个事件循环关联起来，异步处理
 * 
 * Listener类，用于接受新的网络连接
 * 指定地址创建一个监听socket
 * 使用监听套接字创建FdChannel来处理每个接受的连接的事件处理
 * 设置新连接处理函数
*/

#pragma once
#include "buffer.h"

class EventLoop;

class FdChannel
{
public:
    typedef std::function<void()> EventCallback;
    typedef std::function<void()> ReadEventCallback;

private:
    ReadEventCallback read_fn_;
    EventCallback write_fn_;
    EventLoop* loop_;
    bool attached_; // 是否已经附加到loop_
    struct event* event_;
    int events_; // EventType标志的按位或
    evutil_socket_t fd_;

public:
    FdChannel(EventLoop* l, evpp_socket_t fd,
            bool r, bool w)
        : loop_(l), attached_(false), event_(nullptr), fd_(f){
        events_ = (r ? EV_READ : 0) | (w ? EV_WRITE : 0);
        event_ = new event;
        memset(event_, 0, sizeof(struct event));
    }

    ~FdChannel() {
        if (event_) {
            assert(!attached_);
            if (attached_) {
                event_del(event_);
            }

            delete (event_);
            event_ = nullptr;
        }
        read_fn_ = ReadEventCallback();
        write_fn_ = EventCallback();
    }

    void AttachToLoop() {
        ::event_set(event_, fd_, events_ | EV_PERSIST,
                &FdChannel::HandleEvent, this);
        ::event_base_set(loop_->event_base(), event_);
        
        if (event_add(event_, nullptr) == 0)
            attached_ = true;
    }

    void SetReadCallback(const ReadEventCallback& cb) {
        read_fn_ = cb;
    }

    void SetWriteCallback(const EventCallback& cb) {
        write_fn_ = cb;
    }

private:
    void HandleEvent(evpp_socket_t fd, short which) {
        if ((which & EV_READ) && read_fn_)
            read_fn_();
        
        if ((which & EV_WRITE) && write_fn_)
            write_fn_();
    }

    static void HandleEvent(evpp_socket_t fd, short which, void* v);
}

class Listener
{
public:
    typedef std::function <
    void(evutil_socket_t sockfd,
        const std::string&,
        const struct sockaddr_in*) >
    NewConnectionCallback;

private:
    evutil_socket_t fd = -1;
    EventLoop* loop_;
    std::string add_;
    std::unique_ptr<FdChannel> chan_;
    NewConnectionCallback new_conn_fn_;

public:
    Listener(EventLoop* l, const std::string& addr)
        : loop_(l), addr_(addr) {
    }

    ~Listener() {
        chan_.reset();
        EVUTIL_CLOSESOCKET(fd_);
        fd_ = INVALID_SOCKET;
    }

    void Listen(int backlog = SOMAXCONN) {
        fd_ = CreateNonblockingSocket();
        if (fd_ < 0) {
            int serrno = errno;
            thread_safe_cout() << "Create a nonblocking socket failed " << strerror(serrno);
            return;
        }

        struct sockaddr_storage addr = ParseFromIPPort(addr_.data());
        int ret = ::bind(fd_, sockaddr_cast(&addr), static_cast<socklen_t>(sizeof(struct sockaddr)));
        if (ret < 0) {
            int serrno = errno;
            thread_safe_cout() << "bind error :" << strerror(serrno) << " . addr=" << addr_;
        }

        ret = ::listen(fd_, backlog);
        if (ret < 0) {
            int serrno = errno;
            thread_safe_cout() << "Listen failed " << strerror(serrno);
        }
    }

    void Accept() {
        chan_.reset(new FdChannel(loop_, fd_, true, false));
        chan_->SetReadCallback(std::bind(&Listener::HandleAccept, this));
        loop_->RunInLoop(std::bind(&FdChannel::AttachToLoop, chan_.get()));
    }

    void SetNewConnectionCallback(NewConnectionCallback cb) {
        new_conn_fn_ = cb;
    }

private:
    void HandleAccept() {
        struct sockaddr_storage ss;
        socklen_t addrlen = sizeof(ss);
        int nfd = -1;

        if ((nfd = ::accept(fd_, sockaddr_cast(&ss), &addrlen)) == -1) {
            int serrno = errno;
            if (serrno != EAGAIN && serrno != EINTR) {
                thread_safe_cout() << __FUNCTION__ << " bad accept " << strerror(serrno);
            }
            return;
        }

        if (evutil_make_socket_nonblocking(nfd) < 0) {
            EVUTIL_CLOSESOCKET(nfd);
            return;
        }

        SetKeepAlive(nfd, true);

        std::string raddr = sToIPPort(&ss);
        if (raddr.empty()) {
            EVUTIL_CLOSESOCKET(nfd);
            return;
        }

        if (new_conn_fn_) {
            new_conn_fn_(nfd, raddr, sockaddr_in_cast(&ss));
        }
    }
}