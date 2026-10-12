module;

#include <expected>
#include <source_location>
#include <string>
#include <utility>

module Dot.Core.Result;

namespace Dot::Core {
  auto fail(ErrorCode code, std::string message, std::source_location source)
      -> std::unexpected<Error> {
    return std::unexpected(Error{code, std::move(message), source});
  }

}  // namespace Dot::Core
