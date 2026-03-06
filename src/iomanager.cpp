// 包含必要的系统头文件
#include <unistd.h>     // Unix标准函数，包含文件操作和进程控制
#include <sys/epoll.h>  // epoll IO多路复用机制
#include <sys/eventfd.h> // eventfd机制，用于线程间唤醒
#include <fcntl.h>      // 文件控制函数
#include <cstring>      // C风格字符串处理
#include <cstdlib>      // 包含exit等函数

#include <mycoroutine/iomanager.h>  // IO管理器头文件

// 调试标志，用于控制调试信息输出
static bool debug = true;

namespace mycoroutine {

/**
 * @brief 获取当前线程的IO管理器实例
 * @return 当前线程的IO管理器指针
 */
IOManager* IOManager::GetThis() 
{
    // 从线程局部存储获取调度器实例并转换为IO管理器类型
    return dynamic_cast<IOManager*>(Scheduler::GetThis());
}

/**
 * @brief 根据事件类型获取对应的事件上下文
 * @param event 事件类型（READ或WRITE）
 * @return 事件上下文引用
 */
IOManager::FdContext::EventContext& IOManager::FdContext::getEventContext(Event event) 
{
    // 确保事件类型有效
    assert(event==READ || event==WRITE || event==NONE);    
    switch (event) 
    {
    case READ:
        return read;  // 返回读事件上下文
    case WRITE:
        return write; // 返回写事件上下文
    case NONE:
        throw std::invalid_argument("NONE event type is not supported");
    default:
        throw std::invalid_argument("Unsupported event type");
    }
}

/**
 * @brief 重置事件上下文
 * @param ctx 要重置的事件上下文引用
 */
void IOManager::FdContext::resetEventContext(EventContext &ctx) 
{
    ctx.scheduler = nullptr; // 清空调度器指针
    ctx.fiber.reset();       // 重置协程智能指针
    ctx.cb = nullptr;        // 清空回调函数
}

/**
 * @brief 触发指定的事件
 * 注意：此函数在调用时需要持有fd_ctx->mutex锁
 * @param event 要触发的事件类型
 */
// no lock
void IOManager::FdContext::triggerEvent(IOManager::Event event) {
    // 确保事件已注册
    assert(events & event);

    // 从已注册事件中删除触发的事件
    events = (Event)(events & ~event);
    
    // 获取对应的事件上下文
    EventContext& ctx = getEventContext(event);
    if (ctx.cb) 
    {
        // 如果有回调函数，则调度回调函数执行
        ctx.scheduler->scheduleLock(&ctx.cb);
    } 
    else 
    {
        // 如果没有回调函数，则调度协程恢复执行
        ctx.scheduler->scheduleLock(&ctx.fiber);
    }

    // 重置事件上下文
    resetEventContext(ctx);
    return;
}

/**
 * @brief IOManager构造函数
 * @param threads 工作线程数量
 * @param use_caller 是否使用调用者线程作为工作线程
 * @param name IO管理器名称
 */
IOManager::IOManager(size_t threads, bool use_caller, const std::string &name): 
Scheduler(threads, use_caller, name), TimerManager()
{
    // 创建epoll实例，参数5000是历史遗留，现代Linux已忽略此值
    m_epfd = epoll_create(5000);
    assert(m_epfd > 0); // 确保epoll创建成功

    // 创建eventfd，用于线程间唤醒通信
    // EFD_NONBLOCK：非阻塞模式
    // EFD_CLOEXEC：进程执行exec时自动关闭
    m_tickleFds = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    assert(m_tickleFds > 0); // 确保eventfd创建成功

    // 注册eventfd到epoll，监听读事件
    epoll_event event;
    event.events  = EPOLLIN | EPOLLET; // 边缘触发模式
    event.data.fd = m_tickleFds;

    // 将eventfd添加到epoll监控
    int rt = epoll_ctl(m_epfd, EPOLL_CTL_ADD, m_tickleFds, &event);
    assert(!rt);

    // 初始化文件描述符上下文数组，初始大小为32
    contextResize(32);

    // 启动调度器
    start();
}

/**
 * @brief IOManager析构函数
 * 清理资源，停止工作线程
 */
IOManager::~IOManager() {
    stop(); // 停止调度器
    close(m_epfd);          // 关闭epoll文件描述符
    close(m_tickleFds);     // 关闭eventfd

    // 清理文件描述符上下文数组
    for (size_t i = 0; i < m_fdContexts.size(); ++i) 
    {
        if (m_fdContexts[i]) 
        {
            delete m_fdContexts[i];
        }
    }
}

/**
 * @brief 调整文件描述符上下文数组大小
 * 注意：此函数需要在调用前持有m_mutex锁
 * @param size 新的数组大小
 */
// no lock
void IOManager::contextResize(size_t size) 
{
    // 调整数组大小
    m_fdContexts.resize(size);

    // 初始化新增的文件描述符上下文
    for (size_t i = 0; i < m_fdContexts.size(); ++i) 
    {
        if (m_fdContexts[i]==nullptr) 
        {
            m_fdContexts[i] = new FdContext();
            m_fdContexts[i]->fd = i; // 设置文件描述符编号
        }
    }
}

/**
 * @brief 添加IO事件监控
 * @param fd 文件描述符
 * @param event 事件类型（READ或WRITE）
 * @param cb 事件回调函数，如果为nullptr则使用当前协程
 * @return 成功返回0，失败返回-1
 */
int IOManager::addEvent(int fd, Event event, std::function<void()> cb) 
{
    // 尝试获取文件描述符对应的上下文
    FdContext *fd_ctx = nullptr;
    
    // 先尝试读锁查找
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        // 上下文数组不够大，需要扩容
        read_lock.unlock();
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);
        // 扩容到fd*1.5的大小，确保有足够空间
        contextResize(fd * 1.5);
        fd_ctx = m_fdContexts[fd];
    }

    // 对文件描述符上下文加锁
    std::lock_guard<std::mutex> lock(fd_ctx->mutex);
    
    // 检查事件是否已经注册
    if(fd_ctx->events & event) 
    {
        return -1;
    }

    // 确定epoll操作类型：修改或添加
    int op = fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    epoll_event epevent;
    epevent.events   = EPOLLET | fd_ctx->events | event; // 边缘触发模式
    epevent.data.ptr = fd_ctx;                           // 存储上下文指针

    // 更新epoll事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "addEvent::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    // 增加待处理事件计数
    ++m_pendingEventCount;

    // 更新文件描述符上下文
    fd_ctx->events = (Event)(fd_ctx->events | event);

    // 更新事件上下文
    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    assert(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);
    event_ctx.scheduler = Scheduler::GetThis();
    if (cb) 
    {
        // 使用回调函数
        event_ctx.cb.swap(cb);
    } 
    else 
    {
        // 使用当前协程作为回调
        event_ctx.fiber = Fiber::GetThis();
        assert(event_ctx.fiber->getState() == Fiber::RUNNING);
    }
    return 0;
}

/**
 * @brief 删除IO事件监控
 * @param fd 文件描述符
 * @param event 事件类型（READ或WRITE）
 * @return 成功返回true，失败返回false
 */
bool IOManager::delEvent(int fd, Event event) {
    // 尝试获取文件描述符对应的上下文
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return false; // 文件描述符不存在
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    // 检查事件是否存在
    if (!(fd_ctx->events & event)) 
    {
        return false; // 事件不存在
    }

    // 计算删除后的事件集
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL; // 修改或删除
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    // 更新epoll事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "delEvent::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    // 减少待处理事件计数
    --m_pendingEventCount;

    // 更新文件描述符上下文
    fd_ctx->events = new_events;

    // 重置事件上下文
    FdContext::EventContext& event_ctx = fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;
}

/**
 * @brief 取消IO事件监控并触发回调
 * @param fd 文件描述符
 * @param event 事件类型（READ或WRITE）
 * @return 成功返回true，失败返回false
 */
bool IOManager::cancelEvent(int fd, Event event) {
    // 尝试获取文件描述符对应的上下文
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return false; // 文件描述符不存在
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    // 检查事件是否存在
    if (!(fd_ctx->events & event)) 
    {
        return false; // 事件不存在
    }

    // 计算删除后的事件集
    Event new_events = (Event)(fd_ctx->events & ~event);
    int op           = new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = EPOLLET | new_events;
    epevent.data.ptr = fd_ctx;

    // 更新epoll事件
    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "cancelEvent::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    // 减少待处理事件计数
    --m_pendingEventCount;

    // 触发事件回调
    fd_ctx->triggerEvent(event);    
    return true;
}

/**
 * @brief 取消文件描述符上的所有事件监控并触发回调
 * @param fd 文件描述符
 * @return 成功返回true，失败返回false
 */
bool IOManager::cancelAll(int fd) {
    // 尝试获取文件描述符对应的上下文
    FdContext *fd_ctx = nullptr;
    
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if ((int)m_fdContexts.size() > fd) 
    {
        fd_ctx = m_fdContexts[fd];
        read_lock.unlock();
    }
    else 
    {
        read_lock.unlock();
        return false; // 文件描述符不存在
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);
    
    // 检查是否有注册的事件
    if (!fd_ctx->events) 
    {
        return false; // 没有注册的事件
    }

    // 从epoll中删除所有事件
    int op = EPOLL_CTL_DEL;
    epoll_event epevent;
    epevent.events   = 0;
    epevent.data.ptr = fd_ctx;

    int rt = epoll_ctl(m_epfd, op, fd, &epevent);
    if (rt) 
    {
        std::cerr << "IOManager::epoll_ctl failed: " << strerror(errno) << std::endl; 
        return -1;
    }

    // 触发并清理读事件
    if (fd_ctx->events & READ) 
    {
        fd_ctx->triggerEvent(READ);
        --m_pendingEventCount;
    }

    // 触发并清理写事件
    if (fd_ctx->events & WRITE) 
    {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    // 确保所有事件都已清理
    assert(fd_ctx->events == 0);
    return true;
}

/**
 * @brief 唤醒一个空闲线程
 * 用于当有新任务时通知工作线程
 */
void IOManager::tickle() 
{
    // 如果没有空闲线程，则不需要唤醒
    if(!hasIdleThreads()) 
    {
        return;
    }
    // 向eventfd写入一个uint64_t值1，唤醒阻塞在epoll_wait的线程
    uint64_t one = 1;
    int rt = write(m_tickleFds, &one, sizeof(one));
    assert(rt == sizeof(one)); // 确保写入成功
}

/**
 * @brief 判断调度器是否可以停止
 * @return 可以停止返回true，否则返回false
 */
bool IOManager::stopping() 
{
    // 获取下一个定时器的超时时间
    uint64_t timeout = getNextTimer();
    // 当没有定时器、没有待处理事件且基础调度器可以停止时，IO管理器可以停止
    return timeout == ~0ull && m_pendingEventCount == 0 && Scheduler::stopping();
}


/**
 * @brief 空闲线程执行的函数
 * 线程没有任务时阻塞等待IO事件或定时器事件
 */
void IOManager::idle() 
{
    // 最大处理事件数量
    static const uint64_t MAX_EVNETS = 256;
    // 用于存储epoll返回的事件
    std::unique_ptr<epoll_event[]> events(new epoll_event[MAX_EVNETS]);

    while (true) 
    {
        if(debug) std::cout << "IOManager::idle(),run in thread: " << Thread::GetThreadId() << std::endl; 

        // 检查是否可以停止
        if(stopping()) 
        {
            if(debug) std::cout << "name = " << getName() << " idle exits in thread: " << Thread::GetThreadId() << std::endl;
            break;
        }

        // 阻塞等待IO事件，超时时间为下一个定时器的时间
        int rt = 0;
        while(true)
        {
            static const uint64_t MAX_TIMEOUT = 5000; // 最大超时时间5秒
            uint64_t next_timeout = getNextTimer();
            next_timeout = std::min(next_timeout, MAX_TIMEOUT);

            // 阻塞等待事件发生
            rt = epoll_wait(m_epfd, events.get(), MAX_EVNETS, (int)next_timeout);
            // 被信号中断则重试
            if(rt < 0 && errno == EINTR) 
            {
                continue;
            } 
            else 
            {
                break;
            }
        };

        // 处理所有超时的定时器回调
        std::vector<std::function<void()>> cbs;
        listExpiredCb(cbs);
        if(!cbs.empty()) 
        {
            for(const auto& cb : cbs)
            {
                scheduleLock(cb); // 调度回调函数执行
            }
            cbs.clear();
        }
        
        // 处理所有就绪的IO事件
        for (int i = 0; i < rt; ++i) 
        {
            epoll_event& event = events[i];

            // 处理唤醒事件（eventfd事件）
            if (event.data.fd == m_tickleFds) 
            {
                uint64_t dummy;
                // 边缘触发模式，需要读取所有数据
                while (read(m_tickleFds, &dummy, sizeof(dummy)) > 0);
                continue;
            }

            // 处理其他IO事件
            FdContext *fd_ctx = (FdContext *)event.data.ptr;
            std::lock_guard<std::mutex> lock(fd_ctx->mutex);

            // 将错误或挂起事件转换为对应的读或写事件
            if (event.events & (EPOLLERR | EPOLLHUP)) 
            {
                event.events |= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }
            
            // 计算实际发生的事件
            int real_events = NONE;
            if (event.events & EPOLLIN) 
            {
                real_events |= READ;
            }
            if (event.events & EPOLLOUT) 
            {
                real_events |= WRITE;
            }

            // 如果没有注册的事件发生，跳过
            if ((fd_ctx->events & real_events) == NONE) 
            {
                continue;
            }

            // 计算剩余未处理的事件
            int left_events = (fd_ctx->events & ~real_events);
            int op          = left_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL; // 修改或删除
            event.events    = EPOLLET | left_events;

            // 更新epoll事件
            int rt2 = epoll_ctl(m_epfd, op, fd_ctx->fd, &event);
            if (rt2) 
            {
                std::cerr << "idle::epoll_ctl failed: " << strerror(errno) << std::endl; 
                continue;
            }

            // 触发读事件回调
            if (real_events & READ) 
            {
                fd_ctx->triggerEvent(READ);
                --m_pendingEventCount;
            }
            // 触发写事件回调
            if (real_events & WRITE) 
            {
                fd_ctx->triggerEvent(WRITE);
                --m_pendingEventCount;
            }
        } // end for

        // 让出CPU执行权，切换到其他协程
        Fiber::GetThis()->yield();
  
    } // end while(true)
}

/**
 * @brief 定时器插入到队首时的回调
 * 当新定时器比现有定时器更早超时，需要唤醒等待中的线程
 */
void IOManager::onTimerInsertedAtFront() 
{
    tickle(); // 唤醒线程，以便重新计算超时时间
}
}
