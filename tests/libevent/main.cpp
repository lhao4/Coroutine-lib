// =============================================================================
// 文件: main.cpp
// =============================================================================
// 项目: 协程库示例程序
// 模块: libevent HTTP服务器
// 功能: 使用libevent库实现的高并发非阻塞HTTP服务器示例
//      展示如何利用libevent的事件驱动模型实现高性能I/O操作
// 原理: libevent库封装了底层的事件多路分发机制（如epoll、select、poll等）
//       实现了统一的事件驱动接口，使开发者可以专注于业务逻辑而非底层细节
// -----------------------------------------------------------------------------
// 文件名: main.cpp
// 描述: 使用libevent库实现的高并发非阻塞HTTP服务器示例
// 功能: 展示如何使用libevent库实现事件驱动的非阻塞I/O服务器
//      libevent是一个跨平台的事件处理库，封装了底层的事件多路分发机制（如epoll、select等）
// 作者: Auto-generated
// 创建日期: 2024
// -----------------------------------------------------------------------------

// 标准库头文件
#include <stdio.h>      // 标准输入输出函数
#include <stdlib.h>     // 标准库函数
#include <string.h>     // 字符串处理函数
#include <unistd.h>     // Unix标准函数(close等)

// 网络编程相关头文件
#include <sys/socket.h> // 套接字API
#include <netinet/in.h> // 网络地址结构定义
#include <arpa/inet.h>  // 地址转换函数

// libevent库头文件
#include <event2/event.h>      // 基本事件处理功能
#include <event2/listener.h>   // 监听器相关功能
#include <event2/bufferevent.h>// 缓冲事件相关功能

// 宏定义
#define PORT 8080  // 服务器监听端口号

/**
 * @brief HTTP请求读取回调函数
 * @details 当客户端连接套接字上有可读事件发生时，libevent会调用此函数处理HTTP请求
 * @param fd 客户端连接的文件描述符
 * @param events 发生的事件类型
 * @param arg 用户自定义参数，这里是event结构体指针
 */
void http_read_cb(evutil_socket_t fd, short events, void *arg) {
    // =========================================================================
    // HTTP请求处理函数核心实现
    // =========================================================================
    char buf[1024];  // 接收缓冲区，大小为1KB，足够处理基本HTTP请求
    
    // 从客户端套接字接收数据
    // - 使用非阻塞方式接收，但由于libevent事件驱动机制，此处调用时数据已就绪
    // - 接收缓冲区预留1字节用于null终止符
    int len = recv(fd, buf, sizeof(buf) - 1, 0);
    
    // 判断接收结果，处理异常情况
    if (len <= 0) {
        // len == 0 表示客户端正常关闭连接，len < 0 表示发生错误
        if (len < 0) {
            // 通常情况下libevent会避免EAGAIN等错误，但仍需处理其他可能的错误
            perror("recv");
        }
        close(fd);                      // 关闭文件描述符，释放系统资源
        event_free((struct event *)arg); // 释放事件资源，避免内存泄漏
        return;
    }
    
    // 确保字符串以null结尾，防止打印时出现越界
    buf[len] = '\0';
    printf("接收到消息：%s\n", buf);  // 打印接收到的HTTP请求内容，用于调试

    // 构建标准HTTP 1.1响应消息
    // - HTTP/1.1 200 OK: 表示请求成功
    // - Content-Type: 指定响应内容类型为纯文本
    // - Content-Length: 指定响应体长度
    // - Connection: keep-alive: 表明连接可以复用
    const char *response = "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/plain\r\n"
                          "Content-Length: 13\r\n"
                          "Connection: keep-alive\r\n"
                          "\r\n"
                          "Hello, World!";
    
    // 发送HTTP响应给客户端
    // - 在非阻塞模式下，可能需要处理部分发送的情况
    // - 此处简化处理，直接发送完整响应
    send(fd, response, strlen(response), 0);

    // 发送响应后关闭连接并释放相关资源
    // 注意：在实际生产环境中，可能需要支持长连接，此处为了演示简单性而选择关闭
    close(fd);                      // 关闭客户端连接文件描述符
    event_free((struct event *)arg); // 释放与该连接相关的事件资源
}

/**
 * @brief 接受连接回调函数
 * @details 当监听套接字上有新连接到来时，libevent会调用此函数处理新连接
 * @param listener 监听套接字的文件描述符
 * @param event 发生的事件类型
 * @param arg 用户自定义参数，这里是event_base指针
 */
void accept_conn_cb(evutil_socket_t listener, short event, void *arg) {
    // =========================================================================
    // 连接接受回调函数实现
    // =========================================================================
    // 从参数获取事件基础（事件循环）实例
    // - event_base是libevent的核心，负责事件的调度和分发
    struct event_base *base = (struct event_base *)arg;
    
    // 客户端地址结构体
    // - 使用sockaddr_storage可兼容IPv4和IPv6地址
    // - slen初始化为结构体大小，将在accept调用后被修改为实际地址长度
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    
    // 接受新连接
    // - 由于使用事件驱动，调用时已有连接请求等待处理
    // - listener是监听套接字文件描述符
    int fd = accept(listener, (struct sockaddr *)&ss, &slen);
    
    // 判断接受连接的结果，进行错误处理
    if (fd < 0) {
        // 接受连接失败，可能是由于系统资源不足或其他错误
        perror("accept");
    } else if (fd > FD_SETSIZE) {
        // 文件描述符超过系统限制（FD_SETSIZE通常为1024）
        // 在高并发服务器中，这是一个需要特别关注的问题
        // 注意：现代系统通常允许使用更大的文件描述符集
        fprintf(stderr, "文件描述符 %d 超过系统限制\n", fd);
        close(fd);  // 关闭连接，释放资源
    } else {
        // =================================================================
        // 接受连接成功，为新连接创建事件处理
        // =================================================================
        // 创建一个新的事件结构体
        // - 先创建空事件，稍后再分配具体属性
        struct event *ev = event_new(NULL, -1, 0, NULL, NULL);
        
        // 初始化事件，将其与客户端文件描述符关联
        // - base: 事件循环实例
        // - fd: 新连接的文件描述符
        // - EV_READ | EV_PERSIST: 监听可读事件，并设置为持久模式
        // - http_read_cb: 事件触发时的回调函数
        // - (void *)ev: 将事件本身作为参数传递给回调函数，方便清理
        event_assign(ev, base, fd, EV_READ | EV_PERSIST, http_read_cb, (void *)ev);
        
        // 将事件添加到事件循环中，开始监听
        // - NULL表示没有超时限制
        // - 添加后，当fd上有可读事件时，libevent会自动调用http_read_cb
        event_add(ev, NULL);
    }
}

/**
 * @brief 主函数，实现基于libevent的非阻塞HTTP服务器
 * @details 初始化libevent库，创建监听套接字，设置事件处理器，启动事件循环
 * @return int 程序执行状态码，0表示成功，非0表示失败
 */
int main() {
    // =========================================================================
    // 主函数：基于libevent的非阻塞HTTP服务器实现
    // =========================================================================
    // libevent相关结构体定义
    struct event_base *base;      // 事件基础，是libevent事件循环的核心组件
    struct event *listener_event; // 监听事件结构体，用于处理新连接
    struct sockaddr_in sin;       // 服务器地址结构体，定义服务器监听地址和端口

    // =========================================================================
    // 步骤1: 初始化服务器地址信息
    // =========================================================================
    memset(&sin, 0, sizeof(sin));          // 清空地址结构体，防止垃圾值影响
    sin.sin_family = AF_INET;             // 使用IPv4地址族
    sin.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网络接口，接受任何IP地址的连接
    sin.sin_port = htons(PORT);           // 设置端口号，使用htons函数将主机字节序转换为网络字节序

    // =========================================================================
    // 步骤2: 创建并配置TCP监听套接字
    // =========================================================================
    // 创建TCP监听套接字
    // - PF_INET: IPv4协议族
    // - SOCK_STREAM: 流式套接字（TCP）
    // - 0: 使用默认协议（TCP）
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) {
        perror("socket");  // 打印具体错误信息
        return -1;         // 创建失败，返回错误码
    }

    // 设置套接字为非阻塞模式
    // - 这是libevent正常工作的关键，确保I/O操作不会阻塞事件循环
    // - 非阻塞模式下，即使没有数据可读或可写，操作也会立即返回
    evutil_make_socket_nonblocking(listener);
    
    // 设置套接字选项，允许地址复用
    // - SO_REUSEADDR选项允许在服务重启后快速重新绑定到同一地址和端口
    // - 避免TIME_WAIT状态导致的绑定失败问题
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // =========================================================================
    // 步骤3: 绑定和监听
    // =========================================================================
    // 将监听套接字绑定到指定地址和端口
    if (bind(listener, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");   // 打印错误信息
        close(listener);  // 关闭套接字，释放资源
        return -1;        // 绑定失败，返回错误码
    }

    // 开始监听连接请求
    // - 第二个参数1024表示连接请求队列的最大长度
    // - 超过这个数量的连接请求会被拒绝
    if (listen(listener, 1024) < 0) {
        perror("listen");  // 打印错误信息
        close(listener);   // 关闭套接字，释放资源
        return -1;         // 监听失败，返回错误码
    }

    printf("服务器启动成功，监听端口：%d\n", PORT);

    // =========================================================================
    // 步骤4: 初始化libevent并设置事件处理
    // =========================================================================
    // 初始化libevent库，创建事件基础对象
    // - event_base是libevent的核心，负责选择最优的事件多路分发机制
    // - 在Linux系统上通常使用epoll，在其他系统上可能使用select、poll等
    base = event_base_new();
    if (!base) {
        fprintf(stderr, "创建event_base失败\n");
        close(listener);
        return -1;
    }

    // 创建监听事件，关联到监听套接字
    // - base: 事件循环实例
    // - listener: 监听套接字文件描述符
    // - EV_READ | EV_PERSIST: 监听可读事件（新连接），并设置为持久模式
    // - accept_conn_cb: 当有新连接时调用的回调函数
    // - (void *)base: 将事件循环实例传递给回调函数
    listener_event = event_new(base, listener, EV_READ | EV_PERSIST, accept_conn_cb, (void *)base);

    // 将监听事件添加到事件循环中
    // - NULL表示没有超时限制
    // - 添加后，当监听套接字上有新连接时，libevent会自动调用accept_conn_cb
    event_add(listener_event, NULL);

    // =========================================================================
    // 步骤5: 启动事件循环
    // =========================================================================
    // 启动事件循环，开始处理事件
    // - 此函数会一直阻塞，直到没有更多事件或调用event_base_loopbreak
    // - 当有新连接或客户端数据到达时，相应的回调函数会被自动调用
    printf("事件循环已启动，等待客户端连接...\n");
    event_base_dispatch(base);

    // =========================================================================
    // 步骤6: 清理资源（正常情况下这部分代码不会执行到）
    // =========================================================================
    // 注意：如果没有外部中断或调用event_base_loopbreak，事件循环会一直运行
    // 以下代码只有在事件循环被显式终止时才会执行
    event_free(listener_event); // 释放监听事件资源
    event_base_free(base);      // 释放事件基础对象
    close(listener);            // 关闭监听套接字

    return 0;
}