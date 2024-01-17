#pragma once
#include "socket_inc.h"

class event
{
private:
    // 关联事件管理器
    PTR_EVBASE m_base;

public:
    // 绑定的套接字
    socket_t m_fd;
    // 事件标志组
    short m_flags;
    // 当前状态
    short m_state;
    // 队列优先级，数字越小代表越高优先级
    uint32_t m_pri;
    // 绑定的回调函数
    FUNC_CB m_cb;
    // 函数参数
    void* m_arg;
    // 激活标志
    std::queue<short> m_activate;

    event() {}
    event(PTR_EVBASE base, socket_t fd,
        short flags, FUNC_CB cb, void* arg)
    { event_new(base, fd, flags, cb, arg); }

    // 事件初始化
    void event_new(PTR_EVBASE base, socket_t fd, 
        short flags, FUNC_CB cb, void* arg);

    // 加入循环
    int32_t event_add();

    // 退出循环
    int32_t event_del();
};

void event::event_new(PTR_EVBASE base, socket_t fd, 
    short flags, FUNC_CB cb, void* arg)
{
    m_base = base;
    m_fd = fd;
    m_flags = flags;
    m_cb = cb;
    m_arg = arg;
    m_state = EVLIST_INIT;
    
    // 默认优先级为管理器的一半
    if (base != nullptr)
        m_pri = base->m_nactivequeues / 2;
}

int32_t event::event_add()
{
    if (m_base != nullptr)
        return -1;

    int32_t res = 0;
    if ((m_flags & (EV_READ|EV_WRITE)) &&
        !(m_state & (EVLIST_INSERTED|EVLIST_ACTIVE|EVLIST_ACTIVE_LAST))) {
        if (m_flags & (EV_READ|EV_WRITE))
            res = m_base->evmap_io_add(this);
        else if (m_flags & EV_SIGNAL)
            //res = m_base->evmap_signal_add()
        if (res != -1) {
            //++m_base->EVENT_COUNT;
            m_state |= EVLIST_INSERTED;
        }
    }
    return res;
}

int32_t event::event_del()
{
    if (m_base != nullptr)
        return -1;
    return 0;
    // if (m_state & EVLIST_ACTIVE)
    //     // remove m_base->activequeues
    // else if (m_state & EVLIST_ACTIVE_LATER)
    //     // remove m_base->list

    // int32_t res = 0;
    // if (m_state & EVLIST_INSERTED) {
    //     if (m_flags & (EV_READ|EV_WRITE))
    //         //res = m_base->evmap_io_del()
    //     else if (m_flags & EV_SIGNAL)
    //         //res = m_base->evmap_signal_del()
    // }
    // return res;
}


// template<class F, class... Args>
// void event::register_callback(F&& f, Args&&... args)
// {
//     auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
//     m_callback = std::make_shared<TASK>([func]() { func(); });
// }