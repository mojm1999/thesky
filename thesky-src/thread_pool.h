#pragma once
#include "base_inc.h"

class thread_pool
{
private:
    // 定义任务类型
    using TASK = std::function<void()>;
    // 工作线程数组
    std::vector<std::thread> m_vec_workers;
    // 任务队列（待执行）
    std::queue<TASK, std::list<TASK>> m_qe_tasks;
    // 互斥锁
    std::mutex m_qe_mutex;
    // 条件变量
    std::condition_variable m_condition;
    // 线程池是否停止运行
    bool m_stop { false };

public:
    thread_pool(uint32_t expect_threads);
    ~thread_pool();

    /**
     * 添加一个任务到执行队列
     * 
     * @param:
     *  F是函数类型，f是完美转发引用传递，保持左值或右值的属性
     *  Args是模板参数包，其可以接受多个不定数量的参数
     * 
     * @return:
     *  -> 后面的是函数的尾置返回类型
     *  decltype编译时推导 f 调用的结果类型
     *  用std::future对象接收返回值，稍后访问异步操作结果
    */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<decltype(f(args...))>;
};

thread_pool::thread_pool(uint32_t expect_threads)
{
    uint32_t num_threads = std::thread::hardware_concurrency();
    if (num_threads < expect_threads) 
        throw std::runtime_error("期望线程数大于系统核心数");

    // 预申请空间，避免vector扩容
    m_vec_workers.reserve(expect_threads);
    for (uint32_t idx = 0; idx < expect_threads; ++idx) {
        m_vec_workers.emplace_back(
            [this] {
                for(;;) {
                    TASK task;
                    // 加锁，从任务队列中取出一个
                    {
                        std::unique_lock<std::mutex> lock(m_qe_mutex);
                        m_condition.wait(lock,
                            [this] { return this->m_stop || !this->m_qe_tasks.empty(); });
                        if (m_stop && m_qe_tasks.empty())
                            return;
                        task = std::move(m_qe_tasks.front());
                        m_qe_tasks.pop();
                    }
                    // 释放锁，工作线程执行任务
                    task();
                }
            }
        );
    }
}

thread_pool::~thread_pool()
{
    // 设置线程池状态为暂停
    {
        std::unique_lock<std::mutex> lock(m_qe_mutex);
        m_stop = true;
    }
    // 唤醒所有工作线程
    m_condition.notify_all();
    for (std::thread &worker : m_vec_workers) {
        worker.join();
    }
}

template<class F, class... Args>
auto thread_pool::enqueue(F&& f, Args&&... args)
    -> std::future<decltype(f(args...))>
{
    // 定义返回类型别名
    using RETTYPE = decltype(f(args...));
    // 将函数+参数打包成一个任务，执行后可以存储返回值
    auto ptr_task = std::make_shared<std::packaged_task<RETTYPE()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    {
        std::unique_lock<std::mutex> lock(m_qe_mutex);
        if (m_stop)
            throw std::runtime_error("添加任务时线程池已暂停工作");
        // 将任务加入执行队列
        m_qe_tasks.emplace([ptr_task](){ (*ptr_task)(); });
    }
    // 唤醒一条工作线程
    m_condition.notify_one();
    std::future<RETTYPE> fut = ptr_task->get_future();
    return fut;
}

void test_output_something(uint32_t rand)
{
    auto thread_id = std::this_thread::get_id();
    thread_safe_cout() << "工作线程id：" << thread_id <<
        "，随机输出一个数字：" << rand << std::endl;
}

void test_thread_pool()
{
    auto ptr_pool = new thread_pool(8);
    uint32_t count = 10, cur_time = 0;
    while(count != 0) {
        cur_time = get_unixtime();
        uint32_t args = get_random_number(0, 100);
        ptr_pool->enqueue(test_output_something, args);
        thread_safe_cout() << "主线程   时间：" << cur_time <<
            " 加入一个任务：" << args << std::endl;
        --count;
    }
    delete ptr_pool;
}