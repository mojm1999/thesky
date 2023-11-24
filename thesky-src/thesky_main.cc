#include "base_inc.h"
#include "thread_pool.h"

std::mutex thread_safe_cout::m_mutex;

int main()
{
    std::cout << "Hello World." << std::endl;
    test_thread_pool();
    return 0;
}