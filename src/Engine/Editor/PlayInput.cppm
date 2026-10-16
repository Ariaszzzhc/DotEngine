module;

#include <cstdint>
#include <optional>
#include <string_view>

export module Dot.Editor.PlayInput;

import Dot.Core.Result;
import Dot.Platform.Input;

export namespace Dot::Editor {
  struct PlayInputStats {
    bool active{};
    std::uint32_t remaining{};
    std::uint32_t published{};
  };

  /** Bounded agent input uses the same frame state consumed by native Play.
   * One request owns 1..600 native Play frames, then publishes a release frame.
   * It never advances simulation, modifies the OS, or fabricates pointer capture.
   * Calls are serialized by the owning frontend. Returned frames are owned copies.
   * 有界智能体输入使用原生 Play 共用的帧状态。请求占用 1..600 个实际 Play 帧，
   * 随后发布释放帧；不推进模拟、不修改系统，也不伪造指针捕获。前端串行调用，返回独立帧副本。 */
  class PlayInput {
  public:
    /** Strict UTF-8 JSON <=64 KiB/depth 8: frames, keys, buttons, pointer,
     * pointerDelta, wheel, text are all required. Names use the script vocabulary;
     * Escape/F5 remain editor controls. Duplicate/unknown names fail atomically.
     * Position/delta are within +/-1e6, wheel within +/-1000, text <=16 KiB without
     * NUL. Text and press edges publish once; delta/wheel repeat per held frame.
     * Edges compare against the last published frame (neutral by default). Cancelling
     * before publication releases only that previous frame, never unseen presses.
     * An active request must finish or be cancelled/released before replacement.
     * 严格 UTF-8 JSON，至多 64 KiB/8 层；上述字段全部必需。名称与脚本一致，
     * Escape/F5 保留为编辑器操作；重复或未知名称原子拒绝。坐标/位移界限 +/-1e6、
     * 滚轮 +/-1000，文本至多 16 KiB 且无 NUL。文本/按下边沿仅发布一次，位移与滚轮逐帧重复。
     * 边沿对照最后已发布帧（默认中立）；发布前取消仅释放此前的帧，不产生未见按下的释放。
     * 活动请求必须完成或取消并发布释放帧后才可替换。 */
    [[nodiscard]] auto submit(std::string_view value,
                              const Platform::InputState& previous = Platform::InputState{})
        -> Core::Result<void>;
    [[nodiscard]] auto next() -> Core::Result<std::optional<Platform::InputState>>;
    /** Cancellation schedules one release frame; reset discards all state for a
     * stopped/faulted session. 取消会安排一个释放帧；重置供已停止/故障会话丢弃全部状态。 */
    auto cancel() noexcept -> void;
    auto reset() noexcept -> void;
    [[nodiscard]] auto stats() const noexcept -> PlayInputStats;

  private:
    Platform::InputState m_state;
    Platform::InputState m_previous;
    Platform::Point m_delta;
    Platform::Point m_wheel;
    PlayInputStats m_stats;
  };
}  // namespace Dot::Editor
