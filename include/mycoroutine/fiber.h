#ifndef __MYCOROUTINE_FIBER_H_
#define __MYCOROUTINE_FIBER_H_

#include <iostream>     // 标准输入输出
#include <memory>       // 智能指针
#include <atomic>       // 原子操作
#include <functional>   // 函数对象
#include <cassert>      // 断言
#include <ucontext.h>   // 上下文切换
#include <unistd.h>     // 系统调用
#include <mutex>        // 互斥锁

namespace mycoroutine {

/**
 * @brief 协程类，基于ucontext_t实现的用户级协程
 * @details 该类实现了用户级协程功能，支持协程的创建、切换、恢复和销毁
 *          使用智能指针管理协程生命周期，避免资源泄漏
 */
class Fiber : public std::enable_shared_from_this<Fiber>
{
public:
    /**
     * @brief 协程状态枚举
     * @details 定义了协程可能的三种状态
     */
    enum State
    {
        READY,  // 就绪态：协程已创建但尚未运行或已让出执行权
        RUNNING,// 运行态：协程正在执行中
        TERM    // 终止态：协程执行完毕
    };

private:
    /**
     * @brief 私有构造函数，仅用于创建主协程
     * @details 主协程构造函数，由GetThis()调用
     *          主协程不需要分配独立的栈空间，使用线程默认栈
     */
    Fiber();

public:
    /**
     * @brief 构造函数，创建子协程
     * @param cb 协程要执行的回调函数
     * @param stacksize 协程栈大小，默认为0（将使用默认大小128KB）
     * @param run_in_scheduler 是否在调度器中运行，默认为true
     * @details 创建一个新的协程，分配栈空间并设置执行上下文
     */
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool run_in_scheduler = true);
    
    /**
     * @brief 析构函数
     * @details 释放协程资源，包括栈空间
     */
    ~Fiber();

    /**
     * @brief 重置协程
     * @param cb 新的协程回调函数
     * @details 重用已完成的协程，设置新的回调函数
     *          仅当协程处于TERM状态时可以重置
     */
    void reset(std::function<void()> cb);

    /**
     * @brief 恢复协程执行
     * @details 从当前协程切换到该协程继续执行
     *          只能恢复处于READY状态的协程
     */
    void resume();
    
    /**
     * @brief 协程让出执行权
     * @details 暂停当前协程的执行，将控制权交回调用者
     *          协程可以是主动让出或执行完毕后自动让出
     */
    void yield();

    /**
     * @brief 获取协程ID
     * @return 协程的唯一标识符
     */
    uint64_t getId() const {return m_id;}
    
    /**
     * @brief 获取协程状态
     * @return 协程当前的状态（READY、RUNNING或TERM）
     */
    State getState() const {return m_state;}

public:
    /**
     * @brief 设置当前运行的协程
     * @param f 要设置为当前运行的协程指针
     * @details 更新线程局部存储中的当前协程指针
     */
    static void SetThis(Fiber *f);

    /**
     * @brief 获取当前运行的协程
     * @return 返回当前运行协程的智能指针
     * @details 如果当前没有协程在运行，则创建一个主协程
     */
    static std::shared_ptr<Fiber> GetThis();

    /**
     * @brief 设置调度协程
     * @param f 调度协程指针
     * @details 调度协程负责调度其他协程的运行
     */
    static void SetSchedulerFiber(Fiber* f);
    
    /**
     * @brief 获取当前运行的协程ID
     * @return 当前协程的ID，如果没有协程运行则返回-1
     */
    static uint64_t GetFiberId();

    /**
     * @brief 协程入口函数
     * @details 所有协程的统一入口点，负责执行协程回调函数
     *          并在执行完毕后正确清理资源并让出执行权
     */
    static void MainFunc();

private:
    uint64_t m_id = 0;            ///< 协程ID，唯一标识一个协程
    uint32_t m_stacksize = 0;     ///< 协程栈大小
    State m_state = READY;        ///< 协程状态
    ucontext_t m_ctx;             ///< 协程上下文，保存执行环境
    void* m_stack = nullptr;      ///< 协程栈指针，指向分配的栈空间
    std::function<void()> m_cb;   ///< 协程回调函数，协程要执行的任务
    bool m_runInScheduler;        ///< 是否在调度器中运行，决定让出时返回到哪个协程

public:
    std::mutex m_mutex;           ///< 协程互斥锁，用于同步操作
};

}

#endif