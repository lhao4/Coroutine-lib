#ifndef __MYCOROUTINE_FD_MANAGER_H_
#define __MYCOROUTINE_FD_MANAGER_H_

/**
 * @file fd_manager.h
 * @brief 文件描述符管理器头文件
 * @details 提供文件描述符上下文管理功能，包括非阻塞设置、超时控制等
 */

#include <memory>          // 智能指针
#include <shared_mutex>    // 读写锁
#include <mycoroutine/thread.h>        // 线程相关头文件


namespace mycoroutine{  // mycoroutine命名空间

/**
 * @brief 文件描述符上下文类
 * @details 封装文件描述符的各种属性，包括阻塞状态、超时设置等
 */
class FdCtx : public std::enable_shared_from_this<FdCtx>
{
private:
	bool m_isInit = false;       // 文件描述符是否已初始化
	bool m_isSocket = false;     // 是否为套接字描述符
	bool m_sysNonblock = false;  // 系统层面是否非阻塞
	bool m_userNonblock = false; // 用户层面是否非阻塞
	bool m_isClosed = false;     // 文件描述符是否已关闭
	int m_fd;                    // 文件描述符值

	uint64_t m_recvTimeout = (uint64_t)-1; // 接收超时时间（毫秒）
	uint64_t m_sendTimeout = (uint64_t)-1; // 发送超时时间（毫秒）

public:
	/**
	 * @brief 构造函数
	 * @param fd 文件描述符
	 */
	FdCtx(int fd);
	
	/**
	 * @brief 析构函数
	 */
	~FdCtx();

	/**
	 * @brief 初始化文件描述符上下文
	 * @return 初始化是否成功
	 */
	bool init();
	
	/**
	 * @brief 获取文件描述符是否已初始化
	 * @return 是否已初始化
	 */
	bool isInit() const {return m_isInit;}
	
	/**
	 * @brief 获取是否为套接字描述符
	 * @return 是否为套接字
	 */
	bool isSocket() const {return m_isSocket;}
	
	/**
	 * @brief 获取文件描述符是否已关闭
	 * @return 是否已关闭
	 */
	bool isClosed() const {return m_isClosed;}

	/**
	 * @brief 设置用户层面非阻塞状态
	 * @param v 是否非阻塞
	 */
	void setUserNonblock(bool v) {m_userNonblock = v;}
	
	/**
	 * @brief 获取用户层面非阻塞状态
	 * @return 是否非阻塞
	 */
	bool getUserNonblock() const {return m_userNonblock;}

	/**
	 * @brief 设置系统层面非阻塞状态
	 * @param v 是否非阻塞
	 */
	void setSysNonblock(bool v) {m_sysNonblock = v;}
	
	/**
	 * @brief 获取系统层面非阻塞状态
	 * @return 是否非阻塞
	 */
	bool getSysNonblock() const {return m_sysNonblock;}

	/**
	 * @brief 设置文件描述符超时时间
	 * @param type 超时类型，SO_RCVTIMEO(接收超时)或SO_SNDTIMEO(发送超时)
	 * @param v 超时时间（毫秒）
	 */
	void setTimeout(int type, uint64_t v);
	
	/**
	 * @brief 获取文件描述符超时时间
	 * @param type 超时类型，SO_RCVTIMEO(接收超时)或SO_SNDTIMEO(发送超时)
	 * @return 超时时间（毫秒）
	 */
	uint64_t getTimeout(int type);
};

/**
 * @brief 文件描述符管理器类
 * @details 管理所有文件描述符的上下文对象，提供获取和删除功能
 */
class FdManager
{
public:
	/**
	 * @brief 构造函数
	 */
	FdManager();

	/**
	 * @brief 获取文件描述符对应的上下文对象
	 * @param fd 文件描述符
	 * @param auto_create 是否自动创建上下文对象
	 * @return 文件描述符上下文智能指针
	 */
	std::shared_ptr<FdCtx> get(int fd, bool auto_create = false);
	
	/**
	 * @brief 删除文件描述符对应的上下文对象
	 * @param fd 文件描述符
	 */
	void del(int fd);

private:
	std::shared_mutex m_mutex;                        // 读写锁，保护m_datas
	std::vector<std::shared_ptr<FdCtx>> m_datas;      // 文件描述符上下文数组
};

/**
 * @brief 单例模板类
 * @details 提供线程安全的单例实现
 * @tparam T 单例类型
 */
template<typename T>
class Singleton
{
private:
    static T* instance;     // 单例实例指针
    static std::mutex mutex; // 互斥锁，保证线程安全

protected:
    /**
     * @brief 保护构造函数，防止外部直接实例化
     */
    Singleton() {}

public:
    // 删除拷贝构造和赋值操作，确保单例的唯一性
    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

    /**
     * @brief 获取单例实例
     * @return 单例实例指针
     */
    static T* GetInstance() 
    {
        std::lock_guard<std::mutex> lock(mutex); // 加锁确保线程安全
        if (instance == nullptr) 
        {
            instance = new T();  // 创建单例实例
        }
        return instance;
    }

    /**
     * @brief 销毁单例实例
     */
    static void DestroyInstance() 
    {
        std::lock_guard<std::mutex> lock(mutex);
        delete instance;
        instance = nullptr;
    }
};

/**
 * @brief 文件描述符管理器单例类型别名
 */
typedef Singleton<FdManager> FdMgr;

} // end namespace mycoroutine

#endif // __MYCOROUTINE_FD_MANAGER_H_