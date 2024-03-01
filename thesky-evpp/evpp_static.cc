#include "event_loop.h"

void PipeEventWatcher::HandlerFn(evpp_socket_t fd, short /*which*/, void* v)
{
    PipeEventWatcher* e = (PipeEventWatcher*)v;
    char buf[128];
    int n = 0;
    if ((n = ::recv(e->pipe_[1], buf, sizeof(buf), 0)) > 0) {
        e->handler_();
    }
}