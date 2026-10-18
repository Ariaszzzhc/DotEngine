module;

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

export module Dot.Navigation.Files;

export import Dot.Navigation.Surface;
import Dot.Core.Result;
import Dot.Core.Files;

export namespace Dot::Navigation {
  /** DotNav: eight bytes DOTNAV\0\1, little-endian scalar/count fields, canonical
   * geometry identity and bounded engine polygon/detail arrays. Exact length,
   * <=64 MiB; no vendor structs, native endianness, padding or alternate layout.
   * DotNav：八字节 DOTNAV\0\1，小端标量/数量、规范几何身份及有界引擎多边形/细节数组。
   * 严格匹配长度，至多 64 MiB；不包含第三方结构、本机字节序、填充或替代布局。 */
  [[nodiscard]] auto decodeSurface(std::span<const std::byte> bytes) -> Core::Result<Surface>;
  [[nodiscard]] auto encodeSurface(const Surface& surface) -> Core::Result<std::vector<std::byte>>;
  /** Load a regular assets/ .dotnav with exact portable spelling, no symlink components.
   * 读取 assets/ 下路径拼写精确的可移植普通 .dotnav，各组件不得为符号链接。 */
  [[nodiscard]] auto loadSurface(const Core::FileSystem& files, std::string_view path)
      -> Core::Result<Surface>;
}  // namespace Dot::Navigation
