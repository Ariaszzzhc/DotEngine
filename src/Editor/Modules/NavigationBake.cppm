module;

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Dot.Editor.NavigationBake;

export import Dot.Navigation.Surface;
import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;

export namespace Dot::Editor {
  [[nodiscard]] auto decodeNavigationSettings(std::string_view text)
      -> Core::Result<Navigation::BuildSettings>;
  [[nodiscard]] auto encodeNavigationSettings(const Navigation::BuildSettings& settings)
      -> Core::Result<std::string>;

  struct NavigationInput {
    std::string path;
    std::vector<std::byte> bytes;
  };

  struct NavigationCreation {
    Navigation::Surface surface;
    std::vector<std::byte> bytes;
    std::filesystem::path root;
    std::vector<NavigationInput> inputs;
  };

  /** Re-read exactly the frozen source bytes before publication. Callers serialize
   * project writes; this is an authoring conflict guard, not OS-wide snapshot isolation.
   * 发布前精确复核冻结来源字节；调用方须串行项目写入，这是编辑冲突检查而非 OS 快照隔离。 */
  [[nodiscard]] auto validateNavigationInputs(const Core::FileSystem& files,
                                              const NavigationCreation& creation)
      -> Core::Result<void>;

  /** Owned CPU worker with canonical scene text, <=64 static mesh sources/64 MiB.
   * No active Scene handles cross threads, files write only on explicit publication.
   * Owner-thread ready/take are nonblocking; result is consumed once. Cancellation/
   * destruction joins between bounded cooking phases, never detaches.
   * 拥有规范场景文本及至多 64 份/64 MiB 静态网格来源的 CPU 工作线程。不跨线程传递实时
   * Scene 句柄，显式发布才写文件。拥有线程的 ready/take 不等待，结果仅提取一次；
   * 取消/销毁在有界烘焙阶段之间回收线程，不分离。 */
  class NavigationBake {
  public:
    [[nodiscard]] static auto begin(const Core::FileSystem& files, const World::Scene& scene,
                                    const Navigation::BuildSettings& settings)
        -> Core::Result<NavigationBake>;
    NavigationBake(const NavigationBake&) = delete;
    auto operator=(const NavigationBake&) -> NavigationBake& = delete;
    NavigationBake(NavigationBake&&) noexcept;
    auto operator=(NavigationBake&&) noexcept -> NavigationBake&;
    ~NavigationBake();
    [[nodiscard]] auto ready() const -> Core::Result<bool>;
    [[nodiscard]] auto take() -> Core::Result<NavigationCreation>;
    auto requestStop() noexcept -> void;

  private:
    struct Impl;
    explicit NavigationBake(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Editor
