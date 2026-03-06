/**
 * @file hook.h
 * @brief 系统调用钩子头文件
 * @details 定义了需要被hook的系统调用函数指针类型和函数声明，用于实现协程化的系统调用
 */
#ifndef __MYCOROUTINE_HOOK_H_
#define __MYCOROUTINE_HOOK_H_

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>          
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

namespace mycoroutine{

/**
 * @brief 检查钩子是否启用
 * @return 钩子启用状态，true为启用，false为禁用
 */
bool is_hook_enable();

/**
 * @brief 设置钩子启用状态
 * @param flag 启用标志，true为启用，false为禁用
 */
void set_hook_enable(bool flag);

}

// 使用C链接，确保函数名不被C++编译器修饰
extern "C"
{
	/**
	 * @brief 原始系统调用函数指针定义
	 * @details 用于保存原始的系统调用函数，当钩子禁用时调用
	 */
	
	/**
	 * @brief sleep函数指针类型
	 */
	typedef unsigned int (*sleep_fun) (unsigned int seconds);
	/**
	 * @brief 原始sleep函数指针
	 */
	extern sleep_fun sleep_f;

	/**
	 * @brief usleep函数指针类型
	 */
	typedef int (*usleep_fun) (useconds_t usec);
	/**
	 * @brief 原始usleep函数指针
	 */
	extern usleep_fun usleep_f;

	/**
	 * @brief nanosleep函数指针类型
	 */
	typedef int (*nanosleep_fun) (const struct timespec* req, struct timespec* rem);
	/**
	 * @brief 原始nanosleep函数指针
	 */
	extern nanosleep_fun nanosleep_f;    

	/**
	 * @brief socket函数指针类型
	 */
	typedef int (*socket_fun) (int domain, int type, int protocol);
	/**
	 * @brief 原始socket函数指针
	 */
	extern socket_fun socket_f;

	/**
	 * @brief connect函数指针类型
	 */
	typedef int (*connect_fun) (int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	/**
	 * @brief 原始connect函数指针
	 */
	extern connect_fun connect_f;

	/**
	 * @brief accept函数指针类型
	 */
	typedef int (*accept_fun) (int sockfd, struct sockaddr *addr, socklen_t *addrlen);
	/**
	 * @brief 原始accept函数指针
	 */
	extern accept_fun accept_f;

	/**
	 * @brief read函数指针类型
	 */
	typedef ssize_t (*read_fun) (int fd, void *buf, size_t count);
	/**
	 * @brief 原始read函数指针
	 */
	extern read_fun read_f;

	/**
	 * @brief readv函数指针类型
	 */
	typedef ssize_t (*readv_fun)(int fd, const struct iovec *iov, int iovcnt);
	/**
	 * @brief 原始readv函数指针
	 */
	extern readv_fun readv_f;

	/**
	 * @brief recv函数指针类型
	 */
	typedef ssize_t (*recv_fun) (int sockfd, void *buf, size_t len, int flags);
	/**
	 * @brief 原始recv函数指针
	 */
	extern recv_fun recv_f;

	/**
	 * @brief recvfrom函数指针类型
	 */
	typedef ssize_t (*recvfrom_fun) (int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
	/**
	 * @brief 原始recvfrom函数指针
	 */
	extern recvfrom_fun recvfrom_f;

	/**
	 * @brief recvmsg函数指针类型
	 */
	typedef ssize_t (*recvmsg_fun) (int sockfd, struct msghdr *msg, int flags);
	/**
	 * @brief 原始recvmsg函数指针
	 */
	extern recvmsg_fun recvmsg_f;

	/**
	 * @brief write函数指针类型
	 */
	typedef ssize_t (*write_fun) (int fd, const void *buf, size_t count);
	/**
	 * @brief 原始write函数指针
	 */
	extern write_fun write_f;

	/**
	 * @brief writev函数指针类型
	 */
	typedef ssize_t (*writev_fun) (int fd, const struct iovec *iov, int iovcnt);
	/**
	 * @brief 原始writev函数指针
	 */
	extern writev_fun writev_f;

	/**
	 * @brief send函数指针类型
	 */
	typedef ssize_t (*send_fun) (int sockfd, const void *buf, size_t len, int flags);
	/**
	 * @brief 原始send函数指针
	 */
	extern send_fun send_f;

	/**
	 * @brief sendto函数指针类型
	 */
	typedef ssize_t (*sendto_fun) (int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
	/**
	 * @brief 原始sendto函数指针
	 */
	extern sendto_fun sendto_f;

	/**
	 * @brief sendmsg函数指针类型
	 */
	typedef ssize_t (*sendmsg_fun) (int sockfd, const struct msghdr *msg, int flags);
	/**
	 * @brief 原始sendmsg函数指针
	 */
	extern sendmsg_fun sendmsg_f;

	/**
	 * @brief close函数指针类型
	 */
	typedef int (*close_fun) (int fd);
	/**
	 * @brief 原始close函数指针
	 */
	extern close_fun close_f;

	/**
	 * @brief fcntl函数指针类型
	 */
	typedef int (*fcntl_fun) (int fd, int cmd, ... /* arg */ );
	/**
	 * @brief 原始fcntl函数指针
	 */
	extern fcntl_fun fcntl_f;

	/**
	 * @brief ioctl函数指针类型
	 */
	typedef int (*ioctl_fun) (int fd, unsigned long request, ...);
	/**
	 * @brief 原始ioctl函数指针
	 */
	extern ioctl_fun ioctl_f;

	/**
	 * @brief getsockopt函数指针类型
	 */
	typedef int (*getsockopt_fun) (int sockfd, int level, int optname, void *optval, socklen_t *optlen);
    /**
	 * @brief 原始getsockopt函数指针
	 */
    extern getsockopt_fun getsockopt_f;

    /**
	 * @brief setsockopt函数指针类型
	 */
    typedef int (*setsockopt_fun) (int sockfd, int level, int optname, const void *optval, socklen_t optlen);
    /**
	 * @brief 原始setsockopt函数指针
	 */
    extern setsockopt_fun setsockopt_f;

    /**
	 * @brief 系统调用钩子函数声明
	 * @details 这些函数会替代系统原始函数，在启用钩子时被调用
	 */
	
	/**
	 * @brief 睡眠函数钩子
	 * @details 将阻塞式的sleep转换为非阻塞的协程挂起
	 * @param seconds 睡眠秒数
	 * @return 0（协程实现中总是返回0）
	 */
	unsigned int sleep(unsigned int seconds);
	
	/**
	 * @brief 微秒睡眠函数钩子
	 * @details 将阻塞式的usleep转换为非阻塞的协程挂起
	 * @param usec 微秒数
	 * @return 0（协程实现中总是返回0）
	 */
	int usleep(useconds_t usce);
	
	/**
	 * @brief 纳秒睡眠函数钩子
	 * @details 将阻塞式的nanosleep转换为非阻塞的协程挂起
	 * @param req 请求睡眠的时间结构
	 * @param rem 剩余未睡眠的时间结构（在协程实现中未使用）
	 * @return 0（协程实现中总是返回0）
	 */
	int nanosleep(const struct timespec* req, struct timespec* rem);

	/**
	 * @brief 网络相关函数钩子
	 */
	
	/**
	 * @brief socket函数钩子
	 * @details 创建socket并管理其上下文
	 * @param domain 地址族（如AF_INET、AF_INET6）
	 * @param type 套接字类型（如SOCK_STREAM、SOCK_DGRAM）
	 * @param protocol 协议类型（通常为0）
	 * @return 创建的socket文件描述符
	 */
	int socket(int domain, int type, int protocol);
	
	/**
	 * @brief connect函数钩子
	 * @details 将阻塞式的connect转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param addr 目标地址结构
	 * @param addrlen 地址结构长度
	 * @return 成功返回0，失败返回-1并设置errno
	 */
	int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
	
	/**
	 * @brief accept函数钩子
	 * @details 将阻塞式的accept转换为非阻塞的协程挂起操作
	 * @param sockfd 监听socket文件描述符
	 * @param addr 客户端地址结构（输出参数）
	 * @param addrlen 地址结构长度（输入/输出参数）
	 * @return 成功返回新的连接socket文件描述符，失败返回-1并设置errno
	 */
	int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

	/**
	 * @brief 读取相关函数钩子
	 */
	
	/**
	 * @brief read函数钩子
	 * @details 将阻塞式的read转换为非阻塞的协程挂起操作
	 * @param fd 文件描述符
	 * @param buf 接收缓冲区
	 * @param count 要读取的字节数
	 * @return 成功返回读取的字节数，失败返回-1并设置errno
	 */
	ssize_t read(int fd, void *buf, size_t count);
	
	/**
	 * @brief readv函数钩子
	 * @details 将阻塞式的readv（分散读取）转换为非阻塞的协程挂起操作
	 * @param fd 文件描述符
	 * @param iov 缓冲区数组
	 * @param iovcnt 缓冲区数量
	 * @return 成功返回读取的字节数，失败返回-1并设置errno
	 */
	ssize_t readv(int fd, const struct iovec *iov, int iovcnt);

	/**
	 * @brief recv函数钩子
	 * @details 将阻塞式的recv转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param buf 接收缓冲区
	 * @param len 缓冲区长度
	 * @param flags 控制标志
	 * @return 成功返回接收的字节数，失败返回-1并设置errno
	 */
    ssize_t recv(int sockfd, void *buf, size_t len, int flags);
    
	/**
	 * @brief recvfrom函数钩子
	 * @details 将阻塞式的recvfrom转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param buf 接收缓冲区
	 * @param len 缓冲区长度
	 * @param flags 控制标志
	 * @param src_addr 源地址（输出参数）
	 * @param addrlen 地址长度（输入/输出参数）
	 * @return 成功返回接收的字节数，失败返回-1并设置errno
	 */
    ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
    
	/**
	 * @brief recvmsg函数钩子
	 * @details 将阻塞式的recvmsg转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param msg 消息头结构
	 * @param flags 控制标志
	 * @return 成功返回接收的字节数，失败返回-1并设置errno
	 */
    ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);

    /**
	 * @brief 写入相关函数钩子
	 */
	
	/**
	 * @brief write函数钩子
	 * @details 将阻塞式的write转换为非阻塞的协程挂起操作
	 * @param fd 文件描述符
	 * @param buf 发送缓冲区
	 * @param count 要写入的字节数
	 * @return 成功返回写入的字节数，失败返回-1并设置errno
	 */
    ssize_t write(int fd, const void *buf, size_t count);
    
	/**
	 * @brief writev函数钩子
	 * @details 将阻塞式的writev（集中写入）转换为非阻塞的协程挂起操作
	 * @param fd 文件描述符
	 * @param iov 缓冲区数组
	 * @param iovcnt 缓冲区数量
	 * @return 成功返回写入的字节数，失败返回-1并设置errno
	 */
    ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

	/**
	 * @brief send函数钩子
	 * @details 将阻塞式的send转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param buf 发送缓冲区
	 * @param len 缓冲区长度
	 * @param flags 控制标志
	 * @return 成功返回发送的字节数，失败返回-1并设置errno
	 */
    ssize_t send(int sockfd, const void *buf, size_t len, int flags);
    
	/**
	 * @brief sendto函数钩子
	 * @details 将阻塞式的sendto转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param buf 发送缓冲区
	 * @param len 缓冲区长度
	 * @param flags 控制标志
	 * @param dest_addr 目标地址
	 * @param addrlen 地址长度
	 * @return 成功返回发送的字节数，失败返回-1并设置errno
	 */
    ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
    
	/**
	 * @brief sendmsg函数钩子
	 * @details 将阻塞式的sendmsg转换为非阻塞的协程挂起操作
	 * @param sockfd socket文件描述符
	 * @param msg 消息头结构
	 * @param flags 控制标志
	 * @return 成功返回发送的字节数，失败返回-1并设置errno
	 */
    ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);

    /**
	 * @brief 文件描述符相关函数钩子
	 */
	
	/**
	 * @brief close函数钩子
	 * @details 关闭文件描述符并清理相关资源
	 * @param fd 文件描述符
	 * @return 成功返回0，失败返回-1并设置errno
	 */
    int close(int fd);

    /**
	 * @brief 文件控制相关函数钩子
	 */
	
	/**
	 * @brief fcntl函数钩子
	 * @details 处理文件描述符控制操作，特别是非阻塞模式设置
	 * @param fd 文件描述符
	 * @param cmd 操作命令
	 * @param ... 可变参数
	 * @return 根据cmd不同返回不同的值
	 */
    int fcntl(int fd, int cmd, ... /* arg */ );
    
	/**
	 * @brief ioctl函数钩子
	 * @details 处理IO控制操作，特别是FIONBIO请求
	 * @param fd 文件描述符
	 * @param request 请求类型
	 * @param ... 可变参数
	 * @return 成功返回0，失败返回-1并设置errno
	 */
    int ioctl(int fd, unsigned long request, ...);

    /**
	 * @brief 套接字选项相关函数钩子
	 */
	
	/**
	 * @brief getsockopt函数钩子
	 * @details 获取套接字选项
	 * @param sockfd socket文件描述符
	 * @param level 协议级别
	 * @param optname 选项名
	 * @param optval 选项值（输出参数）
	 * @param optlen 选项值长度（输入/输出参数）
	 * @return 成功返回0，失败返回-1并设置errno
	 */
    int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
    
	/**
	 * @brief setsockopt函数钩子
	 * @details 设置套接字选项
	 * @param sockfd socket文件描述符
	 * @param level 协议级别
	 * @param optname 选项名
	 * @param optval 选项值
	 * @param optlen 选项值长度
	 * @return 成功返回0，失败返回-1并设置errno
	 */
    int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
}

#endif