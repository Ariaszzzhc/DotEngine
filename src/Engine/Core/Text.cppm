module;

#include <cstddef>
#include <string_view>

export module Dot.Core.Text;

import Dot.Core.Result;

export namespace Dot::Core {
  /** Validate scalar UTF-8 without allocation. Embedded NUL is valid UTF-8;
   * individual subsystem contracts may reject it. No normalization occurs.
   * 无分配地校验标量 UTF-8；内嵌 NUL 属于有效 UTF-8，具体子系统可拒绝。
   * 不执行规范化。
   */
  [[nodiscard]] auto isValidUtf8(std::string_view text) noexcept -> bool;
  /** Count scalar values, not grapheme clusters/display characters. Invalid
   * encoding returns the offending byte offset in its error.
   * 统计标量值而非字素簇或显示字符；无效编码的错误包含出错字节偏移。
   */
  [[nodiscard]] auto utf8CodePoints(std::string_view text) -> Result<std::size_t>;
}  // namespace Dot::Core
