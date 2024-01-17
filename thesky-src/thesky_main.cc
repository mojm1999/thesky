#include "base_inc.h"
#include "thread_pool.h"
#include "thesky_timer.h"

std::mutex thread_safe_cout::m_mutex;

// 1、测试线程池
// test_thread_pool();

// 2、测试时间轮定时器
// test_thesky_timer();

// 3、测试网络库

int32_t main()
{
    thread_safe_cout() << "Hello World." << std::endl;

    // 陷入循环不终止程序
    for (;;) {}
    return 0;
}