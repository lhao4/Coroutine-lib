#ifndef __MYCOROUTINE_THREAD_H_
#define __MYCOROUTINE_THREAD_H_

#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>         

namespace mycoroutine
{

/**
 * @brief 信号量类
 * 
 * 用于线程间的同步机制，实现了经典的P/V操作（wait/signal）。
 * 信号量是一种计数器，用于控制对共享资源的访问，常用于线程同步。
 */
class Semaphore 
{
private:
    std::mutex mtx;                // 互斥锁，保护count的访问
    std::condition_variable cv;    // 条件变量，用于线程等待和唤醒
    int count;                     // 信号量计数器，表示可用资源的数量

public:
    /**
     * @brief 构造函数
     * 
     * @param count_ 信号量的初始值，默认为0
     */
    explicit Semaphore(int count_ = 0) : count(count_) {}
    
    /**
     * @brief P操作（等待操作）
     * 
     * 尝试获取资源，如果count为0则阻塞等待，直到有资源可用。
     * 获取成功后将count减1。
     */
    void wait() 
    {
        std::unique_lock<std::mutex> lock(mtx);
        // 使用while避免虚假唤醒
        while (count == 0) { 
            cv.wait(lock); // 等待signal信号
        }
        count--; // 获取资源，计数器减1
    }

    /**
     * @brief V操作（释放操作）
     * 
     * 释放资源，将count加1，并通知一个等待的线程。
     */
    void signal() 
    {
        std::unique_lock<std::mutex> lock(mtx);
        count++; // 释放资源，计数器加1
        cv.notify_one();  // 通知一个等待的线程
    }
};

/**
 * @brief 线程类
 * 
 * 封装了pthread线程库的功能，提供了更易用的接口来创建和管理线程。
 * 系统中存在两种线程：1）系统自动创建的主线程；2）通过Thread类创建的线程。
 */
class Thread 
{
public:
    /**
     * @brief 构造函数
     * 
     * 创建并启动一个新线程，执行指定的回调函数。
     * 
     * @param cb 线程要执行的回调函数
     * @param name 线程名称
     */
    Thread(std::function<void()> cb, const std::string& name);
    
    /**
     * @brief 析构函数
     * 
     * 确保线程正确结束，如果线程仍在运行则分离它。
     */
    ~Thread();

    /**
     * @brief 获取线程ID
     * 
     * @return pid_t 线程的系统ID
     */
    pid_t getId() const { return m_id; }
    
    /**
     * @brief 获取线程名称
     * 
     * @return const std::string& 线程名称的引用
     */
    const std::string& getName() const { return m_name; }

    /**
     * @brief 等待线程结束
     * 
     * 阻塞当前线程，直到被join的线程执行完毕。
     */
    void join();

public:
    /**
     * @brief 获取当前线程的系统ID
     * 
     * @return pid_t 当前线程的系统ID
     */
    static pid_t GetThreadId();
    
    /**
     * @brief 获取当前线程对象
     * 
     * @return Thread* 当前线程对象的指针
     */
    static Thread* GetThis();

    /**
     * @brief 获取当前线程的名称
     * 
     * @return const std::string& 当前线程名称的引用
     */
    static const std::string& GetName();
    
    /**
     * @brief 设置当前线程的名称
     * 
     * @param name 要设置的线程名称
     */
    static void SetName(const std::string& name);

private:
    /**
     * @brief 线程执行函数
     * 
     * 由pthread_create调用的静态函数，用于启动线程。
     * 
     * @param arg 线程对象的指针
     * @return void* 线程返回值
     */
    static void* run(void* arg);

private:
    pid_t m_id = -1;              // 线程ID，初始为-1
    pthread_t m_thread = 0;       // pthread线程句柄，初始为0

    std::function<void()> m_cb;   // 线程要执行的回调函数
    std::string m_name;           // 线程名称
    
    Semaphore m_semaphore;        // 信号量，用于线程同步
};

}



#endif