module;

#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <iostream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

module Dot.Core.Logging;

namespace Dot::Core {
  Logger::Logger()
      : Logger([](const LogRecord& record) {
          static std::mutex consoleMutex;
          const std::scoped_lock lock(consoleMutex);
          std::clog << '[' << levelName(record.level) << "] " << record.message << '\n';

          if (!std::clog) {
            throw std::ios_base::failure("Console log output failed");
          }
        }) {
  }

  auto levelName(LogLevel level) noexcept -> std::string_view {
    switch (level) {
      case LogLevel::Trace:
        return "TRACE";
      case LogLevel::Debug:
        return "DEBUG";
      case LogLevel::Info:
        return "INFO";
      case LogLevel::Warn:
        return "WARN";
      case LogLevel::Error:
        return "ERROR";
      case LogLevel::Fatal:
        return "FATAL";
      case LogLevel::Off:
        return "OFF";
    }

    return "UNKNOWN";
  }

  Logger::Logger(Sink sink) : m_sink(std::move(sink)) {
  }

  auto Logger::setLevel(LogLevel level) noexcept -> void {
    m_level.store(level, std::memory_order_relaxed);
  }

  auto Logger::setSink(Sink sink) -> void {
    const std::scoped_lock lock(m_mutex);
    m_sink = std::move(sink);
  }

  auto Logger::write(LogLevel level, std::string_view message, std::source_location source) noexcept
      -> void {
    if (level == LogLevel::Off || level < m_level.load(std::memory_order_relaxed)) {
      return;
    }

    try {
      Sink sink;
      {
        const std::scoped_lock lock(m_mutex);
        sink = m_sink;
      }

      if (sink) {
        sink(LogRecord{level, message, source, std::chrono::system_clock::now()});
      }
    } catch (...) {
      m_failures.fetch_add(1, std::memory_order_relaxed);
    }
  }

  auto Logger::failures() const noexcept -> std::size_t {
    return m_failures.load(std::memory_order_relaxed);
  }

}  // namespace Dot::Core
