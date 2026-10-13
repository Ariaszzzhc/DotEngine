#pragma once

/** Private renderer-module declarations; native headers belong to global fragments.
 * 渲染器模块私有声明；原生头文件属于全局片段。 */
namespace Dot::Graphics::Detail {
  class GuiBackend {
  public:
    [[nodiscard]] static auto create(WGPUDevice device, WGPUTextureFormat format,
                                     SDL_Window* window, const GuiConfig& config)
        -> Core::Result<std::unique_ptr<GuiBackend>>;
    ~GuiBackend();
    [[nodiscard]] auto event(const Platform::Event& event) -> Core::Result<bool>;
    [[nodiscard]] auto build(const GuiDraw& draw, std::uint32_t width, std::uint32_t height,
                             double seconds, float scaleX, float scaleY) -> Core::Result<void>;
    auto encode(WGPURenderPassEncoder pass) -> void;
    [[nodiscard]] auto image(WGPUTextureView view, float width, float height) -> Core::Result<void>;
    [[nodiscard]] auto stats() const -> GuiStats;
    [[nodiscard]] auto building() const -> bool;
    auto submitted() -> void;

  private:
    struct Impl;
    explicit GuiBackend(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Graphics::Detail
