module;

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

export module Dot.Graphics.TextRenderer;

import Dot.Core.Result;
import Dot.Graphics.Text;
import Dot.Graphics.Renderer;

export namespace Dot::Graphics {
  struct TextRenderStats {
    std::uint64_t textureBytes{};
    /** Completed CPU rasters, including subsequently discarded candidates.
     * 已完成的 CPU 栅格化次数，包括随后丢弃的候选。 */
    std::uint64_t rasterizations{};
    std::vector<std::string> overflow;
  };

  /** Borrows a stable renderer. Lazily owns the explicit font (<=32 MiB) and
   * <=16 MiB logical UI textures including old/candidate replacement peak.
   * Reuse unchanged descriptions/dimensions, prune absent/hidden keys. Subpixel
   * boxes cull without a texture and report overflow. No system
   * font fallback or retained scene/GUI handles. Owner-thread operations.
   * 借用稳定渲染器，按需拥有显式字体（至多 32 MiB）及至多 16 MiB 逻辑 UI 贴图，
   * 包括新旧替换峰值；复用未变描述/尺寸，移除消失/隐藏键；不足一像素的框不分配贴图并报告溢出。
   * 不回退系统字体或保留
   * 场景/GUI 句柄，操作限所有者线程。 */
  class TextRenderer {
  public:
    [[nodiscard]] static auto create(Renderer& renderer, std::filesystem::path font)
        -> Core::Result<TextRenderer>;
    TextRenderer(TextRenderer&&) noexcept;
    auto operator=(TextRenderer&&) noexcept -> TextRenderer&;
    ~TextRenderer();
    [[nodiscard]] auto prepare(std::span<const TextLayer> labels, std::uint32_t width,
                               std::uint32_t height) -> Core::Result<std::vector<ScreenDraw>>;
    [[nodiscard]] auto stats() const -> Core::Result<TextRenderStats>;

  private:
    struct Impl;
    explicit TextRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Graphics
