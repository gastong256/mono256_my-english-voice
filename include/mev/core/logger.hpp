#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace mev {

enum class LogLevel : int { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3 };

class Logger {
 public:
  static Logger& instance() {
    static Logger logger;
    return logger;
  }

  void set_level(LogLevel level) { level_.store(level, std::memory_order_release); }

  [[nodiscard]] LogLevel level() const { return level_.load(std::memory_order_acquire); }

  template <typename... Args>
  void log(LogLevel level, Args&&... args) {
    if (static_cast<int>(level) > static_cast<int>(this->level())) {
      return;
    }

    std::ostringstream oss;
    append(oss, std::forward<Args>(args)...);

    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << to_string(level) << " " << oss.str() << '\n';
  }

 private:
  Logger() = default;

  static const char* to_string(LogLevel level) {
    switch (level) {
      case LogLevel::kError:
        return "[ERROR]";
      case LogLevel::kWarn:
        return "[WARN ]";
      case LogLevel::kInfo:
        return "[INFO ]";
      case LogLevel::kDebug:
        return "[DEBUG]";
    }
    return "[UNKWN]";
  }

  template <typename T>
  static void append(std::ostringstream& oss, T&& value) {
    oss << std::forward<T>(value);
  }

  template <typename T, typename... Args>
  static void append(std::ostringstream& oss, T&& value, Args&&... args) {
    oss << std::forward<T>(value);
    append(oss, std::forward<Args>(args)...);
  }

  std::mutex mutex_;
  std::atomic<LogLevel> level_{LogLevel::kInfo};
};

#define MEV_LOG_ERROR(...) ::mev::Logger::instance().log(::mev::LogLevel::kError, __VA_ARGS__)
#define MEV_LOG_WARN(...) ::mev::Logger::instance().log(::mev::LogLevel::kWarn, __VA_ARGS__)
#define MEV_LOG_INFO(...) ::mev::Logger::instance().log(::mev::LogLevel::kInfo, __VA_ARGS__)
#define MEV_LOG_DEBUG(...) ::mev::Logger::instance().log(::mev::LogLevel::kDebug, __VA_ARGS__)

}  // namespace mev
