#pragma once
#include <iostream>
#include <thread>
#include <future>
#include <list>
#include <queue>
#include <functional>
#include <random>
#include <chrono>

uint32_t get_random_number(uint32_t start, uint32_t end)
{
    if (end < start)
        std::swap(start, end);

    // 使用静态的 Mersenne Twister 引擎
    static std::mt19937 gen{std::random_device{}()};
    // 定义分布
    std::uniform_int_distribution<int> dis(start, end);
    return dis(gen);
}

uint64_t get_unixtime()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

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

    thread_safe_cout& operator<< (std::ostream& (*pf)(std::ostream&))
    {
        std::cout << pf;
        return *this;
    }
};