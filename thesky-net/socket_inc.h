#pragma once
#include "base_inc.h"

#ifdef __linux__ 
    // linux �� Unix�ض��Ĵ���
    #include <sys/select.h>
    using socket_t = int32_t;

#elif defined(_WIN32) 
    // windows �ض��Ĵ���
    #include <winsock2.h>
    using socket_t = intptr_t;

    #define read(fd, buf, len) recv(fd, buf, len, 0)
    #define write(fd, buf, len) send(fd, buf, len, 0)
#endif

class event;
class event_base;

// ����ص���������
using FUNC_CB = std::function<void(socket_t, short, void*)>;

// �����¼�˫��������
using LIST_EV = std::list<std::shared_ptr<event>>;

// �����¼�����������ָ������
using PTR_EVBASE = std::shared_ptr<event_base>;

enum EVENT_FLAGS
{
    EV_TIMEOUT  = 0x01,
    EV_READ     = 0x01 << 1,
    EV_WRITE    = 0x01 << 2,
    EV_SIGNAL   = 0x01 << 3,
    EV_PERSIST  = 0x01 << 4,
};

enum EVENT_STATE
{
    EVLIST_INSERTED = 0,
    EVLIST_ACTIVE,
    EVLIST_ACTIVE_LAST,
    EVLIST_FINALIZING,
    EVLIST_INIT,
};

// �����·���ýӿ���
class multi_core
{
public:
    virtual ~multi_core() {}
    virtual void init(event_base* base) = 0;
    virtual int32_t add(socket_t fd, short old, short flags) = 0;
    virtual int32_t del(socket_t fd, short old, short flags) = 0;
    virtual int32_t dispatch(struct timeval* tv) = 0;
};