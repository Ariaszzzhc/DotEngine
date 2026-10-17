#pragma once

/** Private renderer-module declarations; native types stay in implementation units.
 * 渲染器模块私有声明；原生类型仅用于实现单元。 */
namespace Dot::Graphics::Detail {
  enum class ProfilePass : std::uint32_t { Shadows, Occlusion, Surface, Post };

  class FrameProfiler {
  public:
    [[nodiscard]] static auto create(wgpu::Instance instance, wgpu::Device device,
                                     bool gpuSupported)
        -> Core::Result<std::unique_ptr<FrameProfiler>>;
    ~FrameProfiler();
    auto begin() -> void;
    auto prepared() -> void;
    auto encoding(std::uint32_t width, std::uint32_t height) -> void;
    auto encoded() -> void;
    auto cancel() -> void;
    [[nodiscard]] auto writes(ProfilePass pass) -> wgpu::PassTimestampWrites;
    auto resolve(const wgpu::CommandEncoder& encoder) -> void;
    auto submitted(const MeshFrameStats& work) -> void;
    [[nodiscard]] auto snapshot() -> MeshProfiling;

  private:
    struct Impl;
    explicit FrameProfiler(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> m_impl;
  };
}  // namespace Dot::Graphics::Detail
