#pragma once
#include <iostream>
#include <thread>
#include <future>
#include <list>
#include <queue>
#include <functional>
#include <random>
#include <chrono>
#include <algorithm>
#include <map>

// 定义任务类型
using TASK = std::function<void()>;

/**
 * 获取一个随机数值
 * 
 * @param:
 *  [start,end]闭区间随机
 * 
 * @return:
 *  区间内的一个随机数值
*/
uint32_t get_random_number(uint32_t start, uint32_t end)
{
    if (end < start)
        std::swap(start, end);

    // 使用静态的 Mersenne Twister 引擎
    static std::mt19937 gen{std::random_device{}()};
    // 定义分布
    std::uniform_int_distribution<int32_t> dis(start, end);
    return dis(gen);
}

/**
 * 获取当前UNIX时间戳，单位秒
*/
uint64_t get_unixtime()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

/**
 * 获取系统当前已运行时长，单位毫秒
*/
uint64_t get_systemruntime()
{
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    return milliseconds;
}

/**
 * 当前线程睡眠X毫秒
 * 
 * @param:
 *  milliseconds设定时间，单位毫秒
*/
void thread_sleep(uint64_t milliseconds) {
    std::chrono::milliseconds duration(milliseconds);
    std::this_thread::sleep_for(duration);
}

/**
 * 多线程安全的输出类
*/
class thread_safe_cout
{
private:
    static std::mutex m_mutex;
    std::unique_lock<std::mutex> lock;

public:
    thread_safe_cout()
    {
        lock = std::unique_lock<std::mutex>(m_mutex); 
    }

    template <class T>
    thread_safe_cout& operator<< (const T& value)
    {
        std::cout << value;
        return *this;
    }

    // 处理特殊情况std::endl
    thread_safe_cout& operator<< (std::ostream& (*pf)(std::ostream&))
    {
        std::cout << pf;
        return *this;
    }
};