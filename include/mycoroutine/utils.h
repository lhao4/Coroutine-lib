#ifndef __MYCOROUTINE_UTILS_H__
#define __MYCOROUTINE_UTILS_H__

#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <cstdarg>
#include <ctime>

namespace mycoroutine {

/**
 * @brief 日志级别枚举
 */
enum class LogLevel {
    DEBUG   = 0,  // 调试信息
    INFO    = 1,  // 普通信息
    WARN    = 2,  // 警告信息
    ERROR   = 3,  // 错误信息
    FATAL   = 4,  // 致命错误
    UNKNOWN = 5   // 未知级别
};

/**
 * @brief 日志工具类
 */
class Logger {
public:
    /**
     * @brief 获取日志器单例
     */
    static Logger& GetInstance();
    
    /**
     * @brief 设置日志级别
     * @param level 日志级别
     */
    void setLevel(LogLevel level);
    
    /**
     * @brief 获取当前日志级别
     */
    LogLevel getLevel() const;
    
    /**
     * @brief 记录日志
     * @param level 日志级别
     * @param file 文件名
     * @param line 行号
     * @param format 格式化字符串
     * @param ... 可变参数
     */
    void log(LogLevel level, const char* file, int line, const char* format, ...);
    
private:
    /**
     * @brief 构造函数
     */
    Logger();
    
    /**
     * @brief 析构函数
     */
    ~Logger();
    
    /**
     * @brief 将日志级别转换为字符串
     * @param level 日志级别
     * @return 日志级别字符串
     */
    const char* levelToString(LogLevel level) const;
    
    /**
     * @brief 获取当前时间字符串
     * @return 时间字符串
     */
    std::string getTimeString() const;
    
private:
    LogLevel m_level;       // 当前日志级别
    std::mutex m_mutex;     // 日志输出互斥锁
};

// 日志宏定义
#define MYCOROUTINE_LOG(level, ...) \
    do { \
        mycoroutine::Logger& logger = mycoroutine::Logger::GetInstance(); \
        if (logger.getLevel() <= mycoroutine::LogLevel::level) { \
            logger.log(mycoroutine::LogLevel::level, __FILE__, __LINE__, __VA_ARGS__); \
        } \
    } while (0)

// 各级别日志宏
#define MYCOROUTINE_LOG_DEBUG(...)  MYCOROUTINE_LOG(DEBUG, __VA_ARGS__)
#define MYCOROUTINE_LOG_INFO(...)   MYCOROUTINE_LOG(INFO, __VA_ARGS__)
#define MYCOROUTINE_LOG_WARN(...)   MYCOROUTINE_LOG(WARN, __VA_ARGS__)
#define MYCOROUTINE_LOG_ERROR(...)  MYCOROUTINE_LOG(ERROR, __VA_ARGS__)
#define MYCOROUTINE_LOG_FATAL(...)  MYCOROUTINE_LOG(FATAL, __VA_ARGS__)

} // end namespace mycoroutine

#endif // __MYCOROUTINE_UTILS_H__
