#include <mycoroutine/scheduler.h>

// 调试开关，设置为true可以输出更多调试信息
static bool debug = false;

namespace mycoroutine {

// 线程局部存储，指向当前线程的调度器实例
static thread_local Scheduler* t_scheduler = nullptr;

namespace {

MLFQConfig NormalizeMLFQConfig(const MLFQConfig& config)
{
    MLFQConfig normalized = config;
    if (normalized.levels == 0)
    {
        normalized.levels = 1;
    }
    if (normalized.aging_sequence_gap == 0)
    {
        normalized.aging_sequence_gap = 1;
    }
    return normalized;
}

} // namespace

/**
 * @brief 获取当前线程的调度器实例
 * @return 当前线程的调度器指针
 */
Scheduler* Scheduler::GetThis()
{
    return t_scheduler;
}

void Scheduler::setPolicy(SchedulePolicy policy)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_policy = policy;
}

SchedulePolicy Scheduler::getPolicy()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_policy;
}

void Scheduler::setMLFQConfig(const MLFQConfig& config)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mlfqConfig = NormalizeMLFQConfig(config);
}

MLFQConfig Scheduler::getMLFQConfig()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mlfqConfig;
}

void Scheduler::setCoroutinePoolMaxCachedPerKey(size_t max_cached_per_key)
{
    m_coroutinePool.setMaxCachedPerKey(max_cached_per_key);
}

size_t Scheduler::getCoroutinePoolCachedCount(size_t stacksize,
                                              bool run_in_scheduler,
                                              bool use_shared_stack)
{
    return m_coroutinePool.cachedCount(stacksize, run_in_scheduler, use_shared_stack);
}

/**
 * @brief 设置当前线程的调度器实例
 */
void Scheduler::SetThis()
{
    t_scheduler = this;
}

/**
 * @brief 调度器构造函数
 * @param threads 线程池大小（额外创建的线程数）
 * @param use_caller 是否将调用者线程也作为工作线程
 * @param name 调度器名称
 */
Scheduler::Scheduler(size_t threads, bool use_caller, const std::string &name):
    m_name(name), m_useCaller(use_caller)
{
    assert(threads>0 && Scheduler::GetThis()==nullptr);

    // 设置当前线程的调度器
    SetThis();

    // 设置线程名称
    Thread::SetName(m_name);

    // 使用主线程当作工作线程
    if(use_caller)
    {
        // 主线程也作为工作线程，所以额外创建的线程数减1
        threads --;

        // 创建主协程
        Fiber::GetThis();

        // 创建调度协程，参数为run函数，栈大小为0（使用默认值），false表示该协程退出后返回主协程
        m_schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run, this), 0, false)); 
        Fiber::SetSchedulerFiber(m_schedulerFiber.get());
        
        // 记录主线程ID
        m_rootThread = Thread::GetThreadId();
        m_threadIds.push_back(m_rootThread);
    }

    // 设置需要额外创建的线程数
    m_threadCount = threads;
    if(debug) std::cout << "Scheduler::Scheduler() success\n";
}

/**
 * @brief 调度器析构函数
 */
Scheduler::~Scheduler()
{
    assert(stopping()==true);
    if (GetThis() == this) 
    {
        t_scheduler = nullptr;
    }
    if(debug) std::cout << "Scheduler::~Scheduler() success\n";
}

/**
 * @brief 启动调度器
 * 创建并启动所有工作线程
 */
void Scheduler::start()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if(m_stopping)
    {
        std::cerr << "Scheduler is stopped" << std::endl;
        return;
    }

    assert(m_threads.empty());
    // 调整线程池大小
    m_threads.resize(m_threadCount);
    for(size_t i=0;i<m_threadCount;i++)
    {
        // 创建工作线程，每个线程执行run函数
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this), m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
    if(debug) std::cout << "Scheduler::start() success\n";
}

/**
 * @brief 工作线程的主函数
 * 从任务队列获取任务并执行
 */
void Scheduler::run()
{
    int thread_id = Thread::GetThreadId();
    if(debug) std::cout << "Schedule::run() starts in thread: " << thread_id << std::endl;
    
    set_hook_enable(true);

    // 设置当前线程的调度器
    SetThis();

    // 运行在新创建的线程 -> 需要创建主协程
    if(thread_id != m_rootThread)
    {
        Fiber::GetThis();
    }

    // 创建空闲协程，当没有任务时执行
    std::shared_ptr<Fiber> idle_fiber = std::make_shared<Fiber>(std::bind(&Scheduler::idle, this));
    ScheduleTask task;
    
    while(true)
    {
        task.reset();
        bool tickle_me = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (pickNextTaskLocked(thread_id, task, tickle_me))
            {
                m_activeThreadCount++;
            }
        }

        // 如果有其他线程的任务，唤醒其他线程
        if(tickle_me)
        {
            tickle();
        }

        // 3 执行任务
        if(task.fiber)
        {
            {
                std::lock_guard<std::mutex> lock(task.fiber->m_mutex);
                if(task.fiber->getState()!=Fiber::TERM)
                {
                    // 恢复协程执行
                    task.fiber->resume();    
                }
            }

            bool mlfq_requeue = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                mlfq_requeue = (m_policy == SchedulePolicy::MLFQ) &&
                               task.fiber &&
                               task.fiber->getState() == Fiber::READY;
                if (mlfq_requeue)
                {
                    const uint8_t max_level = static_cast<uint8_t>(m_mlfqConfig.levels - 1);
                    if (m_mlfqConfig.demote_on_yield && task.mlfq_level < max_level)
                    {
                        ++task.mlfq_level; // 任务用完时间片后降级
                    }
                    task.sequence = m_taskSequence++;
                    m_tasks.push_back(task);
                }
            }

            if (mlfq_requeue)
            {
                tickle();
            }

            m_activeThreadCount--;
            task.reset();
        }
        else if(task.cb)
        {
            // 使用协程池复用回调协程
            std::shared_ptr<Fiber> cb_fiber = m_coroutinePool.acquire(std::move(task.cb));
            if (!cb_fiber)
            {
                m_activeThreadCount--;
                task.reset();
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(cb_fiber->m_mutex);
                cb_fiber->resume();            
            }
            if (cb_fiber->getState() == Fiber::TERM)
            {
                m_coroutinePool.release(cb_fiber);
            }
            m_activeThreadCount--;
            task.reset();    
        }
        // 4 无任务 -> 执行空闲协程
        else
        {
            // 系统关闭 -> idle协程将从死循环跳出并结束 -> 此时的idle协程状态为TERM -> 再次进入将跳出循环并退出run()
            if (idle_fiber->getState() == Fiber::TERM) 
            {
                if(debug) std::cout << "Schedule::run() ends in thread: " << thread_id << std::endl;
                break;
            }
            m_idleThreadCount++;
            // 执行空闲协程
            idle_fiber->resume();                
            m_idleThreadCount--;
        }
    }
    
}

bool Scheduler::pickNextTaskLocked(int thread_id, ScheduleTask& out_task, bool& tickle_me)
{
    tickle_me = false;
    if (m_tasks.empty())
    {
        return false;
    }

    auto match_thread = [thread_id](const ScheduleTask& task) {
        return task.thread == -1 || task.thread == thread_id;
    };

    auto better_priority = [](const ScheduleTask& lhs, const ScheduleTask& rhs) {
        if (lhs.priority != rhs.priority)
        {
            return lhs.priority > rhs.priority;
        }
        return lhs.sequence < rhs.sequence;
    };

    auto effective_mlfq_level = [&](const ScheduleTask& task) -> uint8_t {
        const uint8_t max_level = static_cast<uint8_t>(m_mlfqConfig.levels - 1);
        const uint8_t base_level = task.mlfq_level > max_level ? max_level : task.mlfq_level;

        if (!m_mlfqConfig.enable_aging || base_level == 0 || task.sequence >= m_taskSequence)
        {
            return base_level;
        }

        const uint64_t wait_gap = m_taskSequence - task.sequence;
        const uint64_t promote = wait_gap / m_mlfqConfig.aging_sequence_gap;
        if (promote >= base_level)
        {
            return 0;
        }
        return static_cast<uint8_t>(base_level - promote);
    };

    auto better_mlfq = [&](const ScheduleTask& lhs, const ScheduleTask& rhs) {
        const uint8_t lhs_level = effective_mlfq_level(lhs);
        const uint8_t rhs_level = effective_mlfq_level(rhs);
        if (lhs_level != rhs_level)
        {
            return lhs_level < rhs_level;
        }
        return lhs.sequence < rhs.sequence;
    };

    auto better_edf = [](const ScheduleTask& lhs, const ScheduleTask& rhs) {
        const bool lhs_has_deadline = lhs.deadline_ms > 0;
        const bool rhs_has_deadline = rhs.deadline_ms > 0;

        if (lhs_has_deadline != rhs_has_deadline)
        {
            return lhs_has_deadline; // 有deadline优先于无deadline
        }
        if (lhs_has_deadline && rhs_has_deadline && lhs.deadline_ms != rhs.deadline_ms)
        {
            return lhs.deadline_ms < rhs.deadline_ms;
        }
        return lhs.sequence < rhs.sequence;
    };

    auto better_hybrid = [&](const ScheduleTask& lhs, const ScheduleTask& rhs) {
        const bool lhs_has_deadline = lhs.deadline_ms > 0;
        const bool rhs_has_deadline = rhs.deadline_ms > 0;

        if (lhs_has_deadline != rhs_has_deadline)
        {
            return lhs_has_deadline;
        }
        if (lhs_has_deadline && rhs_has_deadline)
        {
            if (lhs.deadline_ms != rhs.deadline_ms)
            {
                return lhs.deadline_ms < rhs.deadline_ms;
            }
            return better_priority(lhs, rhs);
        }
        return better_priority(lhs, rhs);
    };

    auto best_it = m_tasks.end();
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it)
    {
        if (!match_thread(*it))
        {
            tickle_me = true;
            continue;
        }

        if (best_it == m_tasks.end())
        {
            best_it = it;
            if (m_policy == SchedulePolicy::FIFO)
            {
                break;
            }
            continue;
        }

        bool better = false;
        switch (m_policy)
        {
        case SchedulePolicy::FIFO:
            break;
        case SchedulePolicy::PRIORITY:
            better = better_priority(*it, *best_it);
            break;
        case SchedulePolicy::MLFQ:
            better = better_mlfq(*it, *best_it);
            break;
        case SchedulePolicy::EDF:
            better = better_edf(*it, *best_it);
            break;
        case SchedulePolicy::HYBRID:
            better = better_hybrid(*it, *best_it);
            break;
        }

        if (better)
        {
            best_it = it;
        }
    }

    if (best_it == m_tasks.end())
    {
        return false;
    }

    out_task = *best_it;
    m_tasks.erase(best_it);
    if (!m_tasks.empty())
    {
        tickle_me = true;
    }
    return true;
}

/**
 * @brief 停止调度器
 * 等待所有任务完成，并停止所有工作线程
 */
void Scheduler::stop()
{
    if(debug) std::cout << "Schedule::stop() starts in thread: " << Thread::GetThreadId() << std::endl;
    
    // 如果调度器已经在停止过程中，直接返回
    if(stopping())
    {
        return;
    }

    // 标记调度器为停止状态
    m_stopping = true;    

    // 如果使用调用者线程作为工作线程
    if (m_useCaller) 
    {
        assert(GetThis() == this);
    } 
    else 
    {
        assert(GetThis() != this);
    }
    
    // 唤醒所有工作线程，让它们检查停止状态
    for (size_t i = 0; i < m_threadCount; i++) 
    {
        tickle();
    }

    // 唤醒调度协程
    if (m_schedulerFiber) 
    {
        tickle();
    }

    // 恢复调度协程执行，等待其完成
    if(m_schedulerFiber)
    {
        m_schedulerFiber->resume();
        if(debug) std::cout << "m_schedulerFiber ends in thread:" << Thread::GetThreadId() << std::endl;
    }

    // 收集所有工作线程
    std::vector<std::shared_ptr<Thread>> thrs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        thrs.swap(m_threads);
    }

    // 等待所有工作线程结束
    for(auto &i : thrs)
    {
        i->join();
    }
    if(debug) std::cout << "Schedule::stop() ends in thread:" << Thread::GetThreadId() << std::endl;
}

/**
 * @brief 唤醒线程函数
 * 通知其他线程有新任务到来
 * 这里是一个空实现，可以在派生类中重写以提供实际的唤醒机制
 */
void Scheduler::tickle()
{
}

/**
 * @brief 空闲协程函数
 * 当没有任务时执行，定期让出CPU时间片
 */
void Scheduler::idle()
{
    while(!stopping())
    {
        if(debug) std::cout << "Scheduler::idle(), sleeping in thread: " << Thread::GetThreadId() << std::endl;    
        sleep(1);    
        // 让出执行权
        Fiber::GetThis()->yield();
    }
}

/**
 * @brief 判断调度器是否可以停止
 * @return 如果调度器已标记为停止且任务队列为空且没有活跃线程，则返回true
 */
bool Scheduler::stopping() 
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}


}
