/**
 * 多线程的网络服务端
 * 给定地址上监听新的连接，创建和管理连接，并且能够处理消息
 * 
 * 用户可设置处理连接事件和消息的回调函数
 * 负载均衡，用多线程事件循环处理不同的客户端IO事件
*/

#pragma once
#include "buffer.h"

class Listener;
class EventLoopThreadPool;

class TCPConn;
typedef std::shared_ptr<TCPConn> TCPConnPtr;

typedef std::function<void(const TCPConnPtr&)> ConnectionCallback;
typedef std::function<void(const TCPConnPtr&, Buffer*)> MessageCallback;


class TCPServer
{
public:
    typedef std::function<void()> DoneCallback;

private:
    typedef std::map<uint64_t/*the id of the connection*/, TCPConnPtr> ConnectionMap;

    EventLoop* loop_;  // the listening loop
    const std::string listen_addr_; // ip:port
    std::unique_ptr<Listener> listener_;
    std::shared_ptr<EventLoopThreadPool> tpool_;
    ConnectionCallback conn_fn_;
    MessageCallback msg_fn_;

    ConnectionMap connections_;

public:
    TCPServer(EventLoop* loop,
              const std::string& listen_addr/*ip:port*/,
              const std::string& name,
              uint32_t thread_num);

    ~TCPServer();

    bool Init();

    bool Start();

    void Stop(DoneCallback cb = DoneCallback());

    void SetConnectionCallback(const ConnectionCallback& cb) {
        conn_fn_ = cb;
    }

    void SetMessageCallback(MessageCallback cb) {
        msg_fn_ = cb;
    }

private:
    void HandleNewConn(evpp_socket_t sockfd, const std::string& remote_addr/*ip:port*/, const struct sockaddr_in* raddr);

}