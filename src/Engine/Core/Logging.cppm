module;

#include <atomic>
#include <chrono>
#include <format>
#include <functional>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

export module Dot.Core.Logging;

export namespace Dot::Core {
  enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal, Off };

  struct LogRecord {
    LogLevel level;
    std::string_view message;
    std::source_location source;
    std::chrono::system_clock::time_point time;
  };

  /** Capture the caller when converting a format literal. 转换格式字符串时捕获调用位置。 */
  template <typename... Args>
  struct LogFormat {
    std::format_string<Args...> text;
    std::source_location source;

    template <typename Text>
    consteval LogFormat(const Text& format,
                        std::source_location location = std::source_location::current())
        : text(format), source(location) {
    }
  };

  [[nodiscard]] auto levelName(LogLevel level) noexcept -> std::string_view;

  /**
   * Concurrent logger; sinks must support concurrent calls and copy retained messages.
   * 并发日志器；输出端须支持并发调用，保留消息时须复制内容。
   */
  class Logger {
  public:
    using Sink = std::function<void(const LogRecord&)>;
    Logger();
    explicit Logger(Sink sink);
    auto setLevel(LogLevel level) noexcept -> void;
    auto setSink(Sink sink) -> void;
    /** Failed sinks never escape; failures remain observable. 输出失败不外抛，可查询失败次数。 */
    auto write(LogLevel level, std::string_view message,
               std::source_location source = std::source_location::current()) noexcept -> void;

    template <typename... Args>
    auto log(LogLevel level, std::type_identity_t<LogFormat<Args...>> format,
             Args&&... args) noexcept -> void {
      if (level == LogLevel::Off || level < m_level.load(std::memory_order_relaxed)) {
        return;
      }

      try {
        write(level, std::format(format.text, std::forward<Args>(args)...), format.source);
      } catch (...) {
        m_failures.fetch_add(1, std::memory_order_relaxed);
      }
    }

    [[nodiscard]] auto failures() const noexcept -> std::size_t;

  private:
    std::mutex m_mutex;
    Sink m_sink;
    std::atomic<LogLevel> m_level{LogLevel::Info};
    std::atomic<std::size_t> m_failures{0};
  };
}  // namespace Dot::Core
