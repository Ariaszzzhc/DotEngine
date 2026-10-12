module;

#include <expected>
#include <source_location>
#include <string>
#include <utility>

export module Dot.Core.Result;

export namespace Dot::Core {
  enum class ErrorCode { InvalidArgument, NotFound, Io, Unavailable, InvalidState, External };

  /** Structured failure with its origin. 带来源信息的结构化错误。 */
  struct Error {
    ErrorCode code;
    std::string message;
    std::source_location source;
  };

  template <typename T>
  using Result = std::expected<T, Error>;

  [[nodiscard]] auto fail(ErrorCode code, std::string message,
                          std::source_location source = std::source_location::current())
      -> std::unexpected<Error>;
}  // namespace Dot::Core
