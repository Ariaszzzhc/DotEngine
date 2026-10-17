module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Animation.Source;

import Dot.Core.Result;
export import Dot.Animation.Layers;

export namespace Dot::Animation {
  struct Target {
    std::uint32_t node;
    std::uint64_t entity;
    auto operator==(const Target&) const -> bool = default;
  };

  /** Ordered override layer using a library clip and explicit logical channels.
   * Stable names address runtime controls; masked channels reserve writer authority
   * even while stopped/zero-weight. Definitions never borrow entity handles.
   * 有序覆盖层，使用库内片段和显式逻辑通道；稳定名称用于运行时控制，停止/零权重仍保留
   * 遮罩通道写入权。定义不借用实体句柄。 */
  struct LayerProperties {
    std::string name;
    std::uint32_t clip{};
    std::vector<LayerChannel> mask;
    double weight{1};
    double speed{1};
    bool looping{};
    bool playOnStart{};
    auto operator==(const LayerProperties&) const -> bool = default;
  };

  /** Scene-local IDs, not borrowed handles; cloning/remapping must rewrite them.
   * Clips are portable assets .dotanimation paths. Authoring reads no files.
   * Replacing a live description restarts its player; negative speed starts at
   * the selected clip end. Disabled players do not load assets or write poses.
   * 场景内 ID，不借用句柄，克隆/重映射时须改写。片段为 assets 内可移植路径，编辑不读文件。
   * 运行中替换描述会重启该播放器；负速度从片段末尾开始。禁用时不加载资产、不写姿态。 */
  struct PlayerProperties {
    std::vector<std::string> clips;
    std::vector<Target> targets;
    std::uint32_t clip{};
    double speed{1};
    bool looping{true};
    bool playOnStart{true};
    bool enabled{true};
    std::vector<LayerProperties> layers{};
    auto operator==(const PlayerProperties&) const -> bool = default;
  };

  [[nodiscard]] auto validatePlayerProperties(const PlayerProperties& properties)
      -> Core::Result<void>;
  /** Strict bounded JSON transport, canonical string entity IDs, no file IO.
   * 严格有界 JSON 传输，实体 ID 使用规范十进制字符串，不读写文件。 */
  [[nodiscard]] auto decodePlayerProperties(std::string_view source)
      -> Core::Result<PlayerProperties>;
  [[nodiscard]] auto encodePlayerProperties(const PlayerProperties& properties)
      -> Core::Result<std::string>;
}  // namespace Dot::Animation
