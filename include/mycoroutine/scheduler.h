#ifndef __MYCOROUTINE_SCHEDULER_H_
#define __MYCOROUTINE_SCHEDULER_H_

// 调度器头文件定义
// 注意：hook.h头文件被注释掉了，暂时没有使用
//#include "hook.h"
#include <mycoroutine/fiber.h>    // 包含协程相关头文件
#include <mycoroutine/thread.h>   // 包含线程相关头文件

#include <mutex>      // 互斥锁头文件
#include <vector>     // 向量容器头文件
#include <string>     // 字符串头文件

namespace mycoroutine {  // mycoroutine命名空间

/**
 * @brief 协程调度器类
 * 负责管理线程池和协程任务调度
 */
class Scheduler
{
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

protected:
    /**
     * @brief 设置正在运行的调度器
     * 将当前调度器实例设置为线程局部存储的调度器
     */
    void SetThis();
    
public:    
    /**
     * @brief 添加任务到任务队列（线程安全）
     * @tparam FiberOrCb 任务类型，可以是协程指针或回调函数
     * @param fc 任务对象
     * @param thread 指定任务执行的线程ID，-1表示任意线程
     */
    template <class FiberOrCb>
    void scheduleLock(FiberOrCb fc, int thread = -1) 
    {
        bool need_tickle;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // 如果任务队列为空，说明所有线程都处于空闲状态，需要唤醒它们
            need_tickle = m_tasks.empty();
            
            // 创建任务对象
            ScheduleTask task(fc, thread);
            if (task.fiber || task.cb) 
            {
                // 将任务添加到队列
                m_tasks.push_back(task);
            }
        }
        
        // 如果需要唤醒线程
        if(need_tickle)
        {
            tickle();
        }
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
            fiber = f;
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
            cb = f;
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
    std::vector<ScheduleTask> m_tasks;   // 任务队列
    std::vector<int> m_threadIds;        // 工作线程的线程ID列表
    size_t m_threadCount = 0;            // 需要额外创建的线程数
    std::atomic<size_t> m_activeThreadCount = {0};  // 活跃线程数
    std::atomic<size_t> m_idleThreadCount = {0};    // 空闲线程数
    std::shared_ptr<Fiber> m_schedulerFiber;  // 调度协程（仅当m_useCaller为true时有效）
    int m_rootThread = -1;               // 主线程ID（仅当m_useCaller为true时有效）
    bool m_stopping = false;             // 是否正在关闭调度器
};

} // end namespace mycoroutine

#endif
