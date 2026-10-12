module;

#include <cstddef>
#include <cstdint>
#include <memory>

export module Dot.Script.InputSource;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Platform.Input;

export namespace Dot::Script {
  struct InputSourceConfig {
    std::size_t textBytes{64 * 1024};
  };

  /** Owns the last explicitly published input frame, independent of scene/realm
   * lifetime. Operations require the creating thread; moves preserve existing
   * bindings, and quiescent destruction may occur elsewhere. Bindings retain the
   * published data after the publisher dies. No window/event pumping is created.
   * 拥有最近一次显式发布的输入帧，独立于场景/realm 寿命；操作限创建线程，移动保留
   * 已有绑定，静止销毁可在异线程进行。发布者销毁后绑定仍保留数据，不创建窗口/事件泵。 */
  class InputSource {
  public:
    [[nodiscard]] static auto create(const InputSourceConfig& config = {})
        -> Core::Result<InputSource>;
    InputSource(const InputSource&) = delete;
    auto operator=(const InputSource&) -> InputSource& = delete;
    InputSource(InputSource&&) noexcept;
    auto operator=(InputSource&&) noexcept -> InputSource&;
    ~InputSource();

    /** Copy after native event consumption and before script dispatch. Successful
     * publication increments a nonzero uint64 index; errors retain the old frame.
     * Points must be finite and text valid UTF-8 within the configured 1..1 MiB
     * limit. Caller must not concurrently modify the borrowed native InputState.
     * 原生事件处理后、脚本分发前复制；成功发布递增非零 uint64 序号，错误保留旧帧。
     * 坐标须有限，文本为有效 UTF-8 且不超过配置的 1 字节..1 MiB 限制，调用者不得并发
     * 修改借用的原生 InputState。 */
    [[nodiscard]] auto publish(const Platform::InputState& state) -> Core::Result<std::uint64_t>;
    /** Eight observational methods return owned snapshots without consuming edges;
     * valid reads before publication fail Unavailable. JSON indices are decimal
     * strings, key/button names are exact native enum names (excluding sentinels).
     * Fixed steps sharing an input frame see the same edges; the application owns
     * beginFrame/event/publish timing. 八个只读方法返回拥有快照且不消费边沿；发布前合法
     * 读取返回 Unavailable。序号为十进制字符串，按键/鼠标名称精确匹配原生枚举（除哨兵）。
     * 共用输入帧的固定步看到相同边沿，应用负责 beginFrame/事件/发布时间。 */
    [[nodiscard]] auto bindings() const -> Core::Result<HostBindings>;

  private:
    struct Impl;
    explicit InputSource(std::shared_ptr<Impl> impl) noexcept;
    std::shared_ptr<Impl> m_impl;
  };
}  // namespace Dot::Script
