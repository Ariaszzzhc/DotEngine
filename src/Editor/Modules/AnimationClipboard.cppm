module;

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Dot.Editor.AnimationClipboard;

export import Dot.Animation.Edit;
import Dot.Core.Result;

export namespace Dot::Editor {
  struct AnimationClipboardContext {
    std::uint64_t scene{};
    std::uint64_t owner{};
    std::map<std::uint32_t, std::uint64_t> bindings;
    auto operator==(const AnimationClipboardContext&) const -> bool;
  };

  struct AnimationClipboardSource {
    AnimationClipboardContext context;
    std::uint64_t buffer{};
    std::uint64_t revision{};
    std::string path;
  };

  struct AnimationClipboardSnapshot {
    std::uint64_t id;
    AnimationClipboardSource source;
    Animation::KeyGroup keys;
  };

  struct AnimationPaste {
    Animation::Clip value;
    std::vector<Animation::KeyReference> selection;
  };

  /** Workspace-owned typed clipboard, independent of OS clipboard/windows/files.
   * Copy validates provenance and selected node bindings before atomically replacing
   * content; failed copies retain content and ID. IDs never repeat after clear.
   * Owned immutable snapshots survive replacement/clear/service destruction.
   * Preparation requires the same authoring scene, player and exact full node/entity
   * map, reuses native paste editing and returns a candidate plus selected targets;
   * it never changes resources, history, preview or clipboard. Caller performs native
   * scene/pose/skin preflight and one history publication. No implicit retargeting.
   * Service is noncopyable/nonmovable; synchronize mutations against any other call.
   * 工作区拥有的类型化剪贴板，无系统剪贴板/窗口/文件。复制先校验来源及所选节点绑定再
   * 原子替换，失败保留内容/标识；清空后不复用标识。不可变拥有快照可晚于替换/清空/服务
   * 销毁。准备粘贴须同编辑场景、播放器及完整节点/实体映射，复用原生编辑返回候选与目标
   * 选择，不改资源/历史/预览/剪贴板；调用方预检实际场景/姿态/蒙皮后提交一步历史，不隐式
   * 重定向。服务不可复制/移动，修改须与其他调用同步。 */
  class AnimationClipboard {
  public:
    AnimationClipboard() = default;
    AnimationClipboard(const AnimationClipboard&) = delete;
    auto operator=(const AnimationClipboard&) -> AnimationClipboard& = delete;
    AnimationClipboard(AnimationClipboard&&) = delete;
    auto operator=(AnimationClipboard&&) -> AnimationClipboard& = delete;
    [[nodiscard]] auto copy(const Animation::Clip& value,
                            std::span<const Animation::KeyReference> selection,
                            const AnimationClipboardSource& source)
        -> Core::Result<std::shared_ptr<const AnimationClipboardSnapshot>>;
    [[nodiscard]] auto snapshot() const noexcept
        -> std::shared_ptr<const AnimationClipboardSnapshot>;
    [[nodiscard]] auto read(std::uint64_t id) const
        -> Core::Result<std::shared_ptr<const AnimationClipboardSnapshot>>;
    [[nodiscard]] auto clear(std::uint64_t id) -> Core::Result<void>;
    [[nodiscard]] auto compatible(const AnimationClipboardContext& context) const -> bool;
    [[nodiscard]] auto prepare(const Animation::Clip& destination,
                               const AnimationClipboardContext& context, std::uint64_t id,
                               float time, Animation::KeyCollision collision) const
        -> Core::Result<AnimationPaste>;

  private:
    std::shared_ptr<const AnimationClipboardSnapshot> m_value;
    std::uint64_t m_serial{};
  };
}  // namespace Dot::Editor
