#include "mycoroutine/fd_manager.h" // 引入文件描述符管理器头文件
#include "mycoroutine/hook.h"       // 引入系统调用钩子

#include <sys/types.h>   // 引入系统类型定义
#include <sys/stat.h>    // 引入文件状态相关函数
#include <unistd.h>      // 引入系统调用函数

namespace mycoroutine{  // mycoroutine命名空间

// 显式实例化单例模板类
template class Singleton<FdManager>;

// 静态成员变量需要在类外定义
template<typename T>
T* Singleton<T>::instance = nullptr;

template<typename T>
std::mutex Singleton<T>::mutex;    

/**
 * @brief 文件描述符上下文构造函数
 * @param fd 文件描述符
 */
FdCtx::FdCtx(int fd):
	m_fd(fd)  // 初始化文件描述符
{
	init();  // 初始化上下文信息
}

/**
 * @brief 文件描述符上下文析构函数
 */
FdCtx::~FdCtx()
{
	// 析构函数暂时为空，资源释放由智能指针管理
}

/**
 * @brief 初始化文件描述符上下文
 * @return 初始化是否成功
 */
bool FdCtx::init()
{
	// 如果已经初始化，直接返回成功
	if(m_isInit)
	{
		return true;
	}
	
	struct stat statbuf;  // 文件状态缓冲区
	
	// 检查文件描述符是否有效
	if(-1 == fstat(m_fd, &statbuf))
	{
		m_isInit = false;    // 标记为未初始化
		m_isSocket = false;  // 标记为非套接字
	}
	else
	{
		m_isInit = true;     // 标记为已初始化
		// 判断是否为套接字文件描述符
		m_isSocket = S_ISSOCK(statbuf.st_mode);    
	}

	// 如果是套接字，自动设置为非阻塞模式
	if(m_isSocket)
	{
		// 使用钩子后的fcntl获取当前文件描述符的标志
		int flags = fcntl_f(m_fd, F_GETFL, 0);
		// 如果不是非阻塞模式，设置为非阻塞
		if(!(flags & O_NONBLOCK))
		{
			fcntl_f(m_fd, F_SETFL, flags | O_NONBLOCK);
		}
		m_sysNonblock = true;  // 标记系统层面为非阻塞
	}
	else
	{
		m_sysNonblock = false; // 非套接字保持默认阻塞状态
	}

	return m_isInit;  // 返回初始化结果
}

/**
 * @brief 设置文件描述符超时时间
 * @param type 超时类型，SO_RCVTIMEO(接收超时)或SO_SNDTIMEO(发送超时)
 * @param v 超时时间（毫秒）
 */
void FdCtx::setTimeout(int type, uint64_t v)
{
	if(type == SO_RCVTIMEO)
	{
		m_recvTimeout = v;  // 设置接收超时时间
	}
	else
	{
		m_sendTimeout = v;  // 设置发送超时时间
	}
}

/**
 * @brief 获取文件描述符超时时间
 * @param type 超时类型，SO_RCVTIMEO(接收超时)或SO_SNDTIMEO(发送超时)
 * @return 超时时间（毫秒）
 */
uint64_t FdCtx::getTimeout(int type)
{
	if(type == SO_RCVTIMEO)
	{
		return m_recvTimeout;  // 返回接收超时时间
	}
	else
	{
		return m_sendTimeout;  // 返回发送超时时间
	}
}

/**
 * @brief 文件描述符管理器构造函数
 */
FdManager::FdManager()
{
	// 初始时预分配64个文件描述符上下文空间
	m_datas.resize(64);
}

/**
 * @brief 获取文件描述符对应的上下文对象
 * @param fd 文件描述符
 * @param auto_create 是否自动创建上下文对象
 * @return 文件描述符上下文智能指针
 */
std::shared_ptr<FdCtx> FdManager::get(int fd, bool auto_create)
{
	// 无效文件描述符直接返回nullptr
	if(fd == -1)
	{
		return nullptr;
	}

	// 读锁保护
	std::shared_lock<std::shared_mutex> read_lock(m_mutex);
	// 检查容器大小是否足够
	if(m_datas.size() <= (size_t)fd)
	{
		// 如果不自动创建，返回nullptr
		if(auto_create == false)
		{
			return nullptr;
		}
	}
	else
	{
		// 如果上下文对象已存在或者不自动创建，直接返回现有对象
		if(m_datas[fd] || !auto_create)
		{
			return m_datas[fd];
		}
	}

	// 升级锁：释放读锁，获取写锁
	read_lock.unlock();
	std::unique_lock<std::shared_mutex> write_lock(m_mutex);

	// 二次检查，防止在锁切换期间被其他线程修改
	if(m_datas.size() <= (size_t)fd)
	{
		// 扩容，扩展为当前需要的1.5倍
		m_datas.resize(fd * 1.5);
	}

	// 创建新的文件描述符上下文对象
	m_datas[fd] = std::make_shared<FdCtx>(fd);
	return m_datas[fd];
}

/**
 * @brief 删除文件描述符对应的上下文对象
 * @param fd 文件描述符
 */
void FdManager::del(int fd)
{
	// 写锁保护
	std::unique_lock<std::shared_mutex> write_lock(m_mutex);
	// 检查文件描述符是否在有效范围内
	if(m_datas.size() <= (size_t)fd)
	{
		return;
	}
	// 重置智能指针，释放资源
	m_datas[fd].reset();
}

} // end namespace mycoroutine
