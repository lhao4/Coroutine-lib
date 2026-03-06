#include <mycoroutine/hook.h>         // 引入钩子头文件
#include <mycoroutine/iomanager.h>  // 引入IO调度器
#include <dlfcn.h>         // 动态库加载函数
#include <iostream>        // 标准输入输出
#include <cstdarg>         // 可变参数支持
#include <mycoroutine/fd_manager.h>    // 引入文件描述符管理器
#include <string.h>        // 字符串处理函数

// 宏定义：对所有需要hook的函数应用同一个操作
#define HOOK_FUN(XX) \
    XX(sleep) \
    XX(usleep) \
    XX(nanosleep) \
    XX(socket) \
    XX(connect) \
    XX(accept) \
    XX(read) \
    XX(readv) \
    XX(recv) \
    XX(recvfrom) \
    XX(recvmsg) \
    XX(write) \
    XX(writev) \
    XX(send) \
    XX(sendto) \
    XX(sendmsg) \
    XX(close) \
    XX(fcntl) \
    XX(ioctl) \
    XX(getsockopt) \
    XX(setsockopt) 

namespace mycoroutine{  // mycoroutine命名空间

// 线程本地存储：标识当前线程是否启用了钩子
static thread_local bool t_hook_enable = false;

/**
 * @brief 获取当前线程是否启用了钩子
 * @return 是否启用钩子
 */
bool is_hook_enable()
{
    return t_hook_enable;
}

/**
 * @brief 设置当前线程钩子的启用状态
 * @param flag 是否启用钩子
 */
void set_hook_enable(bool flag)
{
    t_hook_enable = flag;
}

/**
 * @brief 初始化系统调用钩子
 * @details 从动态库中获取原始系统调用函数指针
 */
void hook_init()
{
	static bool is_inited = false;  // 确保只初始化一次
	if(is_inited)
	{
		return;
	}

	// test
	is_inited = true;

	// 通过dlsym获取原始系统调用函数指针
	// RTLD_NEXT表示在当前库之后搜索符号
#define XX(name) name ## _f = (name ## _fun)dlsym(RTLD_NEXT, #name);
	HOOK_FUN(XX)
#undef XX
}

// 静态初始化器：确保在main函数之前初始化钩子
struct HookIniter
{
	/**
	 * @brief 构造函数，自动初始化钩子系统
	 */
	HookIniter()
	{
		hook_init();
	}
};

// 全局静态对象，在程序启动时自动调用构造函数进行初始化
static HookIniter s_hook_initer;

} // end namespace mycoroutine

/**
 * @brief 定时器信息结构体
 * @details 用于跟踪定时器状态，主要用于IO操作超时控制
 */
struct timer_info 
{
    int cancelled = 0;  // 取消状态，0表示未取消，其他值表示取消原因（如ETIMEDOUT）
};

/**
 * @brief 通用IO操作模板函数
 * @details 处理所有IO相关系统调用的协程调度逻辑
 * @tparam OriginFun 原始系统调用函数类型
 * @tparam Args 可变参数类型
 * @param fd 文件描述符
 * @param fun 原始系统调用函数
 * @param hook_fun_name 钩子函数名称（用于调试）
 * @param event IO事件类型（读/写）
 * @param timeout_so 超时选项类型
 * @param args 传递给原始系统调用的参数
 * @return IO操作的结果
 */
template<typename OriginFun, typename... Args>
static ssize_t do_io(int fd, OriginFun fun, const char* hook_fun_name, uint32_t event, int timeout_so, Args&&... args) 
{
    // 如果钩子未启用，直接调用原始函数
    if(!mycoroutine::t_hook_enable) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }

    // 获取文件描述符上下文
    std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
    if(!ctx) 
    {
        // 如果没有上下文，直接调用原始函数
        return fun(fd, std::forward<Args>(args)...);
    }

    // 检查文件描述符是否已关闭
    if(ctx->isClosed()) 
    {
        errno = EBADF;
        return -1;
    }

    // 非套接字或用户设置为非阻塞，直接调用原始函数
    if(!ctx->isSocket() || ctx->getUserNonblock()) 
    {
        return fun(fd, std::forward<Args>(args)...);
    }

    // 获取超时时间
    uint64_t timeout = ctx->getTimeout(timeout_so);
    // 创建定时器信息
    std::shared_ptr<timer_info> tinfo(new timer_info);

retry:
	// 尝试执行IO操作
    ssize_t n = fun(fd, std::forward<Args>(args)...);
    
    // 如果被信号中断，重新尝试
    while(n == -1 && errno == EINTR) 
    {
        n = fun(fd, std::forward<Args>(args)...);
    }
    
    // 如果资源暂时不可用（阻塞），则进行协程调度
    if(n == -1 && errno == EAGAIN) 
    {
        // 获取当前IO管理器
        mycoroutine::IOManager* iom = mycoroutine::IOManager::GetThis();
        // 定时器指针
        std::shared_ptr<mycoroutine::Timer> timer;
        // 弱引用，用于定时器回调中检查资源是否还存在
        std::weak_ptr<timer_info> winfo(tinfo);

        // 如果设置了超时时间，添加条件定时器
        if(timeout != (uint64_t)-1) 
        {
            timer = iom->addConditionTimer(timeout, [winfo, fd, iom, event]() 
            {
                auto t = winfo.lock();
                if(!t || t->cancelled) 
                {
                    return;
                }
                t->cancelled = ETIMEDOUT;  // 设置超时标志
                // 取消事件并触发一次，使协程恢复执行
                iom->cancelEvent(fd, (mycoroutine::IOManager::Event)(event));
            }, winfo);
        }

        // 添加IO事件，回调为当前协程
        int rt = iom->addEvent(fd, (mycoroutine::IOManager::Event)(event));
        if(rt) 
        {   // 添加事件失败
            std::cout << hook_fun_name << " addEvent("<< fd << ", " << event << ")";
            if(timer) 
            {   // 取消定时器
                timer->cancel();
            }
            return -1;
        } 
        else 
        {   // 添加事件成功，让出协程执行权
            mycoroutine::Fiber::GetThis()->yield();
     
            // 协程恢复，取消定时器
            if(timer) 
            {
                timer->cancel();
            }
            
            // 检查是否超时
            if(tinfo->cancelled == ETIMEDOUT) 
            {
                errno = tinfo->cancelled;
                return -1;
            }
            
            // 重新尝试IO操作
            goto retry;
        }
    }
    
    return n;  // 返回IO操作结果
}




extern "C"{

// declaration -> sleep_fun sleep_f = nullptr;
#define XX(name) name ## _fun name ## _f = nullptr;
	HOOK_FUN(XX)
#undef XX

// only use at task fiber
/**
 * @brief sleep函数钩子实现
 * @details 将阻塞式的sleep转换为非阻塞的协程挂起，避免线程阻塞
 * @param seconds 睡眠秒数
 * @return 0（协程实现中总是返回0）
 */
unsigned int sleep(unsigned int seconds)
{
	// 如果钩子未启用，调用原始函数
	if(!mycoroutine::t_hook_enable)
	{
		return sleep_f(seconds);
	}

	// 获取当前协程对象
	std::shared_ptr<mycoroutine::Fiber> fiber = mycoroutine::Fiber::GetThis();
	// 获取当前IO管理器
	mycoroutine::IOManager* iom = mycoroutine::IOManager::GetThis();
	// 添加定时器，在指定时间后重新调度该协程
	iom->addTimer(seconds*1000, [fiber, iom](){iom->scheduleLock(fiber, -1);});
	// 让出当前协程的执行权，等待定时器唤醒
	fiber->yield();
	return 0;
}

/**
 * @brief usleep函数钩子实现
 * @details 将阻塞式的微秒睡眠转换为非阻塞的协程挂起
 * @param usec 微秒数
 * @return 0（协程实现中总是返回0）
 */
int usleep(useconds_t usec)
{
	// 如果钩子未启用，调用原始函数
	if(!mycoroutine::t_hook_enable)
	{
		return usleep_f(usec);
	}

	// 获取当前协程对象
	std::shared_ptr<mycoroutine::Fiber> fiber = mycoroutine::Fiber::GetThis();
	// 获取当前IO管理器
	mycoroutine::IOManager* iom = mycoroutine::IOManager::GetThis();
	// 添加定时器，在指定时间后重新调度该协程
	iom->addTimer(usec/1000, [fiber, iom](){iom->scheduleLock(fiber);});
	// 让出当前协程的执行权，等待定时器唤醒
	fiber->yield();
	return 0;
}

/**
 * @brief nanosleep函数钩子实现
 * @details 将阻塞式的纳秒睡眠转换为非阻塞的协程挂起
 * @param req 请求睡眠的时间结构
 * @param rem 剩余未睡眠的时间结构（在协程实现中未使用）
 * @return 0（协程实现中总是返回0）
 */
int nanosleep(const struct timespec* req, struct timespec* rem)
{
	// 如果钩子未启用，调用原始函数
	if(!mycoroutine::t_hook_enable)
	{
		return nanosleep_f(req, rem);
	}    

	// 计算超时时间（毫秒）
	int timeout_ms = req->tv_sec*1000 + req->tv_nsec/1000/1000;

	// 获取当前协程对象
	std::shared_ptr<mycoroutine::Fiber> fiber = mycoroutine::Fiber::GetThis();
	// 获取当前IO管理器
	mycoroutine::IOManager* iom = mycoroutine::IOManager::GetThis();
	// 添加定时器，在指定时间后重新调度该协程
	iom->addTimer(timeout_ms, [fiber, iom](){iom->scheduleLock(fiber, -1);});
	// 让出当前协程的执行权，等待定时器唤醒
	fiber->yield();    
	return 0;
}

/**
 * @brief socket函数钩子实现
 * @details 创建socket并管理其上下文
 * @param domain 地址族（如AF_INET、AF_INET6）
 * @param type 套接字类型（如SOCK_STREAM、SOCK_DGRAM）
 * @param protocol 协议类型（通常为0）
 * @return 创建的socket文件描述符
 */
int socket(int domain, int type, int protocol)
{
	// 如果钩子未启用，调用原始函数
	if(!mycoroutine::t_hook_enable)
	{
		return socket_f(domain, type, protocol);
	}

	// 创建socket
	int fd = socket_f(domain, type, protocol);
	if(fd==-1)
	{
		std::cerr << "socket() failed:" << strerror(errno) << std::endl;
		return fd;
	}
	// 获取并初始化文件描述符上下文
	mycoroutine::FdMgr::GetInstance()->get(fd, true);
	return fd;
}

/**
 * @brief 带超时的connect函数实现
 * @details 实现非阻塞的connect操作，并支持超时设置
 * @param fd socket文件描述符
 * @param addr 目标地址结构
 * @param addrlen 地址结构长度
 * @param timeout_ms 超时时间（毫秒）
 * @return 成功返回0，失败返回-1并设置errno
 */
int connect_with_timeout(int fd, const struct sockaddr* addr, socklen_t addrlen, uint64_t timeout_ms) 
{
    // 如果钩子未启用，调用原始函数
    if(!mycoroutine::t_hook_enable) 
    {
        return connect_f(fd, addr, addrlen);
    }

    // 获取文件描述符上下文
    std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
    if(!ctx || ctx->isClosed()) 
    {
        errno = EBADF;
        return -1;
    }

    // 检查是否为socket
    if(!ctx->isSocket()) 
    {
        return connect_f(fd, addr, addrlen);
    }

    // 如果用户设置为非阻塞模式，直接调用原始函数
    if(ctx->getUserNonblock()) 
    {
        return connect_f(fd, addr, addrlen);
    }

    // 尝试连接
    int n = connect_f(fd, addr, addrlen);
    if(n == 0) 
    {   // 连接成功
        return 0;
    } 
    else if(n != -1 || errno != EINPROGRESS) 
    {   // 其他错误情况
        return n;
    }

    // 连接进行中，等待可写事件（表示连接成功或失败）
    mycoroutine::IOManager* iom = mycoroutine::IOManager::GetThis();
    std::shared_ptr<mycoroutine::Timer> timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    // 设置超时定时器
    if(timeout_ms != (uint64_t)-1) 
    {
        timer = iom->addConditionTimer(timeout_ms, [winfo, fd, iom]() 
        {
            auto t = winfo.lock();
            if(!t || t->cancelled) 
            {
                return;
            }
            t->cancelled = ETIMEDOUT;
            iom->cancelEvent(fd, mycoroutine::IOManager::WRITE);
        }, winfo);
    }

    // 添加可写事件
    int rt = iom->addEvent(fd, mycoroutine::IOManager::WRITE);
    if(rt == 0) 
    {   // 事件添加成功，让出协程执行权
        mycoroutine::Fiber::GetThis()->yield();

        // 协程恢复，取消定时器
        if(timer) 
        {
            timer->cancel();
        }

        // 检查是否超时
        if(tinfo->cancelled) 
        {
            errno = tinfo->cancelled;
            return -1;
        }
    } 
    else 
    {   // 事件添加失败
        if(timer) 
        {
            timer->cancel();
        }
        std::cerr << "connect addEvent(" << fd << ", WRITE) error";
    }

    // 检查连接是否成功建立
    int error = 0;
    socklen_t len = sizeof(int);
    if(-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) 
    {
        return -1;
    }
    if(!error) 
    {
        return 0;
    } 
    else 
    {
        errno = error;
        return -1;
    }
}


/**
 * @brief 全局连接超时设置
 */
static uint64_t s_connect_timeout = -1;

/**
 * @brief connect函数钩子实现
 * @details 将阻塞式的connect转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param addr 目标地址结构
 * @param addrlen 地址结构长度
 * @return 成功返回0，失败返回-1并设置errno
 */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
	// 调用带超时的connect函数
	return connect_with_timeout(sockfd, addr, addrlen, s_connect_timeout);
}

/**
 * @brief accept函数钩子实现
 * @details 将阻塞式的accept转换为非阻塞的协程挂起操作
 * @param sockfd 监听socket文件描述符
 * @param addr 客户端地址结构（输出参数）
 * @param addrlen 地址结构长度（输入/输出参数）
 * @return 成功返回新的连接socket文件描述符，失败返回-1并设置errno
 */
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
	// 使用通用IO操作模板函数处理accept
	int fd = do_io(sockfd, accept_f, "accept", mycoroutine::IOManager::READ, SO_RCVTIMEO, addr, addrlen);
	if(fd>=0)
	{
		// 为新接受的连接创建文件描述符上下文
		mycoroutine::FdMgr::GetInstance()->get(fd, true);
	}
	return fd;
}

/**
 * @brief read函数钩子实现
 * @details 将阻塞式的read转换为非阻塞的协程挂起操作
 * @param fd 文件描述符
 * @param buf 接收缓冲区
 * @param count 要读取的字节数
 * @return 成功返回读取的字节数，失败返回-1并设置errno
 */
ssize_t read(int fd, void *buf, size_t count)
{
	return do_io(fd, read_f, "read", mycoroutine::IOManager::READ, SO_RCVTIMEO, buf, count);	
}

/**
 * @brief readv函数钩子实现
 * @details 将阻塞式的readv（分散读取）转换为非阻塞的协程挂起操作
 * @param fd 文件描述符
 * @param iov 缓冲区数组
 * @param iovcnt 缓冲区数量
 * @return 成功返回读取的字节数，失败返回-1并设置errno
 */
ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
	return do_io(fd, readv_f, "readv", mycoroutine::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);	
}

/**
 * @brief recv函数钩子实现
 * @details 将阻塞式的recv转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param buf 接收缓冲区
 * @param len 缓冲区长度
 * @param flags 控制标志
 * @return 成功返回接收的字节数，失败返回-1并设置errno
 */
ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
	return do_io(sockfd, recv_f, "recv", mycoroutine::IOManager::READ, SO_RCVTIMEO, buf, len, flags);	
}

/**
 * @brief recvfrom函数钩子实现
 * @details 将阻塞式的recvfrom转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param buf 接收缓冲区
 * @param len 缓冲区长度
 * @param flags 控制标志
 * @param src_addr 源地址（输出参数）
 * @param addrlen 地址长度（输入/输出参数）
 * @return 成功返回接收的字节数，失败返回-1并设置errno
 */
ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
	return do_io(sockfd, recvfrom_f, "recvfrom", mycoroutine::IOManager::READ, SO_RCVTIMEO, buf, len, flags, src_addr, addrlen);	
}

/**
 * @brief recvmsg函数钩子实现
 * @details 将阻塞式的recvmsg转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param msg 消息头结构
 * @param flags 控制标志
 * @return 成功返回接收的字节数，失败返回-1并设置errno
 */
ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
	return do_io(sockfd, recvmsg_f, "recvmsg", mycoroutine::IOManager::READ, SO_RCVTIMEO, msg, flags);	
}

/**
 * @brief write函数钩子实现
 * @details 将阻塞式的write转换为非阻塞的协程挂起操作
 * @param fd 文件描述符
 * @param buf 发送缓冲区
 * @param count 要写入的字节数
 * @return 成功返回写入的字节数，失败返回-1并设置errno
 */
ssize_t write(int fd, const void *buf, size_t count)
{
	return do_io(fd, write_f, "write", mycoroutine::IOManager::WRITE, SO_SNDTIMEO, buf, count);	
}

/**
 * @brief writev函数钩子实现
 * @details 将阻塞式的writev（集中写入）转换为非阻塞的协程挂起操作
 * @param fd 文件描述符
 * @param iov 缓冲区数组
 * @param iovcnt 缓冲区数量
 * @return 成功返回写入的字节数，失败返回-1并设置errno
 */
ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
	return do_io(fd, writev_f, "writev", mycoroutine::IOManager::WRITE, SO_SNDTIMEO, iov, iovcnt);	
}

/**
 * @brief send函数钩子实现
 * @details 将阻塞式的send转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param buf 发送缓冲区
 * @param len 缓冲区长度
 * @param flags 控制标志
 * @return 成功返回发送的字节数，失败返回-1并设置errno
 */
ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
	return do_io(sockfd, send_f, "send", mycoroutine::IOManager::WRITE, SO_SNDTIMEO, buf, len, flags);	
}

/**
 * @brief sendto函数钩子实现
 * @details 将阻塞式的sendto转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param buf 发送缓冲区
 * @param len 缓冲区长度
 * @param flags 控制标志
 * @param dest_addr 目标地址
 * @param addrlen 地址长度
 * @return 成功返回发送的字节数，失败返回-1并设置errno
 */
ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
	return do_io(sockfd, sendto_f, "sendto", mycoroutine::IOManager::WRITE, SO_SNDTIMEO, buf, len, flags, dest_addr, addrlen);	
}

/**
 * @brief sendmsg函数钩子实现
 * @details 将阻塞式的sendmsg转换为非阻塞的协程挂起操作
 * @param sockfd socket文件描述符
 * @param msg 消息头结构
 * @param flags 控制标志
 * @return 成功返回发送的字节数，失败返回-1并设置errno
 */
ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	return do_io(sockfd, sendmsg_f, "sendmsg", mycoroutine::IOManager::WRITE, SO_SNDTIMEO, msg, flags);	
}

/**
 * @brief close系统调用钩子函数
 * @details 用于关闭文件描述符，在协程环境中自动处理相关的IO事件取消和文件描述符上下文清理
 * @param fd 需要关闭的文件描述符
 * @return 成功返回0，失败返回-1并设置errno
 */
int close(int fd)
{
	// 检查钩子是否启用，如果未启用则直接调用原始close函数
	if(!mycoroutine::t_hook_enable)
	{
		return close_f(fd);
	}

	// 获取文件描述符上下文
	std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(fd);

	if(ctx)
	{
		// 获取当前IO管理器
		auto iom = mycoroutine::IOManager::GetThis();
		if(iom)
		{
			// 取消与该文件描述符相关的所有IO事件
			iom->cancelAll(fd);
		}
		// 从文件描述符管理器中删除该文件描述符的上下文
		mycoroutine::FdMgr::GetInstance()->del(fd);
	}
	// 调用原始close函数关闭文件描述符
	return close_f(fd);
}

/**
 * @brief fcntl系统调用钩子函数
 * @details 文件控制操作的钩子实现，特别处理了非阻塞标志的设置和获取，以支持协程化IO
 * @param fd 目标文件描述符
 * @param cmd 控制命令
 * @param ... 可变参数，根据cmd不同而不同
 * @return 成功返回操作相关的值，失败返回-1并设置errno
 */
int fcntl(int fd, int cmd, ... /* arg */ )
{
  	va_list va; // 用于访问可变参数列表

    va_start(va, cmd);
    switch(cmd) 
    {
        case F_SETFL:  // 设置文件状态标志
            {
                int arg = va_arg(va, int); // 获取下一个int类型参数
                va_end(va);
                // 获取文件描述符上下文
                std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
                // 如果上下文不存在、文件已关闭或不是套接字，则直接调用原始fcntl
                if(!ctx || ctx->isClosed() || !ctx->isSocket()) 
                {
                    return fcntl_f(fd, cmd, arg);
                }
                // 保存用户设置的非阻塞标志
                ctx->setUserNonblock(arg & O_NONBLOCK);
                // 实际的非阻塞状态由系统设置决定
                if(ctx->getSysNonblock()) 
                {
                    arg |= O_NONBLOCK; // 如果系统设置为非阻塞，则添加非阻塞标志
                } 
                else 
                {
                    arg &= ~O_NONBLOCK; // 否则移除非阻塞标志
                }
                return fcntl_f(fd, cmd, arg); // 调用原始函数应用设置
            }
            break;

        case F_GETFL:  // 获取文件状态标志
            {
                va_end(va);
                int arg = fcntl_f(fd, cmd); // 调用原始函数获取标志
                // 获取文件描述符上下文
                std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
                // 如果上下文不存在、文件已关闭或不是套接字，直接返回原始结果
                if(!ctx || ctx->isClosed() || !ctx->isSocket()) 
                {
                    return arg;
                }
                // 向用户返回的是非阻塞标志应根据用户设置的值，而不是系统实际值
                // 这样可以保持对用户的透明性，底层系统设置由库自行管理
                if(ctx->getUserNonblock()) 
                {
                    return arg | O_NONBLOCK;
                } else 
                {
                    return arg & ~O_NONBLOCK;
                }
            }
            break;

        // 以下是不需要特殊处理的fcntl命令，直接调用原始函数
        case F_DUPFD:        // 复制文件描述符
        case F_DUPFD_CLOEXEC:// 复制文件描述符并设置CLOEXEC标志
        case F_SETFD:        // 设置文件描述符标志
        case F_SETOWN:       // 设置所有者
        case F_SETSIG:       // 设置信号
        case F_SETLEASE:     // 设置租约
        case F_NOTIFY:       // 通知事件
#ifdef F_SETPIPE_SZ       // 设置管道大小（条件编译）
        case F_SETPIPE_SZ:
#endif
            {
                int arg = va_arg(va, int);
                va_end(va);
                return fcntl_f(fd, cmd, arg); 
            }
            break;

        case F_GETFD:        // 获取文件描述符标志
        case F_GETOWN:       // 获取所有者
        case F_GETSIG:       // 获取信号
        case F_GETLEASE:     // 获取租约
#ifdef F_GETPIPE_SZ       // 获取管道大小（条件编译）
        case F_GETPIPE_SZ:
#endif
            {
                va_end(va);
                return fcntl_f(fd, cmd);
            }
            break;

        case F_SETLK:        // 设置记录锁（非阻塞）
        case F_SETLKW:       // 设置记录锁（阻塞）
        case F_GETLK:        // 获取记录锁
            {
                struct flock* arg = va_arg(va, struct flock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;

        case F_GETOWN_EX:    // 获取扩展所有者信息
        case F_SETOWN_EX:    // 设置扩展所有者信息
            {
                struct f_owner_exlock* arg = va_arg(va, struct f_owner_exlock*);
                va_end(va);
                return fcntl_f(fd, cmd, arg);
            }
            break;

        default:  // 默认情况下，所有其他命令直接调用原始函数
            va_end(va);
            return fcntl_f(fd, cmd);
    }
}

/**
 * @brief ioctl系统调用钩子函数
 * @details IO控制操作的钩子实现，特别处理了非阻塞模式的设置
 * @param fd 目标文件描述符
 * @param request IO控制请求码
 * @param ... 可变参数，通常是一个指针
 * @return 成功返回操作相关的值，失败返回-1并设置errno
 */
int ioctl(int fd, unsigned long request, ...)
{
    va_list va;
    va_start(va, request);
    void* arg = va_arg(va, void*); // 获取void*类型的参数
    va_end(va);

    // 特别处理FIONBIO请求（设置非阻塞模式）
    if(FIONBIO == request) 
    {
        // 将参数转换为int并检查其真值（0为假，非0为真）
        bool user_nonblock = !!*(int*)arg;
        // 获取文件描述符上下文
        std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(fd);
        // 如果上下文不存在、文件已关闭或不是套接字，直接调用原始ioctl
        if(!ctx || ctx->isClosed() || !ctx->isSocket()) 
        {
            return ioctl_f(fd, request, arg);
        }
        // 更新用户设置的非阻塞标志
        ctx->setUserNonblock(user_nonblock);
    }
    // 无论如何都调用原始ioctl函数执行实际操作
    return ioctl_f(fd, request, arg);
}

/**
 * @brief getsockopt系统调用钩子函数
 * @details 获取套接字选项，这里直接使用原始函数，因为不需要特殊处理
 * @param sockfd 套接字描述符
 * @param level 选项级别（如SOL_SOCKET）
 * @param optname 选项名称
 * @param optval 用于存储选项值的缓冲区
 * @param optlen 选项值缓冲区的大小
 * @return 成功返回0，失败返回-1并设置errno
 */
int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
	return getsockopt_f(sockfd, level, optname, optval, optlen);
}

/**
 * @brief setsockopt系统调用钩子函数
 * @details 设置套接字选项，特别处理了接收和发送超时选项，以支持协程化的超时处理
 * @param sockfd 套接字描述符
 * @param level 选项级别（如SOL_SOCKET）
 * @param optname 选项名称
 * @param optval 选项值的缓冲区
 * @param optlen 选项值缓冲区的大小
 * @return 成功返回0，失败返回-1并设置errno
 */
int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    // 检查钩子是否启用，如果未启用则直接调用原始函数
    if(!mycoroutine::t_hook_enable) 
    {
        return setsockopt_f(sockfd, level, optname, optval, optlen);
    }

    // 特别处理SOL_SOCKET级别的超时选项
    if(level == SOL_SOCKET) 
    {
        // 处理接收超时和发送超时
        if(optname == SO_RCVTIMEO || optname == SO_SNDTIMEO) 
        {
            // 获取文件描述符上下文
            std::shared_ptr<mycoroutine::FdCtx> ctx = mycoroutine::FdMgr::GetInstance()->get(sockfd);
            if(ctx) 
            {
                // 将timeval格式转换为毫秒并设置超时
                const timeval* v = (const timeval*)optval;
                ctx->setTimeout(optname, v->tv_sec * 1000 + v->tv_usec / 1000);
            }
        }
    }
    // 调用原始函数应用设置
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

}