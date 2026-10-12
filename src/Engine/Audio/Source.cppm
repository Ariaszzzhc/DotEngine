module;

#include <string>

export module Dot.Audio.Source;

export import Dot.Audio.Voice;
export import Dot.Audio.Spatial;
import Dot.Core.Result;

export namespace Dot::Audio {
  /** Pose-independent source authoring. Asset must be a project-relative UTF-8
   * .wav/.flac/.mp3 path; validation does not read files or create native voices.
   * Entity world transforms supply spatial pose at binding time.
   * 与位姿无关的音频源编辑描述，资产须为项目相对 UTF-8 .wav/.flac/.mp3 路径；校验
   * 不读取文件或创建原生声部，绑定时由实体世界变换提供空间位姿。 */
  struct SourceProperties {
    std::string asset;
    VoiceSettings voice;
    SpatialProperties spatial;
    bool playOnStart{true};
    auto operator==(const SourceProperties&) const -> bool = default;
  };

  [[nodiscard]] auto validateSourceProperties(const SourceProperties& properties)
      -> Core::Result<void>;
}  // namespace Dot::Audio
