#pragma once
#include "base_inc.h"
#include "thread_pool.h"

// 工作转盘占位
constexpr uint32_t TIME_NEAR_SHIFT = 8;
// 工作转盘数组大小
constexpr uint32_t TIME_NEAR = 1 << TIME_NEAR_SHIFT;
// 工作转盘刻度最大值
constexpr uint32_t TIME_NEAR_MASK = TIME_NEAR - 1;

// 上层发散转盘占位
constexpr uint32_t TIME_LEVEL_SHIFT = 6;
// 上层发散转盘数组大小
constexpr uint32_t TIME_LEVEL = 1 << TIME_LEVEL_SHIFT;
// 上层发散转盘刻度最大值
constexpr uint32_t TIME_LEVEL_MASK = TIME_LEVEL - 1;
// N个上层发散转盘
constexpr uint32_t TIME_LEVEL_COUNT = 4;

/**
 * 工作转盘类比秒针，上层发散转盘类比分针、时针
 * 各承载不同时间维度的任务
*/

// 每刻度占：N毫秒
constexpr uint32_t UNIT_MILLISECOND = 10;

// N毫秒后更新当前刻度值
constexpr uint32_t UPDATE_INTERVAL = 2;

// 设计定时器
class timer {
private:
    // 时间任务基本数据
    struct task_node {
        uint32_t expire;
        TASK function;

        task_node(const uint32_t& tick, const TASK& f)
            : expire(tick), function(f) {}
    };
    using QUEUE = std::queue<task_node, std::list<task_node>>;
    // 工作转盘
    std::vector<QUEUE> m_near;
    // 上层发散转盘
    std::vector<std::vector<QUEUE>> m_table;
    // 当前刻度值
    uint32_t m_tick;
    // 记录操作系统运行时间（校正）
    uint64_t m_record;
    // 互斥锁
    std::mutex m_mutex;

public:
    timer();

    /**
     * 添加一个定时任务
     * 
     * @param:
     *  time设定未来X毫秒后执行任务
     *  f函数对象，args函数参数
     * 
     * @return:
     *  函数对象的返回值，异步结果
    */
    template<class F, class... Args>
    static auto thesky_timeout(uint32_t ms, F&& f, Args&&... args)
        -> std::future<decltype(f(args...))>;

    // 运行定时器线程
    void thesky_thread();

private:
    // 更新定时器滴答
    void timer_update();

    // 执行当前刻度的任务队列
    void timer_execute();

    // 转盘转动
    void timer_shift();

    /**
     * 上层发散转盘的任务队列向下派发
     * 
     * @param:
     *  level指定哪个上层发散转盘
     *  index指定转盘的哪一个位置的队列
    */
    void down_queue(uint32_t level, uint32_t index);

    /**
     * 加入任务
     * 
     * @param:
     *  task时间任务对象
    */
    void emplace_task(task_node& task);
};

// 全局定时器
static timer* global_timer = new timer();

timer::timer()
{
    // 预分配工作转盘空间
    m_near.resize(TIME_NEAR);
    // 设置N个上层发散转盘
    m_table.resize(TIME_LEVEL_COUNT);
    for (auto& level : m_table) {
        level.resize(TIME_LEVEL);
    }
    // 初始化当前刻度值为0
    m_tick = 0;
    // 记录当前操作系统刻度值
    m_record = get_systemruntime() / UNIT_MILLISECOND;
}

template<class F, class... Args>
auto timer::thesky_timeout(uint32_t ms, F&& f, Args&&... args)
    -> std::future<decltype(f(args...))>
{
    // 定义返回类型别名
    using RETTYPE = decltype(f(args...));
    uint32_t arg_tick = ms / UNIT_MILLISECOND;
    if (INT32_MAX < arg_tick) {
        thread_safe_cout() << "添加任务的时间溢出界限" << std::endl;
        return std::future<RETTYPE>();
    }
    uint32_t expire = global_timer->m_tick + arg_tick;
    // 将函数+参数打包成一个任务，执行后可以存储返回值
    auto ptr_task = std::make_shared<std::packaged_task<RETTYPE()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    task_node task(expire, [ptr_task](){ (*ptr_task)(); });
    // 加锁插入时间任务
    {
        std::unique_lock<std::mutex> lock(global_timer->m_mutex);
        global_timer->emplace_task(task);
    }
    std::future<RETTYPE> fut = ptr_task->get_future();
    return fut;
}

void timer::thesky_thread()
{
    for(;;) {
        this->timer_update();
        thread_sleep(UPDATE_INTERVAL);
    }
}

void timer::timer_update()
{
    // 定时器与操作系统的刻度值对比
    uint64_t sys_tick = get_systemruntime() / UNIT_MILLISECOND;
    if (m_record == sys_tick) {
        return;
    }
    else if (m_record < sys_tick) {
        uint64_t over_tick = sys_tick - m_record;
        // 经过多少刻度，运行多少次
        for (uint64_t idx = 0; idx < over_tick; ++idx) {
            std::unique_lock<std::mutex> lock(m_mutex);
            this->timer_execute();
            this->timer_shift();
            this->timer_execute();
        }
    }
    m_record = sys_tick;
}

void timer::timer_execute()
{
    // 位运算得到工作转盘当前指向的位置
    uint32_t idx = m_tick & TIME_NEAR_MASK;
    QUEUE& qe_tasks = m_near[idx];
    // 遍历当前刻度的任务队列
    while (!qe_tasks.empty())
    {
        task_node& task = qe_tasks.front();
        global_thread_pool->enqueue(task.function);
        qe_tasks.pop();
    }
}

void timer::timer_shift()
{
    ++m_tick;
    // 整个周期走完，最大值到0
    if (m_tick == 0) {
        down_queue(TIME_LEVEL_COUNT-1, 0);
        return;
    }
    // 工作转盘走完，归零
    if ((m_tick & TIME_NEAR_MASK) == 0) {
        uint32_t shift_ti = m_tick >> TIME_NEAR_SHIFT;
        uint32_t idx = 0, lv = 0;
        for(;;) {
            // 上层发散转盘当前指向位置
            idx = shift_ti & TIME_LEVEL_MASK;
            if (idx != 0) {
                down_queue(lv, idx);
                break;
            }
            shift_ti >>= TIME_LEVEL_MASK;
            ++lv;
        }
    }
}

void timer::down_queue(uint32_t level, uint32_t index)
{
    QUEUE& qe_tasks = m_table[level][index];
    // 遍历当前刻度的任务队列
    while (!qe_tasks.empty())
    {
        task_node& task = qe_tasks.front();
        emplace_task(task);
        qe_tasks.pop();
    }
}

void timer::emplace_task(task_node& task)
{
    uint32_t idx = 0;
    // 当前任务刻度与定时器刻度的上层是一样的
    if ((task.expire | TIME_NEAR_MASK) == (m_tick | TIME_NEAR_MASK)) {
        // 插入工作转盘中
        idx = task.expire & TIME_NEAR_MASK;
        m_near[idx].emplace(task);
    }
    else {
        uint32_t lv = 0, shift = TIME_NEAR << TIME_LEVEL_SHIFT;
        for (;lv < TIME_LEVEL_COUNT-1; ++lv) {
            // 对比上层刻度
            if ((task.expire | (shift-1)) == (m_tick | (shift-1))) {
                break;
            }
            shift <<= TIME_LEVEL_SHIFT;
        }
        // 插入当前层发散转盘
        idx = (task.expire >> (TIME_NEAR_SHIFT + (lv * TIME_LEVEL_SHIFT))) & TIME_LEVEL_MASK;
        m_table[lv][idx].emplace(task);
    }
}

void test_thesky_timer()
{
    // 绑定类的成员函数
    auto timer_thread = std::bind(&timer::thesky_thread, global_timer);
    global_thread_pool->enqueue(timer_thread);

    auto one_task = []() {
        auto thread_id = std::this_thread::get_id();
        thread_safe_cout() << "工作线程id：" << thread_id <<
            " 时间：" << get_unixtime() << " 执行任务 one_task" << std::endl;
        thread_sleep(2000);
        thread_safe_cout() << "工作线程id：" << thread_id <<
        " 时间：" << get_unixtime() << " 执行任务 one_task时睡眠2000ms" << std::endl;
    };
    auto two_task = [](uint32_t a, uint32_t b) -> uint32_t {
        auto thread_id = std::this_thread::get_id();
        thread_safe_cout() << "工作线程id：" << thread_id <<
            " 时间：" << get_unixtime() << " 执行任务 two_task " << a + b << std::endl;
        return a + b;
    };

    thread_safe_cout() << "主线程   时间：" << get_unixtime() <<
        " 加入一个任务：" << " 3000ms后执行" << std::endl;
    global_timer->thesky_timeout(3000, one_task);

    thread_sleep(2000);

    thread_safe_cout() << "主线程   时间：" << get_unixtime() <<
        " 再加入一个任务：" << " 2000ms后执行" << std::endl;
    auto fut = global_timer->thesky_timeout(2000, two_task, 9, 1);
    auto result = fut.get();
    thread_safe_cout() << "主线程   任务异步获取结果：" << result << std::endl;
}