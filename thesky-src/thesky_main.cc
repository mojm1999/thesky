#include "base_inc.h"
#include "thread_pool.h"
#include "thesky_timer.h"
#include "evpp_test.h"

std::mutex thread_safe_cout::m_mutex;

// 1、测试线程池
// test_thread_pool();

// 2、测试时间轮定时器
// test_thesky_timer();

// 3、测试网络库

int32_t main()
{
#ifdef _WIN32
    // 初始化
    WSADATA wsaData;
    // Winsock版本定为2.2
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        thread_safe_cout() << "WSAStartup failed: " << wsaResult << std::endl;
        return 1;
    }
#endif

    thread_safe_cout() << "Hello World." << std::endl;

    tcp_test();

    // 陷入循环不终止程序
    // for (;;) {}
    return 0;
}