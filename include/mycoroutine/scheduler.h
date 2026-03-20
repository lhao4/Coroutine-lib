#ifndef __MYCOROUTINE_SCHEDULER_H_
#define __MYCOROUTINE_SCHEDULER_H_

#include <mycoroutine/hook.h>
#include <mycoroutine/fiber.h>    // 包含协程相关头文件
#include <mycoroutine/coroutine_pool.h> // 协程池
#include <mycoroutine/thread.h>   // 包含线程相关头文件

#include <mutex>      // 互斥锁头文件
#include <vector>     // 向量容器头文件
#include <deque>      // 双端队列容器
#include <string>     // 字符串头文件
#include <utility>    // std::move
#include <cstdint>    // uint64_t

namespace mycoroutine {  // mycoroutine命名空间

/**
 * @brief 调度策略
 */
enum class SchedulePolicy
{
    FIFO = 0,    ///< 先进先出
    PRIORITY,    ///< 优先级高优先
    MLFQ,        ///< 多级反馈队列
    EDF,         ///< Earliest Deadline First
    HYBRID       ///< 截止期优先 + 优先级回退
};

/**
 * @brief 任务调度参数
 */
struct ScheduleOptions
{
    int thread = -1;               ///< 指定线程，-1表示任意线程
    int priority = 0;              ///< 优先级，越大越先执行
    uint64_t deadline_ms = 0;      ///< 截止时间戳（毫秒），0表示无截止期
};

/**
 * @brief MLFQ 参数配置
 */
struct MLFQConfig
{
    uint8_t levels = 3;            ///< 队列层数，最小为1，0层为最高优先级
    bool demote_on_yield = true;   ///< 任务yield后是否降级
    bool enable_aging = true;      ///< 是否启用aging防饥饿
    uint64_t aging_sequence_gap = 64; ///< 触发一次提升所需的序列差
};

/**
 * @brief 协程调度器类
 * 负责管理线程池和协程任务调度
 */
class Scheduler
{
public:
    class SchedulerRef
    {
    public:
        explicit SchedulerRef(Scheduler* scheduler)
            : m_scheduler(scheduler)
        {
        }

        void schedule(const std::shared_ptr<Fiber>& fiber, int thread)
        {
            if (!fiber)
            {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_scheduler)
            {
                return;
            }
            m_scheduler->scheduleLock(fiber, thread);
        }

        void invalidate()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_scheduler = nullptr;
        }

    private:
        std::mutex m_mutex;
        Scheduler* m_scheduler = nullptr;
    };

private:
    struct ScheduleTask;

public:
    /**
     * @brief 构造函数
     * @param threads 线程池大小（额外创建的线程数）
     * @param use_caller 是否将调用者线程也作为工作线程
     * @param name 调度器名称
     */
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name="Scheduler");
    
    /**
     * @brief 析构函数
     */
    virtual ~Scheduler();
    
    /**
     * @brief 获取调度器名称
     * @return 调度器名称的常量引用
     */
    const std::string& getName() const {return m_name;}

public:    
    /**
     * @brief 获取正在运行的调度器
     * @return 当前线程的调度器指针
     */
    static Scheduler* GetThis();

    /**
     * @brief 获取可安全失效的调度引用
     */
    std::weak_ptr<SchedulerRef> getSchedulerRef() const { return m_schedulerRef; }

    /**
     * @brief 设置调度策略
     */
    void setPolicy(SchedulePolicy policy);

    /**
     * @brief 获取当前调度策略
     */
    SchedulePolicy getPolicy();

    /**
     * @brief 配置MLFQ参数
     */
    void setMLFQConfig(const MLFQConfig& config);

    /**
     * @brief 获取当前MLFQ参数
     */
    MLFQConfig getMLFQConfig();

    /**
     * @brief 设置协程池每个key的最大缓存容量
     */
    void setCoroutinePoolMaxCachedPerKey(size_t max_cached_per_key);

    /**
     * @brief 查询协程池缓存数量
     */
    size_t getCoroutinePoolCachedCount(size_t stacksize = 0,
                                       bool run_in_scheduler = true,
                                       bool use_shared_stack = false);

protected:
    /**
     * @brief 设置正在运行的调度器
     * 将当前调度器实例设置为线程局部存储的调度器
     */
    void SetThis();
    
public:    
    /**
     * @brief 添加任务到任务队列（线程安全，支持优先级/截止时间）
     * @tparam FiberOrCb 任务类型，可以是协程指针或回调函数
     * @param fc 任务对象
     * @param options 调度选项
     */
    template <class FiberOrCb>
    void scheduleEx(FiberOrCb fc, const ScheduleOptions& options = ScheduleOptions());

    /**
     * @brief 添加任务到任务队列（线程安全）
     * @tparam FiberOrCb 任务类型，可以是协程指针或回调函数
     * @param fc 任务对象
     * @param thread 指定任务执行的线程ID，-1表示任意线程
     */
    template <class FiberOrCb>
    void scheduleLock(FiberOrCb fc, int thread = -1) 
    {
        ScheduleOptions options;
        options.thread = thread;
        scheduleEx(fc, options);
    }

    /**
     * @brief 以共享栈协程方式调度一个回调任务
     * @param cb 任务回调
     * @param stacksize 协程栈大小，0表示默认值
     * @param thread 指定执行线程，-1表示任意线程
     */
    void scheduleShared(std::function<void()> cb, size_t stacksize = 0, int thread = -1)
    {
        auto fiber = std::make_shared<Fiber>(std::move(cb), stacksize, true, true);
        ScheduleOptions options;
        options.thread = thread;
        scheduleEx(fiber, options);
    }
    
    /**
     * @brief 启动线程池
     * 创建并启动所有工作线程
     */
    virtual void start();
    
    /**
     * @brief 关闭线程池
     * 等待所有任务完成，并停止所有工作线程
     */
    virtual void stop();    

protected:
    /**
     * @brief 唤醒线程函数
     * 通知其他线程有新任务到来
     */
    virtual void tickle();
    
    /**
     * @brief 工作线程主函数
     * 从任务队列获取任务并执行
     */
    virtual void run();

    /**
     * @brief 空闲协程函数
     * 当没有任务时执行
     */
    virtual void idle();
    
    /**
     * @brief 判断调度器是否可以停止
     * @return 调度器是否可以停止的标志
     */
    virtual bool stopping();

    /**
     * @brief 检查是否有空闲线程
     * @return 是否有空闲线程
     */
    bool hasIdleThreads() {return m_idleThreadCount>0;}

    bool pickNextTaskLocked(int thread_id, ScheduleTask& out_task, bool& tickle_me);

private:
    /**
     * @brief 任务结构体
     * 用于存储协程任务或回调函数
     */
    struct ScheduleTask
    {
        std::shared_ptr<Fiber> fiber;  // 协程指针
        std::function<void()> cb;      // 回调函数
        int thread;                    // 指定任务需要运行的线程id
        int priority = 0;              // 调度优先级
        uint8_t mlfq_level = 0;        // MLFQ队列层级（0最高）
        uint64_t deadline_ms = 0;      // 截止期（0表示无）
        uint64_t sequence = 0;         // 入队序号（稳定排序）

        /**
         * @brief 默认构造函数
         */
        ScheduleTask()
        {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }

        /**
         * @brief 构造函数（接收协程指针）
         * @param f 协程指针
         * @param thr 线程ID
         */
        ScheduleTask(std::shared_ptr<Fiber> f, int thr)
        {
            fiber = std::move(f);
            thread = thr;
        }

        /**
         * @brief 构造函数（接收协程指针的指针）
         * @param f 协程指针的指针
         * @param thr 线程ID
         */
        ScheduleTask(std::shared_ptr<Fiber>* f, int thr)
        {
            fiber.swap(*f);
            thread = thr;
        }    

        /**
         * @brief 构造函数（接收回调函数）
         * @param f 回调函数
         * @param thr 线程ID
         */
        ScheduleTask(std::function<void()> f, int thr)
        {
            cb = std::move(f);
            thread = thr;
        }        

        /**
         * @brief 构造函数（接收回调函数的指针）
         * @param f 回调函数的指针
         * @param thr 线程ID
         */
        ScheduleTask(std::function<void()>* f, int thr)
        {
            cb.swap(*f);
            thread = thr;
        }

        /**
         * @brief 重置任务
         * 清空协程指针、回调函数和线程ID
         */
        void reset()
        {
            fiber = nullptr;
            cb = nullptr;
            thread = -1;
        }    
    };

private:
    std::string m_name;                  // 调度器名称
    bool m_useCaller;                    // 主线程是否用作工作线程
    std::mutex m_mutex;                  // 互斥锁，保护任务队列
    std::vector<std::shared_ptr<Thread>> m_threads;  // 线程池
    std::deque<ScheduleTask> m_tasks;    // 任务队列
    CoroutinePool m_coroutinePool;       // 协程池（复用回调包装协程）
    SchedulePolicy m_policy = SchedulePolicy::FIFO; // 当前调度策略
    MLFQConfig m_mlfqConfig;             // MLFQ运行参数
    uint64_t m_taskSequence = 0;         // 任务序列号生成器
    std::vector<int> m_threadIds;        // 工作线程的线程ID列表
    size_t m_threadCount = 0;            // 需要额外创建的线程数
    std::atomic<size_t> m_activeThreadCount = {0};  // 活跃线程数
    std::atomic<size_t> m_idleThreadCount = {0};    // 空闲线程数
    std::shared_ptr<Fiber> m_schedulerFiber;  // 调度协程（仅当m_useCaller为true时有效）
    std::shared_ptr<SchedulerRef> m_schedulerRef; // 对外提供的安全调度引用
    int m_rootThread = -1;               // 主线程ID（仅当m_useCaller为true时有效）
    bool m_stopping = false;             // 是否正在关闭调度器
};

} // end namespace mycoroutine

template <class FiberOrCb>
void mycoroutine::Scheduler::scheduleEx(FiberOrCb fc, const ScheduleOptions& options)
{
    bool need_tickle = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        need_tickle = m_tasks.empty();

        ScheduleTask task(fc, options.thread);
        if (task.fiber || task.cb)
        {
            task.priority = options.priority;
            task.mlfq_level = 0;
            task.deadline_ms = options.deadline_ms;
            task.sequence = m_taskSequence++;
            m_tasks.emplace_back(std::move(task));
        }
    }

    if (need_tickle)
    {
        tickle();
    }
}

#endif
