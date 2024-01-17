#pragma once
#include "socket_inc.h"

class selectops : public multi_core
{
private:
    // 监听读套接字
    fd_set m_readset_in;
    // 就绪读集合
    fd_set m_readset_out;
    // 监听写套接字
    fd_set m_writeset_in;
    // 写就绪集合
    fd_set m_writeset_out;
    // 监听里最大的fd值
    int32_t m_fdmax {0};

    event_base* m_base;

public:
    void init(event_base* base) override ;
    int32_t add(socket_t fd, short old, short flags) override ;
    int32_t del(socket_t fd, short old, short flags) override ;
    int32_t dispatch(struct timeval* tv) override ;
};

void selectops::init(event_base* arg_base)
{
    m_base = arg_base;
}

int32_t selectops::add(socket_t fd, short old, short flags)
{
    m_fdmax = std::max(m_fdmax, fd);
    if (flags & EV_READ)
        FD_SET(fd, &m_readset_in);
	if (flags & EV_WRITE)
		FD_SET(fd, &m_writeset_out);
    return 0;
}

int32_t selectops::del(socket_t fd, short old, short flags)
{
	if (flags & EV_READ)
		FD_CLR(fd, &m_readset_in);

	if (flags & EV_WRITE)
		FD_CLR(fd, &m_writeset_out);
    return 0;
}

int32_t selectops::dispatch(struct timeval* tv)
{
    if (m_base == nullptr)
        return -1;

    FD_ZERO(&m_readset_out);
    std::memcpy(&m_readset_out, &m_readset_in, sizeof(m_readset_out));
    FD_ZERO(&m_writeset_out);

    int32_t res = 0, nfds = m_fdmax+1;
    res = select(nfds, &m_readset_out, &m_writeset_out, nullptr, tv);
    if (res == -1)
        return 0;

    // 遍历所有监控的套接字
    for (int32_t i = 0; i < nfds; ++i) {
        res = 0;
        if (FD_ISSET(i, &m_readset_out))
            res |= EV_READ;
        if (FD_ISSET(i, &m_readset_out))
            res |= EV_READ;
        if (res == 0)
            continue;
        m_base->evmap_io_active(i, res);
    }
    return 0;
}