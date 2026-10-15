module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Animation.SkinSource;

import Dot.Core.Result;

export namespace Dot::Animation {
  /** Ordered scene-local joint IDs, rewritten by clone/instance operations.
   * Authoring does not read assets; enabled bindings are validated by consumers.
   * 按顺序保存场景内关节 ID，复制/实例化须重映射；编辑不读文件，启用绑定由消费方校验。 */
  struct SkinProperties {
    std::string asset;
    std::vector<std::uint64_t> joints;
    bool enabled{true};
    auto operator==(const SkinProperties&) const -> bool = default;
  };

  [[nodiscard]] auto validateSkinProperties(const SkinProperties& properties) -> Core::Result<void>;
  [[nodiscard]] auto decodeSkinProperties(std::string_view json) -> Core::Result<SkinProperties>;
  [[nodiscard]] auto encodeSkinProperties(const SkinProperties& properties)
      -> Core::Result<std::string>;
}  // namespace Dot::Animation
