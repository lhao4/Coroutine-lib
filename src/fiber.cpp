#include <mycoroutine/fiber.h>

// 调试模式开关，设置为true时会输出协程的创建、销毁和切换信息
static bool debug = false;

namespace mycoroutine {

/**
 * 线程局部存储变量，保存当前线程相关的协程信息
 * 这些变量是线程私有的，每个线程都有自己独立的副本
 */

// 当前正在运行的协程指针
static thread_local Fiber* t_fiber = nullptr;

// 主协程，每个线程的第一个协程，负责调度其他协程
static thread_local std::shared_ptr<Fiber> t_thread_fiber = nullptr;

// 调度协程指针，用于协程间切换回调度器
static thread_local Fiber* t_scheduler_fiber = nullptr;

// 全局协程ID计数器，用于为每个协程分配唯一ID
static std::atomic<uint64_t> s_fiber_id{0};

// 当前系统中协程总数计数器
static std::atomic<uint64_t> s_fiber_count{0};

/**
 * @brief 设置当前正在运行的协程
 * @param f 要设置为当前运行的协程指针
 */
void Fiber::SetThis(Fiber *f)
{
    t_fiber = f;
}

/**
 * @brief 获取当前运行的协程
 * @return 返回当前运行协程的智能指针
 * @details 如果当前没有协程在运行，则创建一个主协程
 *          主协程是线程运行的第一个协程，由操作系统调度
 */
std::shared_ptr<Fiber> Fiber::GetThis()
{
    if(t_fiber)
    {
        // 如果已经有协程在运行，返回该协程的智能指针
        return t_fiber->shared_from_this();
    }

    // 创建主协程
    std::shared_ptr<Fiber> main_fiber(new Fiber());
    t_thread_fiber = main_fiber;
    t_scheduler_fiber = main_fiber.get(); // 默认情况下，主协程也是调度协程
    
    assert(t_fiber == main_fiber.get());
    return t_fiber->shared_from_this();
}

/**
 * @brief 设置调度协程
 * @param f 调度协程指针
 * @details 调度协程负责调度其他协程的运行，通常是主协程或专门的调度器协程
 */
void Fiber::SetSchedulerFiber(Fiber* f)
{
    t_scheduler_fiber = f;
}

/**
 * @brief 获取当前运行的协程ID
 * @return 返回当前协程ID，如果没有协程运行则返回-1
 */
uint64_t Fiber::GetFiberId()
{
    if(t_fiber)
    {
        return t_fiber->getId();
    }
    return (uint64_t)-1;
}

/**
 * @brief 主协程构造函数（私有）
 * @details 仅由GetThis()调用，创建线程的第一个协程
 *          主协程使用线程的栈空间，不需要额外分配
 */
Fiber::Fiber()
{
    // 设置当前协程为自己
    SetThis(this);
    
    // 主协程创建时处于运行状态
    m_state = RUNNING;
    
    // 获取当前上下文
    if(getcontext(&m_ctx))
    {
        std::cerr << "Fiber() failed\n";
        pthread_exit(NULL);
    }
    
    // 分配唯一ID并增加协程计数
    m_id = s_fiber_id++;
    s_fiber_count++;
    
    if(debug) 
        std::cout << "Fiber(): main id = " << m_id << std::endl;
}

/**
 * @brief 子协程构造函数
 * @param cb 协程要执行的回调函数
 * @param stacksize 协程栈大小，默认为0（将使用默认值）
 * @param run_in_scheduler 是否在调度器中运行
 * @details 创建一个新的协程，分配栈空间并设置上下文
 */
Fiber::Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler):
    m_cb(cb), m_runInScheduler(run_in_scheduler)
{
    // 初始状态为就绪态
    m_state = READY;

    // 分配协程栈空间，默认128KB
    m_stacksize = stacksize ? stacksize : 128000;
    m_stack = malloc(m_stacksize);

    // 获取当前上下文作为基础
    if(getcontext(&m_ctx))
    {
        std::cerr << "Fiber(std::function<void()> cb, size_t stacksize, bool run_in_scheduler) failed\n";
        pthread_exit(NULL);
    }
    
    // 设置上下文属性
    m_ctx.uc_link = nullptr;       // 协程结束时不自动切换到其他协程
    m_ctx.uc_stack.ss_sp = m_stack; // 设置栈指针
    m_ctx.uc_stack.ss_size = m_stacksize; // 设置栈大小
    
    // 创建协程上下文，设置入口函数为MainFunc
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
    
    // 分配唯一ID并增加协程计数
    m_id = s_fiber_id++;
    s_fiber_count++;
    
    if(debug) 
        std::cout << "Fiber(): child id = " << m_id << std::endl;
}

/**
 * @brief 协程析构函数
 * @details 减少协程计数并释放栈空间
 */
Fiber::~Fiber()
{
    s_fiber_count--;
    // 如果有分配栈空间则释放
    if(m_stack)
    {
        free(m_stack);
    }
    
    if(debug) 
        std::cout << "~Fiber(): id = " << m_id << std::endl;
}

/**
 * @brief 重置协程函数
 * @param cb 新的协程回调函数
 * @details 重置一个已完成的协程，重新设置其回调函数和上下文
 *          仅当协程处于TERM状态时才能重置
 */
void Fiber::reset(std::function<void()> cb)
{
    // 只有已终止的协程才能重置
    assert(m_stack != nullptr && m_state == TERM);

    // 重置协程状态为就绪
    m_state = READY;
    m_cb = cb;

    // 重新获取上下文
    if(getcontext(&m_ctx))
    {
        std::cerr << "reset() failed\n";
        pthread_exit(NULL);
    }

    // 重新设置上下文属性
    m_ctx.uc_link = nullptr;
    m_ctx.uc_stack.ss_sp = m_stack;
    m_ctx.uc_stack.ss_size = m_stacksize;
    makecontext(&m_ctx, &Fiber::MainFunc, 0);
}

/**
 * @brief 恢复协程执行
 * @details 从当前协程切换到该协程继续执行
 *          只能恢复处于READY状态的协程
 */
void Fiber::resume()
{
    // 确保协程处于就绪状态
    assert(m_state == READY);
    
    // 将协程状态设置为运行中
    m_state = RUNNING;

    if(m_runInScheduler)
    {
        // 如果协程在调度器中运行，则切换到调度协程
        SetThis(this);
        if(swapcontext(&(t_scheduler_fiber->m_ctx), &m_ctx))
        {
            std::cerr << "resume() to t_scheduler_fiber failed\n";
            pthread_exit(NULL);
        }
    }
    else
    {
        // 如果协程不在调度器中运行，则切换到主协程
        SetThis(this);
        if(swapcontext(&(t_thread_fiber->m_ctx), &m_ctx))
        {
            std::cerr << "resume() to t_thread_fiber failed\n";
            pthread_exit(NULL);
        }
    }
}

/**
 * @brief 协程让出执行权
 * @details 暂停当前协程的执行，将控制权交回调用者
 *          可以是运行中的协程主动让出，也可以是已完成的协程自动让出
 */
void Fiber::yield()
{
    // 确保协程处于运行中或已终止状态
    assert(m_state == RUNNING || m_state == TERM);

    // 如果协程未终止，则设置状态为就绪
    if(m_state != TERM)
    {
        m_state = READY;
    }

    if(m_runInScheduler)
    {
        // 如果协程在调度器中运行，则切换回调度协程
        SetThis(t_scheduler_fiber);
        if(swapcontext(&m_ctx, &(t_scheduler_fiber->m_ctx)))
        {
            std::cerr << "yield() to to t_scheduler_fiber failed\n";
            pthread_exit(NULL);
        }
    }
    else
    {
        // 如果协程不在调度器中运行，则切换回主协程
        SetThis(t_thread_fiber.get());
        if(swapcontext(&m_ctx, &(t_thread_fiber->m_ctx)))
        {
            std::cerr << "yield() to t_thread_fiber failed\n";
            pthread_exit(NULL);
        }
    }
}

/**
 * @brief 协程入口函数
 * @details 所有协程的入口点，负责执行协程回调函数并在完成后让出执行权
 *          使用智能指针确保协程在执行过程中不会被销毁
 */
void Fiber::MainFunc()
{
    // 获取当前协程的智能指针，延长其生命周期
    std::shared_ptr<Fiber> curr = GetThis();
    assert(curr != nullptr);

    // 执行协程回调函数
    curr->m_cb();
    
    // 执行完成后清除回调函数，避免循环引用
    curr->m_cb = nullptr;
    
    // 设置协程状态为已终止
    curr->m_state = TERM;

    // 保存裸指针，因为接下来要重置智能指针
    auto raw_ptr = curr.get();
    
    // 释放智能指针引用，减少引用计数
    curr.reset();
    
    // 让出执行权，返回到调用者协程
    raw_ptr->yield();
}

}