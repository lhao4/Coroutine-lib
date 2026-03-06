/**
 * @file coroutine_http_server.cpp
 * @brief 协程化IO事件管理器示例程序
 * @details 演示如何使用协程和IO事件管理器实现非阻塞的HTTP服务器
 */

#include "mycoroutine/iomanager.h"   // IO事件管理器头文件
#include "mycoroutine/hook.h"          // 系统调用钩子头文件
#include <unistd.h>         // UNIX标准函数库
#include <sys/types.h>      // 系统数据类型定义
#include <sys/socket.h>     // 套接字API
#include <arpa/inet.h>      // 网络地址转换函数
#include <fcntl.h>          // 文件控制函数
#include <iostream>         // 标准输入输出
#include <stack>
#include <cstring>          // 字符串处理函数
#include <chrono>
#include <thread>

/**
 * @brief 监听套接字文件描述符
 * @details 全局变量，用于存储服务器监听套接字的文件描述符
 */
static int sock_listen_fd = -1;

/**
 * @brief 函数声明
 */
void test_accept();

/**
 * @brief 错误处理函数
 * @details 打印错误信息并退出程序
 * @param msg 错误描述信息
 */
void error(const char *msg)
{
    perror(msg);           // 打印系统错误信息
    printf("erreur...\n");  // 打印自定义错误提示
    exit(1);               // 退出程序，返回错误码1
}

/**
 * @brief 监听IO读事件
 * @details 为监听套接字添加读事件回调
 */
void watch_io_read()
{
    // 获取当前IO管理器实例，并为监听套接字添加读事件，回调函数为test_accept
    mycoroutine::IOManager::GetThis()->addEvent(sock_listen_fd, mycoroutine::IOManager::READ, test_accept);
}

/**
 * @brief 处理接受连接的回调函数
 * @details 接受新的客户端连接并处理HTTP请求
 */
void test_accept()
{
    struct sockaddr_in addr;      // 客户端地址结构体
    memset(&addr, 0, sizeof(addr)); // 清零初始化
    socklen_t len = sizeof(addr);    // 地址长度
    
    // 接受新连接（这里会被hook，变为非阻塞协程挂起操作）
    int fd = accept(sock_listen_fd, (struct sockaddr *)&addr, &len);
    
    if (fd < 0)
    {
        // 连接失败，忽略错误（已注释掉调试输出）
        //std::cout << "accept failed, fd = " << fd << ", errno = " << errno << std::endl;
    }
    else
    {
        // 连接成功
        std::cout << "accepted connection, fd = " << fd << std::endl;
        
        // 设置为非阻塞模式
        fcntl(fd, F_SETFL, O_NONBLOCK);
        
        // 为新连接添加读事件回调
        mycoroutine::IOManager::GetThis()->addEvent(fd, mycoroutine::IOManager::READ, [fd]()
        {
            char buffer[1024];         // 接收缓冲区
            memset(buffer, 0, sizeof(buffer)); // 清零初始化
            
            while (true)
            {
                // 接收数据（这里会被hook，变为非阻塞协程挂起操作）
                int ret = recv(fd, buffer, sizeof(buffer), 0);
                
                if (ret > 0)
                {
                    // 收到数据
                    //std::cout << "received data, fd = " << fd << ", data = " << buffer << std::endl;
                    
                    // 构建HTTP响应
                    const char *response = "HTTP/1.1 200 OK\r\n"
                                           "Content-Type: text/plain\r\n"
                                           "Content-Length: 13\r\n"
                                           "Connection: keep-alive\r\n"
                                           "\r\n"
                                           "Hello, World!";
                    
                    // 发送HTTP响应（这里会被hook，变为非阻塞协程挂起操作）
                    ret = send(fd, response, strlen(response), 0);
                    //std::cout << "sent data, fd = " << fd << ", ret = " << ret << std::endl;

                    // 关闭连接
                    close(fd);
                    break;
                }
                
                if (ret <= 0)
                {
                    // 连接关闭或发生错误
                    if (ret == 0 || errno != EAGAIN)
                    {
                        // 连接被客户端关闭或发生非临时性错误
                        //std::cout << "closing connection, fd = " << fd << std::endl;
                        close(fd);
                        break;
                    }
                    else if (errno == EAGAIN)
                    {
                        // 资源暂时不可用，非阻塞模式下的正常返回
                        //std::cout << "recv returned EAGAIN, fd = " << fd << std::endl;
                        //std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 注释掉，因为有IO事件机制
                    }
                }
            }
        });
    }
    
    // 重新为监听套接字添加读事件，继续接受新连接
    mycoroutine::IOManager::GetThis()->addEvent(sock_listen_fd, mycoroutine::IOManager::READ, test_accept);
}

/**
 * @brief 测试IO事件管理器
 * @details 创建并配置HTTP服务器，使用IO事件管理器处理连接
 */
void test_iomanager()
{
    int portno = 8080;                       // 服务器监听端口
    struct sockaddr_in server_addr, client_addr; // 服务器和客户端地址结构体
    (void)client_addr;  // 客户端地址结构体，用于消除未使用变量警告

    // 创建TCP套接字（这里会被hook）
    sock_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_listen_fd < 0)
    {
        error("Error creating socket..\n");
    }

    int yes = 1;
    // 设置SO_REUSEADDR选项，解决"address already in use"错误
    setsockopt(sock_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 初始化服务器地址结构体
    memset((char *)&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // IPv4地址族
    server_addr.sin_port = htons(portno);      // 转换端口号为网络字节序
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网络接口

    // 绑定套接字到地址和端口
    if (bind(sock_listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
        error("Error binding socket..\n");

    // 开始监听，backlog为1024
    if (listen(sock_listen_fd, 1024) < 0)
    {
        error("Error listening..\n");
    }

    // 打印服务器启动信息
    printf("epoll echo server listening for connections on port: %d\n", portno);
    
    // 设置套接字为非阻塞模式
    fcntl(sock_listen_fd, F_SETFL, O_NONBLOCK);
    
    // 创建IO事件管理器，工作线程数为9
    mycoroutine::IOManager iom(9);
    
    // 为监听套接字添加读事件，回调函数为test_accept
    iom.addEvent(sock_listen_fd, mycoroutine::IOManager::READ, test_accept);
    
    // IO管理器会在此阻塞，直到所有事件处理完毕或被手动停止
}

/**
 * @brief 程序入口函数
 * @return 程序退出状态码
 */
int main()
{
    // 调用测试函数，启动HTTP服务器
    test_iomanager();
    return 0;  // 正常退出
}
