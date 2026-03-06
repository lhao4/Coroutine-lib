#ifndef __MYCOROUTINE_IOMANAGER_H__
#define __MYCOROUTINE_IOMANAGER_H__

#include <mycoroutine/scheduler.h> // 引入调度器基类
#include <mycoroutine/timer.h>     // 引入定时器管理器

namespace mycoroutine {

/**
 * @brief IO管理器类
 * IO管理器工作流程：
 * 1 注册事件 -> 2 等待事件就绪 -> 3 调度回调函数 -> 4 注销事件 -> 5 执行回调
 */
class IOManager : public Scheduler, public TimerManager 
{
public:
    /**
     * @brief 事件类型枚举
     * 对应epoll的事件类型
     */
    enum Event 
    {
        NONE = 0x0,     // 无事件
        READ = 0x1,     // 读事件，对应EPOLLIN
        WRITE = 0x4     // 写事件，对应EPOLLOUT
    };

private:
    /**
     * @brief 文件描述符上下文
     * 管理文件描述符的所有事件和回调信息
     */
    struct FdContext 
    {
        /**
         * @brief 事件上下文
         * 存储事件的回调函数或协程信息
         */
        struct EventContext 
        {
            Scheduler *scheduler = nullptr;        // 事件所属的调度器
            std::shared_ptr<Fiber> fiber;          // 事件触发时要执行的协程
            std::function<void()> cb;              // 事件触发时要执行的回调函数
        };

        EventContext read;      // 读事件上下文
        EventContext write;     // 写事件上下文
        int fd = 0;             // 文件描述符
        Event events = NONE;    // 当前注册的事件
        std::mutex mutex;       // 用于保护该结构体的互斥锁

        /**
         * @brief 获取指定事件类型的事件上下文
         * @param event 事件类型
         * @return 事件上下文引用
         */
        EventContext& getEventContext(Event event);
        
        /**
         * @brief 重置事件上下文
         * @param ctx 要重置的事件上下文
         */
        void resetEventContext(EventContext &ctx);
        
        /**
         * @brief 触发事件
         * @param event 要触发的事件类型
         */
        void triggerEvent(Event event);        
    };

public:
    /**
     * @brief 构造函数
     * @param threads 工作线程数量
     * @param use_caller 是否将调用者线程作为工作线程
     * @param name IO管理器名称
     */
    IOManager(size_t threads = 1, bool use_caller = true, const std::string &name = "IOManager");
    
    /**
     * @brief 析构函数
     */
    ~IOManager();

    /**
     * @brief 添加IO事件监控
     * @param fd 文件描述符
     * @param event 事件类型
     * @param cb 事件回调函数，默认为nullptr（使用当前协程）
     * @return 成功返回0，失败返回-1
     */
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    
    /**
     * @brief 删除IO事件监控
     * @param fd 文件描述符
     * @param event 事件类型
     * @return 成功返回true，失败返回false
     */
    bool delEvent(int fd, Event event);
    
    /**
     * @brief 取消IO事件监控并触发回调
     * @param fd 文件描述符
     * @param event 事件类型
     * @return 成功返回true，失败返回false
     */
    bool cancelEvent(int fd, Event event);
    
    /**
     * @brief 取消文件描述符上的所有事件监控并触发回调
     * @param fd 文件描述符
     * @return 成功返回true，失败返回false
     */
    bool cancelAll(int fd);

    /**
     * @brief 获取当前线程的IO管理器实例
     * @return IO管理器指针
     */
    static IOManager* GetThis();

protected:
    /**
     * @brief 唤醒一个空闲线程
     * 重写自Scheduler类
     */
    void tickle() override;
    
    /**
     * @brief 判断调度器是否可以停止
     * 重写自Scheduler类
     * @return 可以停止返回true，否则返回false
     */
    bool stopping() override;
    
    /**
     * @brief 空闲线程执行的函数
     * 重写自Scheduler类
     */
    void idle() override;

    /**
     * @brief 定时器插入到队首时的回调
     * 重写自TimerManager类
     */
    void onTimerInsertedAtFront() override;

    /**
     * @brief 调整文件描述符上下文数组大小
     * @param size 新的数组大小
     */
    void contextResize(size_t size);

private:
    int m_epfd = 0;                      // epoll文件描述符
    int m_tickleFds = 0;                 // 线程唤醒eventfd
    std::atomic<size_t> m_pendingEventCount = {0}; // 待处理事件数量
    std::shared_mutex m_mutex;           // 用于保护m_fdContexts的读写锁
    std::vector<FdContext *> m_fdContexts; // 文件描述符上下文数组
};

} // end namespace mycoroutine

#endif