#include <mycoroutine/utils.h>
#include <cstring>

namespace mycoroutine {

Logger::Logger() : m_level(LogLevel::DEBUG) {
    // 默认日志级别为DEBUG
}

Logger::~Logger() {
    // 析构函数
}

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLevel(LogLevel level) {
    m_level = level;
}

LogLevel Logger::getLevel() const {
    return m_level;
}

const char* Logger::levelToString(LogLevel level) const {
    switch (level) {
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::getTimeString() const {
    time_t now = time(nullptr);
    struct tm tm_info;
    char buf[20];
    
    // 线程安全的本地时间转换
    localtime_r(&now, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    
    return std::string(buf);
}

void Logger::log(LogLevel level, const char* file, int line, const char* format, ...) {
    // 检查日志级别
    if (level < m_level) {
        return;
    }
    
    // 格式化日志消息
    char msg[1024] = {0};
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);
    
    // 获取文件名（仅保留最后一部分）
    const char* filename = strrchr(file, '/');
    if (filename == nullptr) {
        filename = file;
    } else {
        filename++;
    }
    
    // 加锁确保日志输出线程安全
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 输出日志
    std::cout << "[" << getTimeString() << "] "
              << "[" << levelToString(level) << "] "
              << "[" << filename << ":" << line << "] "
              << msg << std::endl;
}

} // end namespace mycoroutine
