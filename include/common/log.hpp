#pragma once
#include <iostream>
#include <mutex>
#include <sstream>
namespace chat {
// 先把一条日志拼入独立缓冲，临时对象析构时持锁输出，避免多线程日志交错。
class LogLine {
public:
    explicit LogLine(const char* level) { stream_ << '[' << level << "] "; }
    ~LogLine() { static std::mutex mutex; std::lock_guard<std::mutex> lock(mutex); std::clog << stream_.str() << '\n'; }
    template<class T> LogLine& operator<<(const T& value) { stream_ << value; return *this; }
private:
    std::ostringstream stream_;
};
}
#define LOG_INFO chat::LogLine("INFO")
#define LOG_ERROR chat::LogLine("ERROR")
#define LOG_DEBUG chat::LogLine("DEBUG")
