#include <mycoroutine/thread.h>

#include <sys/syscall.h>  // 用于调用系统调用获取线程ID
#include <iostream>       // 用于输出错误信息
#include <unistd.h>       // 提供POSIX操作系统API

namespace mycoroutine {

// 线程局部存储，存储当前线程对象指针，每个线程独立拥有一份
static thread_local Thread* t_thread          = nullptr;
// 线程局部存储，存储当前线程名称，每个线程独立拥有一份
static thread_local std::string t_thread_name = "UNKNOWN";

/**
 * @brief 获取当前线程的系统ID
 * 
 * 使用syscall(SYS_gettid)获取真实的线程ID，而不是pthread_t
 * pthread_t是进程内的线程标识，而tid是系统级的线程标识
 * 
 * @return pid_t 当前线程的系统ID
 */
pid_t Thread::GetThreadId()
{
	return syscall(SYS_gettid);
}

/**
 * @brief 获取当前线程对象
 * 
 * 返回存储在线程局部存储中的线程对象指针
 * 
 * @return Thread* 当前线程对象的指针
 */
Thread* Thread::GetThis()
{
    return t_thread;
}

/**
 * @brief 获取当前线程的名称
 * 
 * 返回存储在线程局部存储中的线程名称
 * 
 * @return const std::string& 当前线程名称的引用
 */
const std::string& Thread::GetName() 
{
    return t_thread_name;
}

/**
 * @brief 设置当前线程的名称
 * 
 * 同时设置线程对象中的名称和线程局部存储中的名称
 * 
 * @param name 要设置的线程名称
 */
void Thread::SetName(const std::string &name) 
{
    if (t_thread) 
    {
        t_thread->m_name = name; // 更新线程对象中的名称
    }
    t_thread_name = name; // 更新线程局部存储中的名称
}

/**
 * @brief 构造函数
 * 
 * 创建并启动一个新线程，执行指定的回调函数
 * 
 * @param cb 线程要执行的回调函数
 * @param name 线程名称
 * 	hrow std::logic_error 如果线程创建失败
 */
Thread::Thread(std::function<void()> cb, const std::string &name): 
m_cb(cb), m_name(name) 
{
    // 创建线程，指定run函数作为线程入口点，this作为参数
    int rt = pthread_create(&m_thread, nullptr, &Thread::run, this);
    if (rt) 
    {
        // 线程创建失败，输出错误信息并抛出异常
        std::cerr << "pthread_create thread fail, rt=" << rt << " name=" << name;
        throw std::logic_error("pthread_create error");
    }
    // 等待线程函数完成初始化，确保线程已经完全启动
    m_semaphore.wait();
}

/**
 * @brief 析构函数
 * 
 * 确保线程正确结束，如果线程仍在运行则分离它
 * 分离后的线程结束时会自动释放资源，不需要join
 */
Thread::~Thread() 
{
    if (m_thread) 
    {
        pthread_detach(m_thread); // 分离线程
        m_thread = 0;             // 重置线程句柄
    }
}

/**
 * @brief 等待线程结束
 * 
 * 阻塞当前线程，直到被join的线程执行完毕
 * 
 * @throw std::logic_error 如果join失败
 */
void Thread::join() 
{
    if (m_thread) 
    {
        int rt = pthread_join(m_thread, nullptr);
        if (rt) 
        {
            // join失败，输出错误信息并抛出异常
            std::cerr << "pthread_join failed, rt = " << rt << ", name = " << m_name << std::endl;
            throw std::logic_error("pthread_join error");
        }
        m_thread = 0; // 重置线程句柄
    }
}

/**
 * @brief 线程执行函数
 * 
 * 由pthread_create调用的静态函数，用于初始化线程并执行用户指定的回调函数
 * 
 * @param arg 线程对象的指针
 * @return void* 线程返回值
 */
void* Thread::run(void* arg) 
{
    // 将参数转换为线程对象指针
    Thread* thread = (Thread*)arg;

    // 初始化线程局部存储
    t_thread       = thread;               // 设置当前线程对象
    t_thread_name  = thread->m_name;       // 设置线程名称
    thread->m_id   = GetThreadId();        // 获取并设置线程ID
    // 设置pthread线程名称（系统限制长度为15）
    pthread_setname_np(pthread_self(), thread->m_name.substr(0, 15).c_str());

    // 使用swap减少引用计数，避免循环引用
    std::function<void()> cb;
    cb.swap(thread->m_cb); // swap可以减少m_cb中智能指针的引用计数
    
    // 通知构造函数线程初始化已完成
    thread->m_semaphore.signal();

    // 执行用户指定的回调函数
    cb();
    return 0;
}

}