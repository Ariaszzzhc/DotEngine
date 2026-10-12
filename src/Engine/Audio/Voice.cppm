module;

export module Dot.Audio.Voice;

import Dot.Core.Result;

export namespace Dot::Audio {
  struct VoiceSettings {
    float volume{1};
    float pan{};
    float pitch{1};
    bool looping{};
    auto operator==(const VoiceSettings&) const -> bool = default;
  };

  /** Backend-independent authoring/control checks: finite volume [0,1], pan
   * [-1,1], and pitch [0.25,4]. 与后端无关的编辑/控制校验，有限音量 [0,1]、声像 [-1,1]、
   * 音高 [0.25,4]。 */
  [[nodiscard]] auto validateVoiceSettings(const VoiceSettings& settings) -> Core::Result<void>;
}  // namespace Dot::Audio
