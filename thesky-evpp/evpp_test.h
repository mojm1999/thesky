#include <event2/event.h>
#include "base_inc.h"

#ifdef __linux__ 
    // linux 或 Unix特定的代码
    #include <sys/select.h>

#elif defined(_WIN32) 
    // windows 特定的代码
    #include <winsock2.h>
    
#endif

constexpr uint16_t PORT = 8080;

void accept_conn_cb(evutil_socket_t fd, short events, void *arg)
{
    thread_safe_cout() << "fd：" << fd << " events：" << events << std::endl;
}

int tcp_test()
{
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    evutil_socket_t server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket creation failed");
        return -1;
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen failed");
        return 1;
    }

    struct event_base *base = event_base_new();
    struct event *listener_event = event_new(base, server_fd, EV_READ|EV_PERSIST, accept_conn_cb, base);

    event_add(listener_event, NULL);
    event_base_dispatch(base);

    return 0;
}