/**
 * 通用的数据缓冲区实现
 * 用于网络编程中，主要是存储和操作数据
 * char指针
 * 封装了对字节缓冲区的各种操作，例如追加数据、消耗数据、预留空间等
*/

/**
 ** 管理两个索引
 * read_index_：读取数据的开始位置
 * write_index_：写入数据的开始位置
 * 一个缓存区完成读写操作，节省内存
 * 
 ** 预留了开头的一小段内存
 * 允许在缓冲区的前面有效地插入数据
 * 对网络协议非常有用，例如加协议头
 * 
 ** 动态扩展
 * 内存不足时，实现了一个grow函数来增加缓冲区的容量
 * new新内存，拷贝移动现有的数据
 * 
 ** 应用场景
 * 服务器接收到客户端的数据时，使用Buffer作为中间存储
 * 按需处理解析这些数据
*/

#pragma once
#include <event2/event.h>
#include "base_inc.h"

class Buffer
{
private:
    // 数据指针
    char* buffer_;
    // 容量大小（字节数）
    size_t capacity_;
    // 当前读索引
    size_t read_index_;
    // 当前写索引
    size_t write_index_;

private:
    // 返回实例的数据指针
    char* begin();

    // 返回实例的数据指针
    const char* begin() const;

    // 缓冲区扩容
    void grow(size_t len);

public:
    // 构造函数
    explicit buffer();

    // 析构函数
    ~buffer();

public:
    // data 该指针保持缓冲区未读的部分，返回一个长度为 Buffer.length() 的指针
    // 数据仅在下次缓冲区修改之前有效，（即，下次调用 Read, Write, Reset, 或 Truncate 等方法）
    // 数据至少在下次缓冲区修改之前是对缓冲区内容的别名，所以立即对切片的更改将影响未来读取的结果
    const char* data() const;

    // 未读数据长度
    size_t length() const;

    // 可写字节数
    size_t writable_bytes() const;

public:
    int64_t read_int64();

    std::string to_string() const;

    ssize_t read_from_fd(evutil_socket_t fd, int* )
};

