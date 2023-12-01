#include "base_inc.h"
#include "thread_pool.h"
#include "thesky_timer.h"

std::mutex thread_safe_cout::m_mutex;

// 1、测试线程池
// test_thread_pool();

// 2、测试时间轮定时器
// test_thesky_timer();

int main()
{
    thread_safe_cout() << "Hello World." << std::endl;
    test_thesky_timer();
    thread_safe_cout() << "Bye World." << std::endl;
    for (;;) {}
    return 0;
}