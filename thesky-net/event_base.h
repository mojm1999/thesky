#pragma once
#include "socket_inc.h"
#include "core_select.h"

class event_base
{
public:
    // 待激活的IO事件
    std::unordered_map<socket_t, LIST_EV> m_evmapio;
    // 已激活待执行的事件
    std::vector<LIST_EV> m_activequeues;
    // 激活队列的长度
    uint32_t m_nactivequeues;

    // 多路复用底层
    std::shared_ptr<multi_core> m_multicore;

public:
    event_base();
    int32_t event_base_dispatch();

    int32_t evmap_io_add(event* ev);
    void evmap_io_active(socket_t fd, short flags);
    void process_active();
};

event_base::event_base(/* args */)
{
    m_multicore = std::make_shared<selectops>();
    m_multicore->init(this);
}

int32_t event_base::event_base_dispatch()
{
    struct timeval tv;
    bool start = true;
    int32_t res = 0;

    while (start) {
		res = m_multicore->dispatch(&tv);
        process_active();
    }
    
}

int32_t event_base::evmap_io_add(event* ev)
{
    if (ev == nullptr)
        return -1;
    std::shared_ptr<event> ptr(new event(*ev));
    LIST_EV& list = m_evmapio[ptr->m_fd];
    list.emplace_back(ptr);
    ptr->m_state = EVLIST_INSERTED;

    m_multicore->add(ptr->m_fd, 0, ptr->m_flags);
    return 0;
}

void event_base::evmap_io_active(socket_t fd, short flags)
{
    LIST_EV& list = m_evmapio[fd];

    for (auto it = list.begin(); it != list.end();) {
        auto element = *it;
        if (element->m_flags & flags) {
            element->m_activate.emplace(flags);
            m_activequeues[element->m_pri].emplace_back(element);
            element->m_state = EVLIST_ACTIVE;
        }
        if (!(element->m_flags & EV_PERSIST)) {
            element->m_flags &= ~flags;
        }
        if (element->m_flags == 0) {
            it = list.erase(it);
            element->m_state = EVLIST_ACTIVE_LAST;
            continue;
        }
        ++it;
    }
}

void event_base::process_active()
{
    for (auto& list : m_activequeues) {
        for (auto it = list.begin(); it != list.end();) {
            auto element = *it;
            if (element->m_cb) {
                socket_t fd = element->m_fd;
                short flag = element->m_activate.front();
                element->m_cb(fd, flag, element->m_arg);
            }
            element->m_activate.pop();
            it = list.erase(it);
        }
    }
}