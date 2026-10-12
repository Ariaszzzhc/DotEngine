module;

#include <string>

export module Dot.Script.Source;

import Dot.Core.Result;

export namespace Dot::Script {
  /** Authoring data only. Source is a canonical portable UTF-8 .ts/.mts path
   * relative to the compiler's source directory; declarations are not executable.
   * Class export is a 1..128-byte ASCII identifier. Properties are a finite scalar
   * Unicode JSON object, at most 64 KiB/depth 32 without duplicate keys. Validation
   * does not load code or create a realm; enabled controls lifecycle activation.
   * 仅编辑描述；source 为编译源码目录相对、规范可移植 UTF-8 .ts/.mts 路径，声明不可
   * 执行；类导出名为 1..128 字节 ASCII 标识符。properties 是有限标量 Unicode JSON
   * 对象，最多 64 KiB/32 层且无重复字段；校验不加载代码或创建 realm，enabled 控制激活。 */
  struct ComponentProperties {
    std::string source;
    std::string className{"default"};
    std::string properties{"{}"};
    bool enabled{true};
    auto operator==(const ComponentProperties&) const -> bool = default;
  };

  /** Validate and own a canonical compact properties representation.
   * 校验并拥有规范紧凑的属性表示。 */
  [[nodiscard]] auto normalizeComponentProperties(const ComponentProperties& properties)
      -> Core::Result<ComponentProperties>;
  [[nodiscard]] auto emittedModule(const ComponentProperties& properties)
      -> Core::Result<std::string>;
}  // namespace Dot::Script
