/**
 * 管理TCP网络连接的整个生命周期
 * 创建连接、维护连接状态、接收和发送数据以及处理连接的关闭
 * 
 * 连接的创建和设置
 * 接受多个回调函数，响应网络事件，如连接建立、消息接收、写入完成等
 * 
*/

#pragma once
#include "buffer.h"

class EventLoop;
class FdChannel;

class TCPConn : public std::enable_shared_from_this<TCPConn>
{
public:
    TCPConn(EventLoop* loop,
            const std::string& name,
            evpp_socket_t sockfd,
            const std::string& laddr,
            const std::string& raddr,
            uint64_t id);

    ~TCPConn();

private:
    EventLoop* loop_;
    int fd_;

    std::string local_addr_;
    std::string remote_addr_;
    std::unique_ptr<FdChannel> chan_;

    Buffer input_buffer_;
    Buffer output_buffer_;

public:

}