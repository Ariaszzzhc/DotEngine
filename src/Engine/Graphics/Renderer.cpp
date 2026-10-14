module;

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <imgui.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Math;
import Dot.Graphics.View;
import Dot.Platform.Window;
import Dot.Platform.System;
import Dot.Platform.Events;

#include "BuiltinShaders.hpp"
#include "GuiBackend.hpp"

namespace Dot::Graphics {
  namespace {
    constexpr std::uint64_t waitTimeout = 10'000'000'000;
    constexpr std::size_t maxVertices = 3'000'000;
    constexpr std::uint32_t maxReadbackDimension = 4096;

    auto copied(wgpu::StringView value) -> std::string {
      if (!value.data) {
        return {};
      }

      return value.length == WGPU_STRLEN ? std::string(value.data)
                                         : std::string(value.data, value.length);
    }

    auto colorValid(Math::Vec4 value) -> bool {
      return Math::isFinite(value) && value.x >= 0 && value.x <= 1 && value.y >= 0 && value.y <= 1
             && value.z >= 0 && value.z <= 1 && value.w >= 0 && value.w <= 1;
    }

    auto radianceValid(Math::Vec4 value) -> bool {
      return Math::isFinite(value) && value.x >= 0 && value.x <= 65504 && value.y >= 0
             && value.y <= 65504 && value.z >= 0 && value.z <= 65504 && value.w >= 0
             && value.w <= 1;
    }

    auto halfFloat(std::uint16_t bits) -> float {
      const auto exponent = static_cast<int>((bits >> 10) & 31U);
      const auto mantissa = static_cast<int>(bits & 1023U);
      const auto sign = (bits & 32768U) != 0 ? -1.0F : 1.0F;

      if (exponent == 31) {
        return mantissa == 0 ? sign * std::numeric_limits<float>::infinity()
                             : std::numeric_limits<float>::quiet_NaN();
      }

      return sign
             * (exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                              : std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25));
    }

    auto validate(std::span<const Vertex> vertices, const Math::Mat4& transform, Math::Vec4 clear)
        -> Core::Result<void> {
      if (vertices.size() > maxVertices || vertices.size() % 3 != 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Geometry must be a triangle list with at most 3000000 vertices");
      }

      if (!Math::isFinite(transform) || !radianceValid(clear)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid transform or clear color");
      }

      for (const auto& vertex : vertices) {
        const auto normalLength
            = std::hypot(static_cast<double>(vertex.normal.x), static_cast<double>(vertex.normal.y),
                         static_cast<double>(vertex.normal.z));

        if (!Math::isFinite(vertex.position) || !colorValid(vertex.color)
            || !Math::isFinite(vertex.normal)
            || (normalLength != 0 && std::abs(normalLength - 1) > 1e-4)
            || !std::isfinite(vertex.texCoord.x) || !std::isfinite(vertex.texCoord.y)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid vertex position, color, normal or UV");
        }
      }

      return {};
    }

    auto wait(const wgpu::Instance& instance, wgpu::Future future, std::string_view context)
        -> Core::Result<void> {
      auto status = instance.WaitAny(future, waitTimeout);

      if (status != wgpu::WaitStatus::Success) {
        return Core::fail(
            Core::ErrorCode::Unavailable,
            std::format("{}: GPU wait failed ({})", context, static_cast<unsigned>(status)));
      }

      return {};
    }

    template <typename T>
    struct Pending {
      std::shared_ptr<void> keepAlive;
      T value{};
      std::string error;
    };

    struct UniformData {
      Math::Mat4 transform;
      Math::Vec4 tint{1, 1, 1, 1};
      Math::Vec4 emissive{};
      Math::Mat4 model{};
      Math::Mat4 normalMatrix{};
      Math::Vec4 material{0, 0.5F, 1, 0};
      Math::Vec4 flags{0, 0, 0, 1};
    };

    static_assert(sizeof(UniformData) == 256);
    static_assert(offsetof(UniformData, tint) == 64);
    static_assert(offsetof(UniformData, emissive) == 80);
    static_assert(offsetof(UniformData, model) == 96);
    static_assert(offsetof(UniformData, normalMatrix) == 160);
    static_assert(offsetof(UniformData, material) == 224);

    struct LightData {
      Math::Vec4 directionKind;
      Math::Vec4 positionRange;
      Math::Vec4 colorIntensity;
      Math::Vec4 cone;
    };

    struct LightingData {
      Math::Vec4 viewPosition;
      Math::Vec4 meta;
      std::array<LightData, 64> lights{};
    };

    static_assert(sizeof(LightData) == 64);
    static_assert(offsetof(LightingData, lights) == 32);
    static_assert(sizeof(LightingData) == 4128);

    struct PostUniform {
      float exposure;
      std::uint32_t toneMap;
      std::uint32_t encoding;
      std::uint32_t padding{};
    };

    static_assert(sizeof(PostUniform) == 16);

    struct Diagnostics {
      std::atomic<bool> alive{true};
      std::atomic<std::uint64_t> meshes{};
      std::atomic<std::uint64_t> vertexBytes{};
      std::atomic<std::uint64_t> indexBytes{};
      std::atomic<std::uint64_t> targets{};
      std::atomic<std::uint64_t> targetBytes{};
      std::atomic<std::uint64_t> textures{};
      std::atomic<std::uint64_t> textureBytes{};
      std::atomic<std::uint64_t> environments{};
      std::atomic<std::uint64_t> environmentBytes{};
      std::atomic<bool> failed{};
      std::mutex mutex;
      std::string failure;

      auto record(std::string_view context, unsigned status, wgpu::StringView message) noexcept
          -> void {
        failed.store(true);

        try {
          std::scoped_lock lock(mutex);
          failure = std::format("{} ({}): {}", context, status, copied(message));
        } catch (...) {
        }
      }
    };

    auto describeFailure(std::string& fallback, std::string_view context, unsigned status,
                         wgpu::StringView message) noexcept -> void {
      try {
        fallback = std::format("{} ({}): {}", context, status, copied(message));
      } catch (...) {
      }
    }

    /** Error scopes always balance, including early exits. Callbacks own their
     * state so timeout/cancellation cannot dereference a departed stack frame.
     * 错误作用域在提前退出时也保持配对；回调拥有其状态，超时或取消不会访问失效栈帧。
     */
    class ErrorScope {
    public:
      ErrorScope(wgpu::Instance instance, wgpu::Device device)
          : m_instance(std::move(instance)), m_device(std::move(device)) {
        m_device.PushErrorScope(wgpu::ErrorFilter::Internal);
        m_device.PushErrorScope(wgpu::ErrorFilter::OutOfMemory);
        m_device.PushErrorScope(wgpu::ErrorFilter::Validation);
      }

      ErrorScope(const ErrorScope&) = delete;
      auto operator=(const ErrorScope&) -> ErrorScope& = delete;

      ~ErrorScope() {
        if (!m_finished) {
          for (int index = 0; index < 3; ++index) {
            static_cast<void>(m_device.PopErrorScope(
                wgpu::CallbackMode::AllowSpontaneous,
                [](wgpu::PopErrorScopeStatus, wgpu::ErrorType, wgpu::StringView) noexcept {}));
          }
        }
      }

      auto finish() -> Core::Result<void> {
        m_finished = true;
        std::string error;

        for (int index = 0; index < 3; ++index) {
          auto pending = std::make_shared<Pending<bool>>();
          pending->error = "GPU error scope failed";
          auto future = m_device.PopErrorScope(
              wgpu::CallbackMode::WaitAnyOnly,
              [pending](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type,
                        wgpu::StringView message) noexcept {
                if (status != wgpu::PopErrorScopeStatus::Success
                    || type != wgpu::ErrorType::NoError) {
                  pending->value = true;
                  describeFailure(pending->error, "GPU error scope", static_cast<unsigned>(type),
                                  message);
                }
              });
          auto completed = wait(m_instance, future, "GPU error scope");

          if (!completed && error.empty()) {
            error = completed.error().message;
          } else if (completed && pending->value && error.empty()) {
            error = std::move(pending->error);
          }
        }

        if (!error.empty()) {
          return Core::fail(Core::ErrorCode::External, std::move(error));
        }

        return {};
      }

    private:
      wgpu::Instance m_instance;
      wgpu::Device m_device;
      bool m_finished{};
    };
  }  // namespace

  struct Mesh::Impl {
    std::shared_ptr<Diagnostics> owner;
    wgpu::Buffer vertices;
    wgpu::Buffer indices;
    MeshInfo info{};
    Bounds bounds{};
    bool hasNormals{};
    bool hasNormalMapFrame{};
    std::uint64_t vertexBytes{};
    std::uint64_t indexBytes{};
    bool tracked{};

    ~Impl() {
      if (tracked) {
        owner->meshes.fetch_sub(1);
        owner->vertexBytes.fetch_sub(vertexBytes);
        owner->indexBytes.fetch_sub(indexBytes);
      }
    }
  };

  Mesh::Mesh() noexcept = default;

  Mesh::Mesh(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Mesh::Mesh(const Mesh&) noexcept = default;

  auto Mesh::operator=(const Mesh&) noexcept -> Mesh& = default;

  Mesh::Mesh(Mesh&&) noexcept = default;

  auto Mesh::operator=(Mesh&&) noexcept -> Mesh& = default;

  Mesh::~Mesh() = default;

  auto Mesh::info() const -> Core::Result<MeshInfo> {
    if (!m_impl || !m_impl->owner->alive.load()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Mesh has no live rendering device");
    }

    if (m_impl->owner->failed.load()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Mesh rendering device failed");
    }

    return m_impl->info;
  }

  struct RenderTarget::Impl {
    std::thread::id thread{std::this_thread::get_id()};
    std::shared_ptr<Diagnostics> owner;
    wgpu::Texture color;
    wgpu::Texture hdr;
    wgpu::TextureView view;
    wgpu::Texture depth;
    wgpu::TextureView depthView;
    TargetInfo info{};
    std::uint64_t bytes{};
    bool tracked{};
    bool faulted{};

    ~Impl() {
      if (tracked) {
        owner->targets.fetch_sub(1);
        owner->targetBytes.fetch_sub(bytes);
      }
    }
  };

  RenderTarget::RenderTarget() noexcept = default;

  RenderTarget::RenderTarget(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  RenderTarget::RenderTarget(const RenderTarget&) noexcept = default;

  auto RenderTarget::operator=(const RenderTarget&) noexcept -> RenderTarget& = default;

  RenderTarget::RenderTarget(RenderTarget&&) noexcept = default;

  auto RenderTarget::operator=(RenderTarget&&) noexcept -> RenderTarget& = default;

  RenderTarget::~RenderTarget() = default;

  auto RenderTarget::info() const -> Core::Result<TargetInfo> {
    if (!m_impl || !m_impl->owner->alive.load()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Render target has no live rendering device");
    }

    if (m_impl->thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Render target query requires its creating thread");
    }

    if (m_impl->faulted || m_impl->owner->failed.load()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Render target or device faulted");
    }

    return m_impl->info;
  }

  struct EnvironmentMap::Impl {
    std::thread::id thread{std::this_thread::get_id()};
    std::shared_ptr<Diagnostics> owner;
    std::array<wgpu::Texture, 4> textures;
    std::array<wgpu::TextureView, 4> views;
    wgpu::Sampler sampler;
    EnvironmentInfo description{};
    bool tracked{};

    ~Impl() {
      if (tracked) {
        owner->environments.fetch_sub(1);
        owner->environmentBytes.fetch_sub(description.bytes);
      }
    }
  };

  EnvironmentMap::EnvironmentMap() noexcept = default;

  EnvironmentMap::EnvironmentMap(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  EnvironmentMap::EnvironmentMap(const EnvironmentMap&) noexcept = default;

  auto EnvironmentMap::operator=(const EnvironmentMap&) noexcept -> EnvironmentMap& = default;

  EnvironmentMap::EnvironmentMap(EnvironmentMap&&) noexcept = default;

  auto EnvironmentMap::operator=(EnvironmentMap&&) noexcept -> EnvironmentMap& = default;

  EnvironmentMap::~EnvironmentMap() = default;

  auto EnvironmentMap::info() const -> Core::Result<EnvironmentInfo> {
    if (!m_impl || !m_impl->owner->alive.load()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Environment has no live rendering device");
    }

    if (m_impl->thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Environment query requires owner thread");
    }

    if (m_impl->owner->failed.load()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Environment rendering device failed");
    }

    return m_impl->description;
  }

  struct Texture::Impl {
    std::shared_ptr<Diagnostics> owner;
    std::thread::id thread{std::this_thread::get_id()};
    wgpu::Texture texture;
    wgpu::TextureView view;
    wgpu::Sampler sampler;
    TextureInfo info{};
    bool normalEncoding{};
    std::uint64_t bytes{};
    bool tracked{};

    ~Impl() {
      if (tracked) {
        owner->textures.fetch_sub(1);
        owner->textureBytes.fetch_sub(bytes);
      }
    }
  };

  Texture::Texture() noexcept = default;

  Texture::Texture(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Texture::Texture(const Texture&) noexcept = default;

  auto Texture::operator=(const Texture&) noexcept -> Texture& = default;

  Texture::Texture(Texture&&) noexcept = default;

  auto Texture::operator=(Texture&&) noexcept -> Texture& = default;

  Texture::~Texture() = default;

  auto Texture::info() const -> Core::Result<TextureInfo> {
    if (!m_impl || !m_impl->owner->alive.load()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Texture has no live rendering device");
    }

    if (m_impl->thread != std::this_thread::get_id()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Texture query requires its creating thread");
    }

    if (m_impl->owner->failed.load()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Texture device faulted");
    }

    return m_impl->info;
  }

  struct Renderer::Impl {
    std::thread::id thread{std::this_thread::get_id()};
    Platform::Window* window{};
    RendererConfig config;
    std::shared_ptr<Diagnostics> diagnostics{std::make_shared<Diagnostics>()};
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
    wgpu::Surface surface;
#if defined(__APPLE__)
    SDL_MetalView metalView{};
#endif
    AdapterInfo info{};
    wgpu::TextureFormat surfaceFormat{wgpu::TextureFormat::Undefined};
    wgpu::CompositeAlphaMode alphaMode{};
    std::uint32_t surfaceWidth{};
    std::uint32_t surfaceHeight{};
    wgpu::RenderPipeline scenePipeline;
    wgpu::RenderPipeline environmentPipeline;
    wgpu::RenderPipeline skyPipeline;
    wgpu::RenderPipeline shadowScenePipeline;
    wgpu::RenderPipeline shadowEnvironmentPipeline;
    wgpu::RenderPipeline shadowDepthPipeline;
    wgpu::Texture shadowTexture;
    std::optional<OcclusionSettings> occlusion;
    std::array<wgpu::Texture, 4> occlusionTextures;
    wgpu::RenderPipeline geometryPipeline;
    wgpu::RenderPipeline pickingPipeline;
    wgpu::ComputePipeline occlusionPipeline;
    wgpu::ComputePipeline occlusionFilterPipeline;
    wgpu::RenderPipeline occlusionEnvironmentPipeline;
    wgpu::RenderPipeline occlusionShadowEnvironmentPipeline;
    std::uint32_t occlusionWidth{};
    std::uint32_t occlusionHeight{};
    std::uint64_t occlusionStorageBytes{};
    std::uint64_t occlusionRevision{};
    bool occlusionReady{};
    bool occlusionPending{};
    std::uint32_t shadowResolution{};
    std::uint32_t shadowLayers{};
    std::uint64_t shadowBytes{};
    std::optional<EnvironmentLighting> environment;
    wgpu::RenderPipeline postPipeline;
    wgpu::RenderPipeline surfacePostPipeline;
    wgpu::Texture frameHdr;
    wgpu::Texture frameDepth;
    std::uint32_t frameWidth{};
    std::uint32_t frameHeight{};
    wgpu::Buffer uniform;
    wgpu::Texture whiteTexture;
    wgpu::TextureView whiteView;
    wgpu::Sampler whiteSampler;
    std::unique_ptr<Detail::GuiBackend> gui;
    std::vector<RenderTarget> guiTargets;

    ~Impl() {
      guiTargets.clear();
      gui.reset();
      diagnostics->alive.store(false);

      if (surface) {
        if (surfaceWidth != 0) {
          surface.Unconfigure();
        }

        surface = nullptr;
      }
#if defined(__APPLE__)
      if (metalView) {
        SDL_Metal_DestroyView(metalView);
      }
#endif
      if (device) {
        device.Destroy();
      }
    }

    auto check(bool allowGui = false) const -> Core::Result<void> {
      if (thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Renderer operations require its creating thread");
      }

      std::scoped_lock lock(diagnostics->mutex);

      if (!allowGui && gui && gui->building()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Renderer cannot reenter a GUI callback");
      }

      if (diagnostics->failed.load()) {
        return Core::fail(Core::ErrorCode::Unavailable, diagnostics->failure.empty()
                                                            ? "GPU device failed"
                                                            : diagnostics->failure);
      }

      return {};
    }

    auto createSurface() -> Core::Result<void> {
      if (!Platform::isMainThread() || !window->nativeHandle()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Surface requires a valid window on the main thread");
      }

      auto* nativeWindow = static_cast<SDL_Window*>(window->nativeHandle());
      wgpu::SurfaceDescriptor descriptor;
#if defined(__APPLE__)
      metalView = SDL_Metal_CreateView(nativeWindow);

      if (!metalView) {
        return Core::fail(Core::ErrorCode::External,
                          std::format("Create Metal view: {}", SDL_GetError()));
      }

      wgpu::SurfaceSourceMetalLayer source;
      source.layer = SDL_Metal_GetLayer(metalView);

      if (!source.layer) {
        return Core::fail(Core::ErrorCode::External, "Metal view has no layer");
      }

      descriptor.nextInChain = &source;
      surface = instance.CreateSurface(&descriptor);
#elif defined(_WIN32)
      auto properties = SDL_GetWindowProperties(nativeWindow);
      wgpu::SurfaceSourceWindowsHWND source;
      source.hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
      source.hinstance
          = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);

      if (!source.hwnd || !source.hinstance) {
        return Core::fail(Core::ErrorCode::External, "SDL window has no Win32 handles");
      }

      descriptor.nextInChain = &source;
      surface = instance.CreateSurface(&descriptor);
#else
      auto properties = SDL_GetWindowProperties(nativeWindow);
      auto* waylandDisplay
          = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);

      if (waylandDisplay) {
        wgpu::SurfaceSourceWaylandSurface source;
        source.display = waylandDisplay;
        source.surface
            = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);

        if (!source.surface) {
          return Core::fail(Core::ErrorCode::External, "SDL window has no Wayland surface");
        }

        descriptor.nextInChain = &source;
        surface = instance.CreateSurface(&descriptor);
      } else {
        wgpu::SurfaceSourceXlibWindow source;
        source.display
            = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        auto id = SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);

        if (!source.display || id <= 0) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "SDL window has no supported X11 or Wayland surface");
        }

        source.window = static_cast<std::uint64_t>(id);
        descriptor.nextInChain = &source;
        surface = instance.CreateSurface(&descriptor);
      }
#endif
      if (!surface) {
        return Core::fail(Core::ErrorCode::External, "Dawn could not create the native surface");
      }

      return {};
    }

    auto pipeline(wgpu::TextureFormat format, bool withEnvironment = false,
                  bool withShadows = false, bool withOcclusion = false) -> wgpu::RenderPipeline {
      wgpu::ShaderSourceWGSL source;
      const auto shaderCode
          = std::string(withEnvironment ? Detail::surfaceEnvironmentShader
                                        : Detail::surfaceDirectShader)
            + "\n"
            + (withShadows ? Detail::visibilityShadowsShader : Detail::visibilityDirectShader)
            + "\n"
            + (withOcclusion ? Detail::ambientOcclusionShader : Detail::ambientUnoccludedShader)
            + "\n" + Detail::surfaceShader;
      source.code = shaderCode.c_str();
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &source;
      auto shader = device.CreateShaderModule(&shaderDescriptor);
      const std::array attributes{wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, position),
                                                        .shaderLocation = 0},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4,
                                                        .offset = offsetof(Vertex, color),
                                                        .shaderLocation = 1},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2,
                                                        .offset = offsetof(Vertex, texCoord),
                                                        .shaderLocation = 2},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, normal),
                                                        .shaderLocation = 3}};
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.stepMode = wgpu::VertexStepMode::Vertex;
      layout.attributeCount = attributes.size();
      layout.attributes = attributes.data();
      wgpu::ColorTargetState target;
      target.format = format;
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth;
      depth.format = wgpu::TextureFormat::Depth24Plus;
      depth.depthWriteEnabled = true;
      depth.depthCompare = wgpu::CompareFunction::Less;
      wgpu::RenderPipelineDescriptor descriptor;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = "vertexMain";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &layout;
      descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
      descriptor.primitive.cullMode = wgpu::CullMode::None;
      descriptor.depthStencil = &depth;
      descriptor.fragment = &fragment;
      return device.CreateRenderPipeline(&descriptor);
    }

    auto postPipelineFor(wgpu::TextureFormat format) -> wgpu::RenderPipeline {
      wgpu::ShaderSourceWGSL source;
      source.code = Detail::toneMappingShader;
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &source;
      auto shader = device.CreateShaderModule(&shaderDescriptor);
      wgpu::ColorTargetState target;
      target.format = format;
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::RenderPipelineDescriptor descriptor;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = "vertexMain";
      descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
      descriptor.fragment = &fragment;
      return device.CreateRenderPipeline(&descriptor);
    }

    auto shadowSource() const -> std::optional<std::size_t> {
      for (std::size_t index = 0; index < config.lighting.lights.size(); ++index) {
        if (config.lighting.lights[index].shadows) {
          return index;
        }
      }

      return std::nullopt;
    }

    auto ensureShadowPipelines() -> Core::Result<void> {
      if (shadowDepthPipeline) {
        return {};
      }

      ErrorScope scope(instance, device);
      auto direct = pipeline(wgpu::TextureFormat::RGBA16Float, false, true);
      auto environmentSurface = pipeline(wgpu::TextureFormat::RGBA16Float, true, true);
      wgpu::ShaderSourceWGSL source;
      source.code = Detail::shadowDepthShader;
      wgpu::ShaderModuleDescriptor module;
      module.nextInChain = &source;
      auto shader = device.CreateShaderModule(&module);
      wgpu::VertexAttribute attribute;
      attribute.format = wgpu::VertexFormat::Float32x3;
      attribute.offset = offsetof(Vertex, position);
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.attributeCount = 1;
      layout.attributes = &attribute;
      wgpu::DepthStencilState depth;
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = true;
      depth.depthCompare = wgpu::CompareFunction::Less;
      wgpu::RenderPipelineDescriptor descriptor;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = "vertexMain";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &layout;
      descriptor.depthStencil = &depth;
      auto depthPipeline = device.CreateRenderPipeline(&descriptor);
      auto checked = scope.finish();

      if (!checked) {
        return checked;
      }

      if (!direct || !environmentSurface || !depthPipeline) {
        return Core::fail(Core::ErrorCode::External, "Create shadow pipelines failed");
      }

      shadowScenePipeline = std::move(direct);
      shadowEnvironmentPipeline = std::move(environmentSurface);
      shadowDepthPipeline = std::move(depthPipeline);
      return {};
    }

    auto ensureShadowMaps(const ShadowSettings& settings) -> Core::Result<void> {
      if (shadowResolution == settings.resolution && shadowLayers == settings.cascades) {
        return {};
      }

      if (settings.resolution > info.maxTextureDimension) {
        return Core::fail(Core::ErrorCode::Unavailable, "Shadow resolution exceeds device limits");
      }

      ErrorScope scope(instance, device);
      wgpu::TextureDescriptor descriptor;
      descriptor.size = {settings.resolution, settings.resolution, settings.cascades};
      descriptor.format = wgpu::TextureFormat::Depth32Float;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
      auto texture = device.CreateTexture(&descriptor);
      auto checked = scope.finish();

      if (!checked) {
        return checked;
      }

      if (!texture) {
        return Core::fail(Core::ErrorCode::External, "Allocate shadow depth maps failed");
      }

      shadowTexture = std::move(texture);
      shadowResolution = settings.resolution;
      shadowLayers = settings.cascades;
      shadowBytes
          = static_cast<std::uint64_t>(shadowResolution) * shadowResolution * shadowLayers * 4;
      return {};
    }

    auto ensurePickingPipeline() -> Core::Result<void> {
      if (pickingPipeline) {
        return {};
      }

      ErrorScope scope(instance, device);
      wgpu::ShaderSourceWGSL source;
      source.code = Detail::pickingShader;
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &source;
      auto shader = device.CreateShaderModule(&shaderDescriptor);
      wgpu::VertexAttribute attribute{.format = wgpu::VertexFormat::Float32x3,
                                      .offset = offsetof(Vertex, position),
                                      .shaderLocation = 0};
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.attributeCount = 1;
      layout.attributes = &attribute;
      wgpu::ColorTargetState target;
      target.format = wgpu::TextureFormat::R32Uint;
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth;
      depth.format = wgpu::TextureFormat::Depth24Plus;
      depth.depthWriteEnabled = true;
      depth.depthCompare = wgpu::CompareFunction::Less;
      wgpu::RenderPipelineDescriptor descriptor;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = "vertexMain";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &layout;
      descriptor.depthStencil = &depth;
      descriptor.fragment = &fragment;
      auto pipeline = device.CreateRenderPipeline(&descriptor);
      auto checked = scope.finish();

      if (!checked) {
        return checked;
      }

      if (!pipeline) {
        return Core::fail(Core::ErrorCode::External, "Create integer picking pipeline failed");
      }

      pickingPipeline = std::move(pipeline);
      return {};
    }

    auto ensureOcclusionPipelines() -> Core::Result<void> {
      if (geometryPipeline) {
        return {};
      }

      ErrorScope scope(instance, device);
      const auto module = [&](const char* code) {
        wgpu::ShaderSourceWGSL source;
        source.code = code;
        wgpu::ShaderModuleDescriptor descriptor;
        descriptor.nextInChain = &source;
        return device.CreateShaderModule(&descriptor);
      };
      auto shader = module(Detail::geometryShader);
      const std::array attributes{wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, position),
                                                        .shaderLocation = 0},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, normal),
                                                        .shaderLocation = 3}};
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.attributeCount = attributes.size();
      layout.attributes = attributes.data();
      wgpu::ColorTargetState target;
      target.format = wgpu::TextureFormat::RGBA16Float;
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth;
      depth.format = wgpu::TextureFormat::Depth32Float;
      depth.depthWriteEnabled = true;
      depth.depthCompare = wgpu::CompareFunction::Less;
      wgpu::RenderPipelineDescriptor descriptor;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = "vertexMain";
      descriptor.vertex.bufferCount = 1;
      descriptor.vertex.buffers = &layout;
      descriptor.depthStencil = &depth;
      descriptor.fragment = &fragment;
      auto geometry = device.CreateRenderPipeline(&descriptor);
      wgpu::ComputePipelineDescriptor compute;
      compute.compute.module = module(Detail::occlusionShader);
      compute.compute.entryPoint = "compute";
      auto ao = device.CreateComputePipeline(&compute);
      compute.compute.module = module(Detail::occlusionFilterShader);
      compute.compute.entryPoint = "filterOcclusion";
      auto filter = device.CreateComputePipeline(&compute);
      auto ambient = pipeline(wgpu::TextureFormat::RGBA16Float, true, false, true);
      auto shadowed = pipeline(wgpu::TextureFormat::RGBA16Float, true, true, true);
      auto checked = scope.finish();

      if (!checked) {
        return checked;
      }

      if (!geometry || !ao || !filter || !ambient || !shadowed) {
        return Core::fail(Core::ErrorCode::External, "Create occlusion pipelines failed");
      }

      geometryPipeline = std::move(geometry);
      occlusionPipeline = std::move(ao);
      occlusionFilterPipeline = std::move(filter);
      occlusionEnvironmentPipeline = std::move(ambient);
      occlusionShadowEnvironmentPipeline = std::move(shadowed);
      return {};
    }

    auto ensureOcclusionStorage(std::uint32_t width, std::uint32_t height) -> Core::Result<void> {
      auto size = Graphics::occlusionBytes(width, height);

      if (!size) {
        return std::unexpected(size.error());
      }

      if (width == occlusionWidth && height == occlusionHeight) {
        return {};
      }

      if (width > info.maxTextureDimension || height > info.maxTextureDimension) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Occlusion dimensions exceed device limits");
      }

      ErrorScope scope(instance, device);
      std::array<wgpu::Texture, 4> textures;
      const std::array formats{wgpu::TextureFormat::Depth32Float, wgpu::TextureFormat::RGBA16Float,
                               wgpu::TextureFormat::RGBA16Float, wgpu::TextureFormat::RGBA8Unorm};

      for (std::size_t index = 0; index < textures.size(); ++index) {
        wgpu::TextureDescriptor descriptor;
        descriptor.size = {width, height, 1};
        descriptor.format = formats[index];
        descriptor.usage = wgpu::TextureUsage::TextureBinding
                           | (index < 2 ? wgpu::TextureUsage::RenderAttachment
                                        : wgpu::TextureUsage::StorageBinding);

        if (index == 3) {
          descriptor.usage |= wgpu::TextureUsage::CopySrc;
        }

        textures[index] = device.CreateTexture(&descriptor);
      }

      auto checked = scope.finish();

      if (!checked) {
        return checked;
      }

      if (std::ranges::any_of(textures, [](const auto& texture) { return !texture; })) {
        return Core::fail(Core::ErrorCode::External, "Allocate occlusion working maps failed");
      }

      occlusionTextures = std::move(textures);
      occlusionWidth = width;
      occlusionHeight = height;
      occlusionStorageBytes = *size;
      occlusionReady = false;
      return {};
    }

    auto skyPipelineFor() -> wgpu::RenderPipeline {
      wgpu::ShaderSourceWGSL source;
      source.code = Detail::skyShader;
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &source;
      auto shader = device.CreateShaderModule(&shaderDescriptor);
      wgpu::ColorTargetState target;
      target.format = wgpu::TextureFormat::RGBA16Float;
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth;
      depth.format = wgpu::TextureFormat::Depth24Plus;
      depth.depthWriteEnabled = false;
      depth.depthCompare = wgpu::CompareFunction::Always;
      wgpu::RenderPipelineDescriptor descriptor;
      descriptor.vertex.module = shader;
      descriptor.vertex.entryPoint = "vertexMain";
      descriptor.fragment = &fragment;
      descriptor.depthStencil = &depth;
      return device.CreateRenderPipeline(&descriptor);
    }

    auto environmentValues() const -> Math::Vec4 {
      const auto radians = environment->rotationDegrees * 0.01745329252F;
      return {environment->intensity, std::cos(radians), std::sin(radians),
              static_cast<float>(environment->map.m_impl->description.specularLevels - 1)};
    }

    auto bindEnvironment(const wgpu::RenderPipeline& renderPipeline, bool withOcclusion = false)
        -> wgpu::BindGroup {
      const auto data = environmentValues();
      wgpu::BufferDescriptor descriptor;
      descriptor.size = sizeof(data);
      descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);
      queue.WriteBuffer(buffer, 0, &data, sizeof(data));
      const auto& map = *environment->map.m_impl;
      std::array<wgpu::BindGroupEntry, 6> entries{};
      entries[0].buffer = buffer;
      entries[0].size = sizeof(data);

      for (std::uint32_t index = 1; index < 4; ++index) {
        entries[index].binding = index;
        entries[index].textureView = map.views[index];
      }

      entries[4].binding = 4;
      entries[4].sampler = map.sampler;
      if (withOcclusion) {
        entries[5].binding = 5;
        entries[5].textureView = occlusionTextures[3].CreateView();
      }

      wgpu::BindGroupDescriptor group;
      group.layout = renderPipeline.GetBindGroupLayout(2);
      group.entryCount = withOcclusion ? 6 : 5;
      group.entries = entries.data();
      return device.CreateBindGroup(&group);
    }

    auto drawSky(const wgpu::RenderPassEncoder& pass, const Math::Mat4& inverse) -> void {
      struct SkyData {
        Math::Mat4 inverse;
        Math::Vec4 position;
        Math::Vec4 direction;
        Math::Vec4 values;
      };

      static_assert(sizeof(SkyData) == 112);
      const auto position = config.lighting.viewPosition;
      SkyData data{inverse, {position.x, position.y, position.z, 0}, {}, environmentValues()};

      if (config.lighting.viewDirection) {
        const auto direction = *config.lighting.viewDirection;
        data.direction = {direction.x, direction.y, direction.z, 1};
      }

      wgpu::BufferDescriptor descriptor;
      descriptor.size = sizeof(data);
      descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);
      queue.WriteBuffer(buffer, 0, &data, sizeof(data));
      const auto& map = *environment->map.m_impl;
      std::array<wgpu::BindGroupEntry, 3> entries{};
      entries[0].buffer = buffer;
      entries[0].size = sizeof(data);
      entries[1].binding = 1;
      entries[1].textureView = map.views[0];
      entries[2].binding = 2;
      entries[2].sampler = map.sampler;
      wgpu::BindGroupDescriptor group;
      group.layout = skyPipeline.GetBindGroupLayout(0);
      group.entryCount = entries.size();
      group.entries = entries.data();
      pass.SetPipeline(skyPipeline);
      pass.SetBindGroup(0, device.CreateBindGroup(&group));
      pass.Draw(3);
    }

    auto ensureFrame(std::uint32_t width, std::uint32_t height) -> Core::Result<void> {
      if (width == 0 || height == 0 || width > info.maxTextureDimension
          || height > info.maxTextureDimension
          || static_cast<std::uint64_t>(width) * height * 12 > 256 * 1024 * 1024) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "HDR working frame exceeds dimension/256 MiB budget");
      }

      if (width == frameWidth && height == frameHeight) {
        return {};
      }

      ErrorScope scope(instance, device);
      wgpu::TextureDescriptor descriptor;
      descriptor.size = {width, height, 1};
      descriptor.format = wgpu::TextureFormat::RGBA16Float;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
      auto hdr = device.CreateTexture(&descriptor);
      descriptor.format = wgpu::TextureFormat::Depth24Plus;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment;
      auto depth = device.CreateTexture(&descriptor);
      auto checked = scope.finish();

      if (!checked) {
        return checked;
      }

      if (!hdr || !depth) {
        return Core::fail(Core::ErrorCode::External, "Allocate HDR working frame failed");
      }

      frameHdr = std::move(hdr);
      frameDepth = std::move(depth);
      frameWidth = width;
      frameHeight = height;
      return {};
    }

    auto encodePost(const wgpu::CommandEncoder& encoder, const wgpu::Texture& hdr,
                    const wgpu::Texture& output) -> void {
      const auto format = output.GetFormat();
      const auto hardwareSrgb = format == wgpu::TextureFormat::RGBA8UnormSrgb
                                || format == wgpu::TextureFormat::BGRA8UnormSrgb;
      const auto srgb = config.postProcess.outputEncoding == ColorSpace::Srgb;
      const PostUniform data{std::exp2(config.postProcess.exposureEv),
                             static_cast<std::uint32_t>(config.postProcess.toneMap),
                             srgb ? (hardwareSrgb ? 0U : 1U) : (hardwareSrgb ? 2U : 0U)};
      wgpu::BufferDescriptor descriptor;
      descriptor.size = sizeof(data);
      descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);
      queue.WriteBuffer(buffer, 0, &data, sizeof(data));
      const auto& renderPipeline
          = format == wgpu::TextureFormat::RGBA8Unorm ? postPipeline : surfacePostPipeline;
      std::array<wgpu::BindGroupEntry, 2> entries{};
      entries[0].binding = 0;
      entries[0].textureView = hdr.CreateView();
      entries[1].binding = 1;
      entries[1].buffer = buffer;
      entries[1].size = sizeof(data);
      wgpu::BindGroupDescriptor groupDescriptor;
      groupDescriptor.layout = renderPipeline.GetBindGroupLayout(0);
      groupDescriptor.entryCount = entries.size();
      groupDescriptor.entries = entries.data();
      auto group = device.CreateBindGroup(&groupDescriptor);
      wgpu::RenderPassColorAttachment color;
      color.view = output.CreateView();
      color.loadOp = wgpu::LoadOp::Clear;
      color.storeOp = wgpu::StoreOp::Store;
      wgpu::RenderPassDescriptor passDescriptor;
      passDescriptor.colorAttachmentCount = 1;
      passDescriptor.colorAttachments = &color;
      auto pass = encoder.BeginRenderPass(&passDescriptor);
      pass.SetPipeline(renderPipeline);
      pass.SetBindGroup(0, group);
      pass.Draw(3);
      pass.End();
    }

    template <typename F>
    auto encodePass(const wgpu::Texture& target, std::uint32_t width, std::uint32_t height,
                    Math::Vec4 clear, F&& draw, wgpu::TextureView depthView = {},
                    wgpu::CommandEncoder encoder = {}) -> wgpu::CommandEncoder {
      wgpu::TextureDescriptor depthDescriptor;
      depthDescriptor.size = {width, height, 1};
      depthDescriptor.format = wgpu::TextureFormat::Depth24Plus;
      depthDescriptor.usage = wgpu::TextureUsage::RenderAttachment;
      wgpu::Texture depth;

      if (!depthView) {
        depth = device.CreateTexture(&depthDescriptor);
        depthView = depth.CreateView();
      }

      wgpu::RenderPassColorAttachment color;
      color.view = target.CreateView();
      color.loadOp = wgpu::LoadOp::Clear;
      color.storeOp = wgpu::StoreOp::Store;
      color.clearValue = {clear.x, clear.y, clear.z, clear.w};
      wgpu::RenderPassDepthStencilAttachment depthAttachment;
      depthAttachment.view = depthView;
      depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
      depthAttachment.depthStoreOp = wgpu::StoreOp::Discard;
      depthAttachment.depthClearValue = 1;
      wgpu::RenderPassDescriptor descriptor;
      descriptor.colorAttachmentCount = 1;
      descriptor.colorAttachments = &color;
      descriptor.depthStencilAttachment = &depthAttachment;
      if (!encoder) {
        encoder = device.CreateCommandEncoder();
      }
      auto pass = encoder.BeginRenderPass(&descriptor);

      draw(pass);
      pass.End();
      return encoder;
    }

    auto bindMaterial(const wgpu::RenderPipeline& renderPipeline, const wgpu::Buffer& buffer,
                      const std::array<std::shared_ptr<Texture::Impl>, 4>& textures = {})
        -> wgpu::BindGroup {
      std::array<wgpu::BindGroupEntry, 9> entries{};
      entries[0].binding = 0;
      entries[0].buffer = buffer;
      entries[0].size = sizeof(UniformData);

      for (std::size_t index = 0; index < textures.size(); ++index) {
        entries[index * 2 + 1].binding = static_cast<std::uint32_t>(index * 2 + 1);
        entries[index * 2 + 1].textureView = textures[index] ? textures[index]->view : whiteView;
        entries[index * 2 + 2].binding = static_cast<std::uint32_t>(index * 2 + 2);
        entries[index * 2 + 2].sampler = textures[index] ? textures[index]->sampler : whiteSampler;
      }

      wgpu::BindGroupDescriptor descriptor;
      descriptor.layout = renderPipeline.GetBindGroupLayout(0);
      descriptor.entryCount = entries.size();
      descriptor.entries = entries.data();
      return device.CreateBindGroup(&descriptor);
    }

    auto bindLighting(const wgpu::RenderPipeline& renderPipeline) -> wgpu::BindGroup {
      LightingData data;
      const auto position = config.lighting.viewPosition;
      data.viewPosition = {position.x, position.y, position.z, 0};
      data.meta.x = static_cast<float>(config.lighting.lights.size());

      if (config.lighting.viewDirection) {
        const auto direction = *config.lighting.viewDirection;
        data.meta.y = direction.x;
        data.meta.z = direction.y;
        data.meta.w = direction.z;
      }

      for (std::size_t index = 0; index < config.lighting.lights.size(); ++index) {
        const auto& light = config.lighting.lights[index];
        const auto direction = light.direction;
        const auto p = light.position;
        const auto c = light.color;
        data.lights[index]
            = {{direction.x, direction.y, direction.z, static_cast<float>(light.kind)},
               {p.x, p.y, p.z, light.range},
               {c.x, c.y, c.z, light.intensity},
               {std::cos(light.innerConeDegrees * 0.01745329252F),
                std::cos(light.outerConeDegrees * 0.01745329252F), 0, 0}};
      }

      wgpu::BufferDescriptor descriptor;
      descriptor.size = sizeof(data);
      descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);
      queue.WriteBuffer(buffer, 0, &data, sizeof(data));
      wgpu::BindGroupEntry entry;
      entry.buffer = buffer;
      entry.size = sizeof(data);
      wgpu::BindGroupDescriptor group;
      group.layout = renderPipeline.GetBindGroupLayout(1);
      group.entryCount = 1;
      group.entries = &entry;
      return device.CreateBindGroup(&group);
    }

    auto encode(const wgpu::Texture& target, std::span<const Vertex> vertices,
                const Math::Mat4& transform, std::uint32_t width, std::uint32_t height,
                Math::Vec4 clear) -> Core::Result<wgpu::CommandEncoder> {
      auto ready = ensureFrame(width, height);

      if (!ready) {
        return std::unexpected(ready.error());
      }

      const auto& renderPipeline = scenePipeline;
      auto encoder = encodePass(
          frameHdr, width, height, clear,
          [&](const wgpu::RenderPassEncoder& pass) {
            if (!vertices.empty()) {
              wgpu::BufferDescriptor bufferDescriptor;
              bufferDescriptor.size = vertices.size_bytes();
              bufferDescriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
              auto buffer = device.CreateBuffer(&bufferDescriptor);
              queue.WriteBuffer(buffer, 0, vertices.data(), vertices.size_bytes());
              const UniformData data{transform};
              queue.WriteBuffer(uniform, 0, &data, sizeof(data));
              auto group = bindMaterial(renderPipeline, uniform);
              pass.SetPipeline(renderPipeline);
              pass.SetBindGroup(0, group);
              pass.SetBindGroup(1, bindLighting(renderPipeline));
              pass.SetVertexBuffer(0, buffer);
              pass.Draw(static_cast<std::uint32_t>(vertices.size()));
            }
          },
          frameDepth.CreateView());
      encodePost(encoder, frameHdr, target);
      return encoder;
    }

    struct PreparedDraw {
      std::shared_ptr<Mesh::Impl> mesh;
      UniformData uniform;
      std::array<std::shared_ptr<Texture::Impl>, 4> textures;
      bool castsShadow;
    };

    auto prepare(std::span<const MeshDraw> draws, const Math::Mat4& viewProjection,
                 Math::Vec4 clear) -> Core::Result<std::vector<PreparedDraw>> {
      if (draws.size() > 4096 || !Math::isFinite(viewProjection) || !radianceValid(clear)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid mesh draw count, camera, or clear color");
      }

      std::vector<PreparedDraw> prepared;
      prepared.reserve(draws.size());

      for (const auto& draw : draws) {
        if (!draw.mesh || !draw.mesh->m_impl || draw.mesh->m_impl->owner != diagnostics) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Draw requires a live mesh uploaded by this renderer");
        }

        if (!colorValid(draw.tint)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Draw tint must be finite and in [0,1]");
        }

        if (!colorValid(draw.material.baseColor)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Material base color must be finite and in [0,1]");
        }

        const auto emissive = draw.material.emissive;

        if (!Math::isFinite(emissive) || emissive.x < 0 || emissive.x > 65503 || emissive.y < 0
            || emissive.y > 65503 || emissive.z < 0 || emissive.z > 65503) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Material emission must be finite in [0,65503]");
        }

        const auto& material = draw.material;

        if ((material.model != SurfaceModel::Unlit
             && material.model != SurfaceModel::MetallicRoughness)
            || !std::isfinite(material.metallic) || material.metallic < 0 || material.metallic > 1
            || !std::isfinite(material.roughness) || material.roughness < 0
            || material.roughness > 1 || !std::isfinite(material.normalScale)
            || material.normalScale < 0 || material.normalScale > 8) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid physical material properties");
        }

        if (material.model == SurfaceModel::Unlit
            && (material.metallicRoughnessTexture || material.normalTexture)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Physical data textures require a physical material");
        }

        std::array<std::shared_ptr<Texture::Impl>, 4> textures{};
        const std::array slots{&material.baseColorTexture, &material.metallicRoughnessTexture,
                               &material.normalTexture, &material.emissiveTexture};

        for (std::size_t index = 0; index < slots.size(); ++index) {
          if (*slots[index]) {
            textures[index] = (*slots[index])->m_impl;

            if (!textures[index] || textures[index]->owner != diagnostics
                || ((index == 1 || index == 2)
                    && textures[index]->info.options.colorSpace != ColorSpace::Linear)
                || (index == 2 && !textures[index]->normalEncoding)) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Invalid/foreign material texture or nonlinear data texture");
            }
          }
        }

        Math::Mat4 normalMatrix;
        float orientation = 1;

        if (material.model == SurfaceModel::MetallicRoughness) {
          if (!draw.mesh->m_impl->hasNormals
              || (material.normalTexture && !draw.mesh->m_impl->hasNormalMapFrame)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Physical mesh requires unit normals and valid normal-map UV faces");
          }

          const auto& m = draw.transform.data;

          if (!Math::isFinite(draw.transform) || m[3] != 0 || m[7] != 0 || m[11] != 0
              || m[15] != 1) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Physical world transform must be finite affine");
          }

          auto inverse = Math::inverse(draw.transform);

          if (!inverse) {
            return std::unexpected(inverse.error());
          }

          for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
              const auto value = inverse->data[row * 4 + column];

              if (std::abs(value) > 1e12) {
                return Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Physical normal matrix exceeds precision bounds");
              }

              normalMatrix.data[column * 4 + row] = value;
            }
          }

          double worldNorm{};
          double inverseNorm{};
          float normalMaximum{};

          for (std::size_t row = 0; row < 3; ++row) {
            double worldRow{};
            double inverseRow{};

            for (std::size_t column = 0; column < 3; ++column) {
              worldRow += std::abs(static_cast<double>(m[column * 4 + row]));
              inverseRow += std::abs(static_cast<double>(inverse->data[column * 4 + row]));
              normalMaximum
                  = std::max(normalMaximum, std::abs(normalMatrix.data[column * 4 + row]));
            }

            worldNorm = std::max(worldNorm, worldRow);
            inverseNorm = std::max(inverseNorm, inverseRow);
          }

          if (normalMaximum == 0 || worldNorm * inverseNorm > 1e20) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Physical transform exceeds relative precision bounds");
          }

          for (std::size_t column = 0; column < 3; ++column) {
            for (std::size_t row = 0; row < 3; ++row) {
              normalMatrix.data[column * 4 + row] /= normalMaximum;
            }
          }

          const auto& bounds = draw.mesh->m_impl->bounds;

          for (int corner = 0; corner < 8; ++corner) {
            auto position = Math::transformPoint(
                draw.transform, {corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                                 corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                                 corner & 4 ? bounds.maximum.z : bounds.minimum.z});

            if (!position || std::abs(position->x) > 1e6 || std::abs(position->y) > 1e6
                || std::abs(position->z) > 1e6) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Physical geometry exceeds world coordinate bounds");
            }
          }

          const auto determinant
              = static_cast<double>(m[0])
                    * (static_cast<double>(m[5]) * m[10] - static_cast<double>(m[9]) * m[6])
                - static_cast<double>(m[4])
                      * (static_cast<double>(m[1]) * m[10] - static_cast<double>(m[9]) * m[2])
                + static_cast<double>(m[8])
                      * (static_cast<double>(m[1]) * m[6] - static_cast<double>(m[5]) * m[2]);
          orientation = determinant < 0 ? -1.0F : 1.0F;
        }

        auto transform = Math::multiply(viewProjection, draw.transform);

        if (!transform) {
          return std::unexpected(transform.error());
        }

        const auto tint = draw.tint;
        const auto factor = draw.material.baseColor;
        prepared.push_back(
            {draw.mesh->m_impl,
             {*transform,
              {tint.x * factor.x, tint.y * factor.y, tint.z * factor.z, tint.w * factor.w},
              {emissive.x, emissive.y, emissive.z, 0},
              draw.transform,
              normalMatrix,
              {material.metallic, material.roughness, material.normalScale,
               draw.receivesShadow ? 1.0F : 0.0F},
              {material.model == SurfaceModel::MetallicRoughness ? 1.0F : 0.0F,
               material.normalTexture ? 1.0F : 0.0F, material.doubleSided ? 1.0F : 0.0F,
               orientation}},
             std::move(textures),
             draw.castsShadow});
      }

      return prepared;
    }

    struct ShadowData {
      std::array<Math::Mat4, 4> matrices;
      std::array<float, 4> splits;
      Math::Vec4 origin;
      Math::Vec4 forward;
      Math::Vec4 bias;
      Math::Vec4 meta;
    };

    static_assert(sizeof(ShadowData) == 336);

    auto fitShadows(std::span<const PreparedDraw> draws, const Math::Mat4& camera,
                    std::size_t source) -> Core::Result<ShadowCascades> {
      std::vector<Bounds> casters;
      casters.reserve(draws.size());

      for (const auto& draw : draws) {
        if (!draw.castsShadow) {
          continue;
        }

        const auto& matrix = draw.uniform.model;
        const auto& m = matrix.data;

        if (!Math::isFinite(matrix) || m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1
            || !Math::inverse(matrix)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Shadow caster requires finite affine transform");
        }

        Bounds bounds{{1e6F, 1e6F, 1e6F}, {-1e6F, -1e6F, -1e6F}};
        const auto& local = draw.mesh->bounds;

        for (int index = 0; index < 8; ++index) {
          auto point
              = Math::transformPoint(matrix, {index & 1 ? local.maximum.x : local.minimum.x,
                                              index & 2 ? local.maximum.y : local.minimum.y,
                                              index & 4 ? local.maximum.z : local.minimum.z});

          if (!point || std::abs(point->x) > 1e6 || std::abs(point->y) > 1e6
              || std::abs(point->z) > 1e6) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Shadow caster exceeds world bounds");
          }

          bounds.minimum
              = {std::min(bounds.minimum.x, point->x), std::min(bounds.minimum.y, point->y),
                 std::min(bounds.minimum.z, point->z)};
          bounds.maximum
              = {std::max(bounds.maximum.x, point->x), std::max(bounds.maximum.y, point->y),
                 std::max(bounds.maximum.z, point->z)};
        }

        casters.push_back(bounds);
      }

      const auto& light = config.lighting.lights[source];
      return fitDirectionalShadows(camera, config.lighting.viewPosition, light.direction,
                                   *light.shadows, casters);
    }

    auto encodeShadows(const wgpu::CommandEncoder& encoder, std::span<const PreparedDraw> draws,
                       const ShadowCascades& fit) -> Core::Result<void> {
      std::vector<Math::Mat4> matrices;
      matrices.reserve(draws.size() * fit.count);

      for (std::uint32_t cascade = 0; cascade < fit.count; ++cascade) {
        for (const auto& draw : draws) {
          if (!draw.castsShadow) {
            matrices.emplace_back();
            continue;
          }

          auto matrix = Math::multiply(fit.matrices[cascade], draw.uniform.model);

          if (!matrix) {
            return Core::fail(matrix.error().code,
                              "Shadow caster matrix: " + matrix.error().message);
          }

          matrices.push_back(*matrix);
        }
      }

      for (std::uint32_t cascade = 0; cascade < fit.count; ++cascade) {
        wgpu::TextureViewDescriptor view;
        view.dimension = wgpu::TextureViewDimension::e2D;
        view.baseArrayLayer = cascade;
        view.arrayLayerCount = 1;
        view.aspect = wgpu::TextureAspect::DepthOnly;
        wgpu::RenderPassDepthStencilAttachment depth;
        depth.view = shadowTexture.CreateView(&view);
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.depthStencilAttachment = &depth;
        auto pass = encoder.BeginRenderPass(&descriptor);
        pass.SetPipeline(shadowDepthPipeline);

        for (std::size_t index = 0; index < draws.size(); ++index) {
          const auto& draw = draws[index];

          if (!draw.castsShadow) {
            continue;
          }

          const auto& matrix = matrices[cascade * draws.size() + index];
          wgpu::BufferDescriptor bufferDescriptor;
          bufferDescriptor.size = sizeof(matrix);
          bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
          auto buffer = device.CreateBuffer(&bufferDescriptor);
          queue.WriteBuffer(buffer, 0, &matrix, sizeof(matrix));
          wgpu::BindGroupEntry entry;
          entry.buffer = buffer;
          entry.size = sizeof(matrix);
          wgpu::BindGroupDescriptor group;
          group.layout = shadowDepthPipeline.GetBindGroupLayout(0);
          group.entryCount = 1;
          group.entries = &entry;
          pass.SetBindGroup(0, device.CreateBindGroup(&group));
          pass.SetVertexBuffer(0, draw.mesh->vertices);

          if (draw.mesh->info.indices) {
            pass.SetIndexBuffer(draw.mesh->indices, wgpu::IndexFormat::Uint32);
            pass.DrawIndexed(draw.mesh->info.indices);
          } else {
            pass.Draw(draw.mesh->info.vertices);
          }
        }

        pass.End();
      }

      return {};
    }

    auto bindShadows(const wgpu::RenderPipeline& pipeline, const ShadowCascades& fit,
                     std::size_t source) -> wgpu::BindGroup {
      const auto& settings = *config.lighting.lights[source].shadows;
      const ShadowData data{
          fit.matrices,
          fit.splits,
          {fit.origin.x, fit.origin.y, fit.origin.z, fit.nearDepth},
          {fit.forward.x, fit.forward.y, fit.forward.z, 0},
          {settings.depthBias, settings.slopeBias, settings.filterRadius, settings.normalBias},
          {static_cast<float>(source), static_cast<float>(settings.resolution),
           static_cast<float>(fit.count), 0}};
      wgpu::BufferDescriptor descriptor;
      descriptor.size = sizeof(data);
      descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);
      queue.WriteBuffer(buffer, 0, &data, sizeof(data));
      wgpu::SamplerDescriptor sampler;
      sampler.compare = wgpu::CompareFunction::LessEqual;
      sampler.minFilter = sampler.magFilter = wgpu::FilterMode::Linear;
      wgpu::TextureViewDescriptor view;
      view.dimension = wgpu::TextureViewDimension::e2DArray;
      view.aspect = wgpu::TextureAspect::DepthOnly;
      std::array<wgpu::BindGroupEntry, 3> entries{};
      entries[0].buffer = buffer;
      entries[0].size = sizeof(data);
      entries[1].binding = 1;
      entries[1].textureView = shadowTexture.CreateView(&view);
      entries[2].binding = 2;
      entries[2].sampler = device.CreateSampler(&sampler);
      wgpu::BindGroupDescriptor group;
      group.layout = pipeline.GetBindGroupLayout(3);
      group.entryCount = entries.size();
      group.entries = entries.data();
      return device.CreateBindGroup(&group);
    }

    struct OcclusionData {
      Math::Mat4 viewProjection;
      Math::Mat4 inverse;
      Math::Vec4 right;
      Math::Vec4 up;
      Math::Vec4 values;
      std::array<std::uint32_t, 4> work;
    };

    static_assert(sizeof(OcclusionData) == 192);

    auto validateOcclusionGeometry(std::span<const PreparedDraw> draws) -> Core::Result<void> {
      for (const auto& draw : draws) {
        const auto& m = draw.uniform.model.data;

        if (!Math::isFinite(draw.uniform.model) || m[3] != 0 || m[7] != 0 || m[11] != 0
            || m[15] != 1) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Occlusion geometry requires affine world transforms");
        }

        const auto& bounds = draw.mesh->bounds;

        for (int corner = 0; corner < 8; ++corner) {
          auto position = Math::transformPoint(draw.uniform.model,
                                               {corner & 1 ? bounds.maximum.x : bounds.minimum.x,
                                                corner & 2 ? bounds.maximum.y : bounds.minimum.y,
                                                corner & 4 ? bounds.maximum.z : bounds.minimum.z});

          if (!position || std::abs(position->x) > 1e6 || std::abs(position->y) > 1e6
              || std::abs(position->z) > 1e6) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Occlusion geometry exceeds world bounds");
          }
        }
      }

      return {};
    }

    auto encodeOcclusion(const wgpu::CommandEncoder& encoder, std::span<const PreparedDraw> draws,
                         const OcclusionCamera& camera) -> Core::Result<void> {
      wgpu::RenderPassColorAttachment normal;
      normal.view = occlusionTextures[1].CreateView();
      normal.loadOp = wgpu::LoadOp::Clear;
      normal.storeOp = wgpu::StoreOp::Store;
      wgpu::TextureViewDescriptor depthView;
      depthView.aspect = wgpu::TextureAspect::DepthOnly;
      wgpu::RenderPassDepthStencilAttachment depth;
      depth.view = occlusionTextures[0].CreateView(&depthView);
      depth.depthLoadOp = wgpu::LoadOp::Clear;
      depth.depthStoreOp = wgpu::StoreOp::Store;
      depth.depthClearValue = 1;
      wgpu::RenderPassDescriptor descriptor;
      descriptor.colorAttachmentCount = 1;
      descriptor.colorAttachments = &normal;
      descriptor.depthStencilAttachment = &depth;
      auto pass = encoder.BeginRenderPass(&descriptor);
      pass.SetPipeline(geometryPipeline);
      pass.SetBindGroup(1, bindLighting(geometryPipeline));

      for (const auto& draw : draws) {
        wgpu::BufferDescriptor bufferDescriptor;
        bufferDescriptor.size = sizeof(UniformData);
        bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        auto buffer = device.CreateBuffer(&bufferDescriptor);
        queue.WriteBuffer(buffer, 0, &draw.uniform, sizeof(draw.uniform));
        wgpu::BindGroupEntry entry;
        entry.buffer = buffer;
        entry.size = sizeof(draw.uniform);
        wgpu::BindGroupDescriptor group;
        group.layout = geometryPipeline.GetBindGroupLayout(0);
        group.entryCount = 1;
        group.entries = &entry;
        pass.SetBindGroup(0, device.CreateBindGroup(&group));
        pass.SetVertexBuffer(0, draw.mesh->vertices);

        if (draw.mesh->info.indices) {
          pass.SetIndexBuffer(draw.mesh->indices, wgpu::IndexFormat::Uint32);
          pass.DrawIndexed(draw.mesh->info.indices);
        } else {
          pass.Draw(draw.mesh->info.vertices);
        }
      }

      pass.End();
      const auto& s = *occlusion;
      const OcclusionData data{camera.viewProjection,
                               camera.inverse,
                               {camera.right.x, camera.right.y, camera.right.z, 0},
                               {camera.up.x, camera.up.y, camera.up.z, 0},
                               {s.radius, s.bias, s.strength, 0},
                               {s.directions, s.steps, s.filterRadius, 0}};
      wgpu::BufferDescriptor bufferDescriptor;
      bufferDescriptor.size = sizeof(data);
      bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
      auto uniformData = device.CreateBuffer(&bufferDescriptor);
      queue.WriteBuffer(uniformData, 0, &data, sizeof(data));

      for (std::uint32_t phase = 0; phase < 2; ++phase) {
        const auto& pipeline = phase == 0 ? occlusionPipeline : occlusionFilterPipeline;
        std::array<wgpu::BindGroupEntry, 5> entries{};
        entries[0].buffer = uniformData;
        entries[0].size = sizeof(data);
        entries[1].binding = 1;
        entries[1].textureView = occlusionTextures[0].CreateView(&depthView);
        entries[2].binding = 2;
        entries[2].textureView = occlusionTextures[1].CreateView();
        entries[3].binding = 3;
        entries[3].textureView = occlusionTextures[2 + phase].CreateView();

        if (phase == 1) {
          entries[3].textureView = occlusionTextures[2].CreateView();
          entries[4].binding = 4;
          entries[4].textureView = occlusionTextures[3].CreateView();
        }

        wgpu::BindGroupDescriptor group;
        group.layout = pipeline.GetBindGroupLayout(0);
        group.entryCount = phase == 0 ? 4 : 5;
        group.entries = entries.data();
        auto compute = encoder.BeginComputePass();
        compute.SetPipeline(pipeline);
        compute.SetBindGroup(0, device.CreateBindGroup(&group));
        compute.DispatchWorkgroups((occlusionWidth + 7) / 8, (occlusionHeight + 7) / 8);
        compute.End();
      }

      occlusionPending = true;
      return {};
    }

    auto encodeMeshes(const wgpu::Texture& target, std::span<const PreparedDraw> draws,
                      const Math::Mat4& viewProjection, std::uint32_t width, std::uint32_t height,
                      Math::Vec4 clear, wgpu::Texture hdr = {}, wgpu::TextureView depthView = {})
        -> Core::Result<wgpu::CommandEncoder> {
      occlusionPending = false;

      if (!hdr) {
        auto ready = ensureFrame(width, height);

        if (!ready) {
          return std::unexpected(ready.error());
        }

        hdr = frameHdr;
        depthView = frameDepth.CreateView();
      }

      Math::Mat4 inverse;

      if (environment && environment->background) {
        auto result = Math::inverse(viewProjection);

        if (!result) {
          return std::unexpected(result.error());
        }

        inverse = *result;

        if (!config.lighting.viewDirection) {
          const auto& m = viewProjection.data;
          const auto p = config.lighting.viewPosition;
          const auto z = static_cast<double>(m[2]) * p.x + static_cast<double>(m[6]) * p.y
                         + static_cast<double>(m[10]) * p.z + m[14];
          const auto w = static_cast<double>(m[3]) * p.x + static_cast<double>(m[7]) * p.y
                         + static_cast<double>(m[11]) * p.z + m[15];

          if (std::abs(z - 0.5 * w) <= 0.0000001 * std::max(std::abs(z), std::abs(w))) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Sky camera lies on its ray reconstruction plane");
          }
        }

        std::optional<bool> negativeWeight;

        for (const auto x : {-1.0F, 1.0F}) {
          for (const auto y : {-1.0F, 1.0F}) {
            const auto& m = inverse.data;
            const auto weight = static_cast<double>(m[3]) * x + static_cast<double>(m[7]) * y
                                + static_cast<double>(m[11]) * 0.5 + m[15];

            if (negativeWeight && *negativeWeight != std::signbit(weight)) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Sky clip plane crosses a homogeneous horizon");
            }

            negativeWeight = std::signbit(weight);
            auto point = Math::transformPoint(inverse, {x, y, 0.5F});

            if (!point
                || (!config.lighting.viewDirection
                    && !Math::normalized(*point - config.lighting.viewPosition))) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Sky requires finite, nonzero camera rays");
            }
          }
        }
      }

      const auto source = shadowSource();
      std::optional<ShadowCascades> fit;
      wgpu::CommandEncoder shadowEncoder;

      if (source) {
        auto matrices = fitShadows(draws, viewProjection, *source);

        if (!matrices) {
          return std::unexpected(matrices.error());
        }

        auto checked = ensureShadowPipelines();

        if (!checked) {
          return std::unexpected(checked.error());
        }

        checked = ensureShadowMaps(*config.lighting.lights[*source].shadows);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        fit = *matrices;
        shadowEncoder = device.CreateCommandEncoder();
        checked = encodeShadows(shadowEncoder, draws, *fit);

        if (!checked) {
          return std::unexpected(checked.error());
        }
      }

      if (occlusion) {
        auto camera = Graphics::occlusionCamera(viewProjection);

        if (!camera) {
          return std::unexpected(camera.error());
        }

        auto checked = validateOcclusionGeometry(draws);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        checked = ensureOcclusionPipelines();

        if (!checked) {
          return std::unexpected(checked.error());
        }

        checked = ensureOcclusionStorage(width, height);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        if (!shadowEncoder) {
          shadowEncoder = device.CreateCommandEncoder();
        }

        checked = encodeOcclusion(shadowEncoder, draws, *camera);

        if (!checked) {
          return std::unexpected(checked.error());
        }
      }

      const auto& renderPipeline
          = occlusion && environment
                ? (source ? occlusionShadowEnvironmentPipeline : occlusionEnvironmentPipeline)
                : (source ? (environment ? shadowEnvironmentPipeline : shadowScenePipeline)
                          : (environment ? environmentPipeline : scenePipeline));
      auto encoder = encodePass(
          hdr, width, height, clear,
          [&](const wgpu::RenderPassEncoder& pass) {
            if (environment && environment->background) {
              drawSky(pass, inverse);
            }

            if (environment) {
              pass.SetBindGroup(2, bindEnvironment(renderPipeline, occlusion.has_value()));
            }

            if (fit) {
              pass.SetBindGroup(3, bindShadows(renderPipeline, *fit, *source));
            }

            auto lights = bindLighting(renderPipeline);
            pass.SetBindGroup(1, lights);

            for (const auto& draw : draws) {
              /** Queue writes precede submission, so a shared uniform would overwrite
               * earlier draws. The command buffer retains each independent buffer.
               * 队列写入先于提交，共用 uniform 会覆盖较早对象的数据。
               * 命令缓冲区会保留每个独立缓冲区的引用。
               */
              wgpu::BufferDescriptor descriptor;
              descriptor.size = sizeof(UniformData);
              descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
              auto drawUniform = device.CreateBuffer(&descriptor);
              queue.WriteBuffer(drawUniform, 0, &draw.uniform, sizeof(draw.uniform));
              auto group = bindMaterial(renderPipeline, drawUniform, draw.textures);
              pass.SetPipeline(renderPipeline);
              pass.SetBindGroup(0, group);
              pass.SetVertexBuffer(0, draw.mesh->vertices);

              if (draw.mesh->info.indices != 0) {
                pass.SetIndexBuffer(draw.mesh->indices, wgpu::IndexFormat::Uint32);
                pass.DrawIndexed(draw.mesh->info.indices);
              } else {
                pass.Draw(draw.mesh->info.vertices);
              }
            }
          },
          depthView, shadowEncoder);
      encodePost(encoder, hdr, target);
      return encoder;
    }

    auto configureSurface(std::uint32_t width, std::uint32_t height) -> Core::Result<void> {
      ErrorScope scope(instance, device);
      wgpu::SurfaceConfiguration configuration;
      configuration.device = device;
      configuration.format = surfaceFormat;
      configuration.width = width;
      configuration.height = height;
      configuration.presentMode = wgpu::PresentMode::Fifo;
      configuration.alphaMode = alphaMode;
      surface.Configure(&configuration);
      auto result = scope.finish();

      if (!result) {
        return result;
      }

      surfaceWidth = width;
      surfaceHeight = height;
      return {};
    }

    template <typename Readback = Image, typename F>
    auto readback(std::uint32_t width, std::uint32_t height, F&& encodeTarget,
                  wgpu::Texture existing = {},
                  wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm,
                  std::uint32_t layer = 0, std::uint32_t mip = 0, bool commitOcclusion = false)
        -> Core::Result<Readback> {
      const auto maximum = std::min(maxReadbackDimension, info.maxTextureDimension);

      if (width == 0 || height == 0 || width > maximum || height > maximum) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          std::format("Readback dimensions must be in [1, {}]", maximum));
      }

      ErrorScope scope(instance, device);
      wgpu::TextureDescriptor descriptor;
      descriptor.size = {width, height, 1};
      descriptor.format = format;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
      auto texture = existing ? std::move(existing) : device.CreateTexture(&descriptor);
      auto encoded = encodeTarget(texture, width, height);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto encoder = *encoded;
      constexpr auto hdr = std::is_same_v<Readback, HdrImage>;
      const auto rowBytes = width * (hdr ? 8U : 4U);
      const auto paddedRowBytes = (rowBytes + 255U) & ~255U;
      const auto bufferBytes = static_cast<std::size_t>(paddedRowBytes) * height;
      wgpu::BufferDescriptor bufferDescriptor;
      bufferDescriptor.size = bufferBytes;
      bufferDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
      auto buffer = device.CreateBuffer(&bufferDescriptor);
      wgpu::TexelCopyTextureInfo source;
      source.texture = texture;
      source.origin.z = layer;
      source.mipLevel = mip;
      wgpu::TexelCopyBufferInfo destination;
      destination.buffer = buffer;
      destination.layout.bytesPerRow = paddedRowBytes;
      destination.layout.rowsPerImage = height;
      wgpu::Extent3D extent{width, height, 1};
      encoder.CopyTextureToBuffer(&source, &destination, &extent);
      auto commands = encoder.Finish();
      queue.Submit(1, &commands);
      auto checked = scope.finish();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (commitOcclusion && occlusionPending) {
        occlusionReady = true;
        occlusionPending = false;
      }

      auto pending = std::make_shared<Pending<bool>>();
      pending->error = "GPU buffer mapping failed";
      auto future = buffer.MapAsync(
          wgpu::MapMode::Read, 0, bufferBytes, wgpu::CallbackMode::WaitAnyOnly,
          [pending](wgpu::MapAsyncStatus status, wgpu::StringView message) noexcept {
            pending->value = status == wgpu::MapAsyncStatus::Success;

            if (!pending->value) {
              describeFailure(pending->error, "GPU readback", static_cast<unsigned>(status),
                              message);
            }
          });
      checked = wait(instance, future, "Readback");

      if (!checked) {
        buffer.Destroy();
        return std::unexpected(checked.error());
      }

      if (!pending->value) {
        return Core::fail(Core::ErrorCode::External, pending->error);
      }

      auto* bytes = static_cast<const std::uint8_t*>(buffer.GetConstMappedRange(0, bufferBytes));

      if (!bytes) {
        buffer.Unmap();
        return Core::fail(Core::ErrorCode::External, "GPU readback returned no mapped bytes");
      }

      auto mapped = true;

      try {
        Readback image{width, height, {}};
        image.pixels.resize(static_cast<std::size_t>(width) * height * 4);

        for (std::size_t row = 0; row < height; ++row) {
          if constexpr (hdr) {
            for (std::size_t component = 0; component < static_cast<std::size_t>(width) * 4;
                 ++component) {
              std::uint16_t bits{};
              std::memcpy(&bits, bytes + row * paddedRowBytes + component * 2, 2);
              image.pixels[row * width * 4 + component] = halfFloat(bits);
            }
          } else {
            std::memcpy(image.pixels.data() + row * rowBytes, bytes + row * paddedRowBytes,
                        rowBytes);
          }
        }

        buffer.Unmap();
        mapped = false;

        if constexpr (hdr) {
          auto valid = validateImage(image);

          if (!valid) {
            return Core::fail(Core::ErrorCode::External,
                              "Invalid HDR readback: " + valid.error().message);
          }
        } else {
          if (format == wgpu::TextureFormat::BGRA8Unorm
              || format == wgpu::TextureFormat::BGRA8UnormSrgb) {
            for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
              std::swap(image.pixels[offset], image.pixels[offset + 2]);
            }
          }
        }

        return image;
      } catch (const std::exception& error) {
        if (mapped) {
          buffer.Unmap();
        }

        return Core::fail(Core::ErrorCode::External,
                          "Readback allocation: " + std::string(error.what()));
      }
    }

    auto encodeGui(const wgpu::Texture& target) -> wgpu::CommandEncoder {
      wgpu::RenderPassColorAttachment color;
      color.view = target.CreateView();
      color.loadOp = wgpu::LoadOp::Clear;
      color.storeOp = wgpu::StoreOp::Store;
      color.clearValue = {0.04, 0.04, 0.04, 1};
      wgpu::RenderPassDescriptor descriptor;
      descriptor.colorAttachmentCount = 1;
      descriptor.colorAttachments = &color;
      auto encoder = device.CreateCommandEncoder();
      auto pass = encoder.BeginRenderPass(&descriptor);
      gui->encode(pass.Get());
      pass.End();
      return encoder;
    }

    template <typename F>
    auto presentFrame(F&& encodeTarget, bool commitOcclusion = false)
        -> Core::Result<PresentResult> {
      if (!surface) {
        return Core::fail(Core::ErrorCode::InvalidState, "Renderer has no native surface");
      }

      auto checked = Core::Result<void>{};
      auto size = window->pixelSize();

      if (!size) {
        return std::unexpected(size.error());
      }

      if (size->width <= 0 || size->height <= 0) {
        return PresentResult::Deferred;
      }

      const auto width = static_cast<std::uint32_t>(size->width);
      const auto height = static_cast<std::uint32_t>(size->height);

      if (width > info.maxTextureDimension || height > info.maxTextureDimension) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Surface exceeds GPU texture limits");
      }

      if (surfaceWidth != width || surfaceHeight != height) {
        checked = configureSurface(width, height);

        if (!checked) {
          return std::unexpected(checked.error());
        }
      }

      wgpu::SurfaceTexture frame;
      surface.GetCurrentTexture(&frame);

      switch (frame.status) {
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
          break;
        case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
          return PresentResult::Deferred;
        case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
        case wgpu::SurfaceGetCurrentTextureStatus::Lost:
          checked = configureSurface(width, height);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          return PresentResult::Deferred;
        default:
          return Core::fail(Core::ErrorCode::External,
                            std::format("Acquire surface texture failed ({})",
                                        static_cast<unsigned>(frame.status)));
      }

      if (!frame.texture) {
        return Core::fail(Core::ErrorCode::External, "Surface returned no texture");
      }

      ErrorScope scope(instance, device);
      auto encoder = encodeTarget(frame.texture, width, height);

      if (!encoder) {
        return std::unexpected(encoder.error());
      }

      auto commands = encoder->Finish();
      queue.Submit(1, &commands);
      checked = scope.finish();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (surface.Present() != wgpu::Status::Success) {
        return Core::fail(Core::ErrorCode::External, "Present surface failed");
      }

      instance.ProcessEvents();
      checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (commitOcclusion && occlusionPending) {
        occlusionReady = true;
        occlusionPending = false;
      }

      return PresentResult::Presented;
    }
  };

  Renderer::Renderer(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Renderer::Renderer(Renderer&&) noexcept = default;

  auto Renderer::operator=(Renderer&&) noexcept -> Renderer& = default;

  Renderer::~Renderer() = default;

  auto Renderer::create(Platform::Window* window, const RendererConfig& config)
      -> Core::Result<Renderer> {
    if (config.maxTextures == 0 || config.maxTextures > 4096 || config.maxTextureBytes == 0
        || config.maxTextureBytes > 512 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid texture residency budget");
    }

    auto settings = validatePostProcess(config.postProcess);

    if (!settings) {
      return std::unexpected(settings.error());
    }

    auto lighting = validatedLighting(config.lighting);

    if (!lighting) {
      return std::unexpected(lighting.error());
    }

    auto impl = std::make_unique<Impl>();
    impl->window = window;
    impl->config = config;
    impl->config.lighting = std::move(*lighting);
    const auto feature = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &feature;
    impl->instance = wgpu::CreateInstance(&instanceDescriptor);

    if (!impl->instance) {
      return Core::fail(Core::ErrorCode::Unavailable, "Dawn instance creation failed");
    }

    if (window) {
      auto created = impl->createSurface();

      if (!created) {
        return std::unexpected(created.error());
      }
    }

    wgpu::RequestAdapterOptions options;
#if defined(__APPLE__)
    options.backendType = wgpu::BackendType::Metal;
    impl->info.backend = Backend::Metal;
#elif defined(_WIN32)
    options.backendType = wgpu::BackendType::D3D12;
    impl->info.backend = Backend::D3D12;
#else
    options.backendType = wgpu::BackendType::Vulkan;
    impl->info.backend = Backend::Vulkan;
#endif
    options.compatibleSurface = impl->surface;
    auto pendingAdapter = std::make_shared<Pending<wgpu::Adapter>>();
    pendingAdapter->error = "GPU adapter request failed";
    auto adapterFuture = impl->instance.RequestAdapter(
        &options, wgpu::CallbackMode::WaitAnyOnly,
        [pendingAdapter](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter,
                         wgpu::StringView message) noexcept {
          if (status == wgpu::RequestAdapterStatus::Success) {
            pendingAdapter->value = std::move(adapter);
          } else {
            describeFailure(pendingAdapter->error, "Request GPU adapter",
                            static_cast<unsigned>(status), message);
          }
        });
    auto completed = wait(impl->instance, adapterFuture, "Request adapter");

    if (!completed) {
      return std::unexpected(completed.error());
    }

    if (!pendingAdapter->value) {
      return Core::fail(Core::ErrorCode::Unavailable, pendingAdapter->error);
    }

    impl->adapter = std::move(pendingAdapter->value);
    wgpu::AdapterInfo adapterInfo;

    if (impl->adapter.GetInfo(&adapterInfo) != wgpu::Status::Success) {
      return Core::fail(Core::ErrorCode::External, "Cannot query GPU adapter information");
    }

    impl->info.name = copied(adapterInfo.device);
    impl->info.description = copied(adapterInfo.description);
    wgpu::DeviceDescriptor deviceDescriptor;
    auto diagnostics = impl->diagnostics;
    deviceDescriptor.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [diagnostics](const wgpu::Device&, wgpu::DeviceLostReason reason,
                      wgpu::StringView message) noexcept {
          if (reason != wgpu::DeviceLostReason::Destroyed
              && reason != wgpu::DeviceLostReason::CallbackCancelled) {
            diagnostics->record("GPU device lost", static_cast<unsigned>(reason), message);
          }
        });
    deviceDescriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message,
           Diagnostics* state) noexcept {
          state->record("Uncaptured GPU error", static_cast<unsigned>(type), message);
        },
        diagnostics.get());
    auto pendingDevice = std::make_shared<Pending<wgpu::Device>>();
    pendingDevice->error = "GPU device request failed";
    pendingDevice->keepAlive = diagnostics;
    auto deviceFuture = impl->adapter.RequestDevice(
        &deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly,
        [pendingDevice](wgpu::RequestDeviceStatus status, wgpu::Device device,
                        wgpu::StringView message) noexcept {
          if (status == wgpu::RequestDeviceStatus::Success) {
            pendingDevice->value = std::move(device);
          } else {
            describeFailure(pendingDevice->error, "Request GPU device",
                            static_cast<unsigned>(status), message);
          }
        });
    completed = wait(impl->instance, deviceFuture, "Request device");

    if (!completed) {
      return std::unexpected(completed.error());
    }

    if (!pendingDevice->value) {
      return Core::fail(Core::ErrorCode::Unavailable, pendingDevice->error);
    }

    impl->device = std::move(pendingDevice->value);
    impl->queue = impl->device.GetQueue();
    wgpu::Limits limits;

    if (impl->device.GetLimits(&limits) != wgpu::Status::Success) {
      return Core::fail(Core::ErrorCode::External, "Cannot query GPU device limits");
    }

    impl->info.maxTextureDimension = limits.maxTextureDimension2D;
    ErrorScope scope(impl->instance, impl->device);
    impl->scenePipeline = impl->pipeline(wgpu::TextureFormat::RGBA16Float);
    impl->postPipeline = impl->postPipelineFor(wgpu::TextureFormat::RGBA8Unorm);
    wgpu::BufferDescriptor uniformDescriptor;
    uniformDescriptor.size = sizeof(UniformData);
    uniformDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    impl->uniform = impl->device.CreateBuffer(&uniformDescriptor);

    wgpu::TextureDescriptor whiteDescriptor;
    whiteDescriptor.size = {1, 1, 1};
    whiteDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    whiteDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    impl->whiteTexture = impl->device.CreateTexture(&whiteDescriptor);
    impl->whiteView = impl->whiteTexture.CreateView();
    wgpu::SamplerDescriptor whiteSampler;
    impl->whiteSampler = impl->device.CreateSampler(&whiteSampler);
    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    wgpu::TexelCopyTextureInfo whiteDestination;
    whiteDestination.texture = impl->whiteTexture;
    wgpu::TexelCopyBufferLayout whiteLayout;
    whiteLayout.bytesPerRow = 4;
    whiteLayout.rowsPerImage = 1;
    const wgpu::Extent3D whiteExtent{1, 1, 1};
    impl->queue.WriteTexture(&whiteDestination, white.data(), white.size(), &whiteLayout,
                             &whiteExtent);

    if (impl->surface) {
      wgpu::SurfaceCapabilities capabilities;

      if (impl->surface.GetCapabilities(impl->adapter, &capabilities) != wgpu::Status::Success
          || capabilities.formatCount == 0 || capabilities.alphaModeCount == 0) {
        return Core::fail(Core::ErrorCode::Unavailable, "Surface has no supported configuration");
      }

      impl->surfaceFormat = capabilities.formats[0];
      impl->alphaMode = capabilities.alphaModes[0];
      impl->surfacePostPipeline = impl->postPipelineFor(impl->surfaceFormat);
    }

    completed = scope.finish();

    if (!completed) {
      return std::unexpected(completed.error());
    }

    return Renderer(std::move(impl));
  }

  auto Renderer::upload(const MeshData& data) -> Core::Result<Mesh> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateMesh(data);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto& gpu = *m_impl;
    auto mesh = std::make_shared<Mesh::Impl>();
    mesh->owner = gpu.diagnostics;
    mesh->info = {static_cast<std::uint32_t>(data.vertices.size()),
                  static_cast<std::uint32_t>(data.indices.size())};
    auto surface = meshSurface(data);

    if (!surface) {
      return std::unexpected(surface.error());
    }

    mesh->bounds = surface->bounds;
    mesh->hasNormals = surface->hasNormals;
    mesh->hasNormalMapFrame = surface->hasNormalMapFrame;
    mesh->vertexBytes = data.vertices.size() * sizeof(Vertex);
    mesh->indexBytes = data.indices.size() * sizeof(std::uint32_t);
    ErrorScope scope(gpu.instance, gpu.device);
    wgpu::BufferDescriptor descriptor;
    descriptor.size = mesh->vertexBytes;
    descriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    mesh->vertices = gpu.device.CreateBuffer(&descriptor);
    gpu.queue.WriteBuffer(mesh->vertices, 0, data.vertices.data(), mesh->vertexBytes);

    if (!data.indices.empty()) {
      descriptor.size = mesh->indexBytes;
      descriptor.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
      mesh->indices = gpu.device.CreateBuffer(&descriptor);
      gpu.queue.WriteBuffer(mesh->indices, 0, data.indices.data(), mesh->indexBytes);
    }

    checked = scope.finish();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    gpu.diagnostics->meshes.fetch_add(1);
    gpu.diagnostics->vertexBytes.fetch_add(mesh->vertexBytes);
    gpu.diagnostics->indexBytes.fetch_add(mesh->indexBytes);
    mesh->tracked = true;
    return Mesh(std::move(mesh));
  }

  auto validateTextureOptions(const TextureOptions& options) -> Core::Result<void> {
    const auto addressValid = [](TextureAddress address) {
      return address == TextureAddress::Clamp || address == TextureAddress::Repeat
             || address == TextureAddress::Mirror;
    };

    if ((options.colorSpace != ColorSpace::Linear && options.colorSpace != ColorSpace::Srgb)
        || (options.filter != TextureFilter::Nearest && options.filter != TextureFilter::Linear)
        || !addressValid(options.addressU) || !addressValid(options.addressV)
        || options.maxAnisotropy == 0 || options.maxAnisotropy > 16
        || (options.filter == TextureFilter::Nearest && options.maxAnisotropy != 1)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid texture sampling/color options");
    }

    return {};
  }

  auto Renderer::uploadTexture(const Image& image, const TextureOptions& options)
      -> Core::Result<Texture> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateImage(image);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (image.width > m_impl->info.maxTextureDimension
        || image.height > m_impl->info.maxTextureDimension) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Texture exceeds device dimensions");
    }

    checked = validateTextureOptions(options);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::uint64_t bytes{};
    std::uint32_t levels{};
    auto width = image.width;
    auto height = image.height;

    do {
      bytes += static_cast<std::uint64_t>(width) * height * 4;
      ++levels;

      if (!options.mipmaps || (width == 1 && height == 1)) {
        break;
      }

      width = std::max(1U, width / 2);
      height = std::max(1U, height / 2);
    } while (true);

    if (m_impl->diagnostics->textures.load() >= m_impl->config.maxTextures
        || m_impl->diagnostics->textureBytes.load() + m_impl->diagnostics->environmentBytes.load()
                   + bytes
               > m_impl->config.maxTextureBytes) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Renderer texture count/byte budget exhausted");
    }

    try {
      auto texture = std::make_shared<Texture::Impl>();
      texture->owner = m_impl->diagnostics;
      texture->bytes = bytes;
      texture->info = {image.width, image.height, levels, options};
      texture->normalEncoding = options.colorSpace == ColorSpace::Linear;

      for (std::size_t index = 2; texture->normalEncoding && index < image.pixels.size();
           index += 4) {
        texture->normalEncoding = image.pixels[index] >= 128;
      }
      ErrorScope scope(m_impl->instance, m_impl->device);
      wgpu::TextureDescriptor descriptor;
      descriptor.size = {image.width, image.height, 1};
      descriptor.mipLevelCount = levels;
      descriptor.format = options.colorSpace == ColorSpace::Srgb
                              ? wgpu::TextureFormat::RGBA8UnormSrgb
                              : wgpu::TextureFormat::RGBA8Unorm;
      descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
      texture->texture = m_impl->device.CreateTexture(&descriptor);
      texture->view = texture->texture.CreateView();
      const auto addressMode = [](TextureAddress address) {
        switch (address) {
          case TextureAddress::Clamp:
            return wgpu::AddressMode::ClampToEdge;
          case TextureAddress::Repeat:
            return wgpu::AddressMode::Repeat;
          case TextureAddress::Mirror:
            return wgpu::AddressMode::MirrorRepeat;
        }

        std::unreachable();
      };
      wgpu::SamplerDescriptor sampler;
      sampler.addressModeU = addressMode(options.addressU);
      sampler.addressModeV = addressMode(options.addressV);
      sampler.magFilter = sampler.minFilter = options.filter == TextureFilter::Linear
                                                  ? wgpu::FilterMode::Linear
                                                  : wgpu::FilterMode::Nearest;
      sampler.mipmapFilter = options.filter == TextureFilter::Linear
                                 ? wgpu::MipmapFilterMode::Linear
                                 : wgpu::MipmapFilterMode::Nearest;
      sampler.maxAnisotropy = options.maxAnisotropy;
      texture->sampler = m_impl->device.CreateSampler(&sampler);
      Image mip = image;

      for (std::uint32_t level = 0; level < levels; ++level) {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = texture->texture;
        destination.mipLevel = level;
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = mip.width * 4;
        layout.rowsPerImage = mip.height;
        const wgpu::Extent3D extent{mip.width, mip.height, 1};
        m_impl->queue.WriteTexture(&destination, mip.pixels.data(), mip.pixels.size(), &layout,
                                   &extent);

        if (level + 1 == levels) {
          break;
        }

        Image next{std::max(1U, mip.width / 2), std::max(1U, mip.height / 2), {}};
        next.pixels.resize(static_cast<std::size_t>(next.width) * next.height * 4);

        for (std::uint32_t y = 0; y < next.height; ++y) {
          for (std::uint32_t x = 0; x < next.width; ++x) {
            const auto beginX = x * mip.width / next.width;
            const auto endX = (x + 1) * mip.width / next.width;
            const auto beginY = y * mip.height / next.height;
            const auto endY = (y + 1) * mip.height / next.height;
            const auto count = static_cast<double>((endX - beginX) * (endY - beginY));

            for (std::size_t channel = 0; channel < 4; ++channel) {
              double sum{};
              const auto srgb = channel != 3 && options.colorSpace == ColorSpace::Srgb;

              for (auto sy = beginY; sy < endY; ++sy) {
                for (auto sx = beginX; sx < endX; ++sx) {
                  auto value
                      = mip.pixels[(static_cast<std::size_t>(sy) * mip.width + sx) * 4 + channel]
                        / 255.0;

                  if (srgb) {
                    value
                        = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
                  }

                  sum += value;
                }
              }

              auto value = sum / count;

              if (srgb) {
                value = value <= 0.0031308 ? value * 12.92
                                           : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
              }

              next.pixels[(static_cast<std::size_t>(y) * next.width + x) * 4 + channel]
                  = static_cast<std::uint8_t>(std::clamp(std::round(value * 255), 0.0, 255.0));
            }
          }
        }

        mip = std::move(next);
      }

      checked = scope.finish();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (!texture->texture || !texture->view || !texture->sampler) {
        return Core::fail(Core::ErrorCode::External, "GPU texture/sampler allocation failed");
      }

      texture->owner->textures.fetch_add(1);
      texture->owner->textureBytes.fetch_add(bytes);
      texture->tracked = true;
      return Texture(std::move(texture));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Upload texture: " + std::string(error.what()));
    }
  }

  auto Renderer::resourceStats() const -> Core::Result<ResourceStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto& stats = *m_impl->diagnostics;
    return ResourceStats{stats.meshes.load(),
                         stats.vertexBytes.load(),
                         stats.indexBytes.load(),
                         stats.targets.load(),
                         stats.targetBytes.load(),
                         stats.textures.load(),
                         stats.textureBytes.load(),
                         static_cast<std::uint64_t>(m_impl->frameWidth) * m_impl->frameHeight * 12,
                         stats.environments.load(),
                         stats.environmentBytes.load(),
                         m_impl->shadowBytes,
                         m_impl->occlusionStorageBytes};
  }

  auto Renderer::bakeEnvironment(const HdrImage& panorama, const EnvironmentConfig& config)
      -> Core::Result<EnvironmentMap> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto description = validateEnvironment(panorama, config);

    if (!description) {
      return std::unexpected(description.error());
    }

    if (panorama.width > m_impl->info.maxTextureDimension) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "HDR panorama exceeds GPU dimensions");
    }

    std::uint64_t sourceBytes{};
    auto width = panorama.width;
    auto height = panorama.height;
    std::uint32_t sourceLevels{};

    do {
      sourceBytes += static_cast<std::uint64_t>(width) * height * 16;
      ++sourceLevels;
      width /= 2;
      height = std::max(1U, height / 2);
    } while (width);

    const auto& counters = *m_impl->diagnostics;

    if (counters.environments.load() >= 32
        || counters.environmentBytes.load() + counters.textureBytes.load() + sourceBytes
                   + description->bytes
               > m_impl->config.maxTextureBytes) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "Environment processing/residency budget exceeded");
    }

    try {
      auto& gpu = *m_impl;
      ErrorScope scope(gpu.instance, gpu.device);
      auto map = std::make_shared<EnvironmentMap::Impl>();
      map->owner = gpu.diagnostics;
      map->description = *description;
      wgpu::TextureDescriptor inputDescriptor;
      inputDescriptor.size = {panorama.width, panorama.height, 1};
      inputDescriptor.mipLevelCount = sourceLevels;
      inputDescriptor.format = wgpu::TextureFormat::RGBA32Float;
      inputDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
      auto input = gpu.device.CreateTexture(&inputDescriptor);
      auto inputView = input.CreateView();
      const HdrImage* image = &panorama;
      std::optional<HdrImage> owned;

      for (std::uint32_t level = 0; level < sourceLevels; ++level) {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = input;
        destination.mipLevel = level;
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = image->width * 16;
        layout.rowsPerImage = image->height;
        const wgpu::Extent3D extent{image->width, image->height, 1};
        gpu.queue.WriteTexture(&destination, image->pixels.data(),
                               image->pixels.size() * sizeof(float), &layout, &extent);

        if (level + 1 == sourceLevels) {
          break;
        }

        HdrImage next{std::max(1U, image->width / 2), std::max(1U, image->height / 2), {}};
        next.pixels.resize(static_cast<std::size_t>(next.width) * next.height * 4);

        for (std::uint32_t y = 0; y < next.height; ++y) {
          for (std::uint32_t x = 0; x < next.width; ++x) {
            const auto beginX = x * image->width / next.width;
            const auto endX = (x + 1) * image->width / next.width;
            const auto beginY = y * image->height / next.height;
            const auto endY = (y + 1) * image->height / next.height;
            std::array<double, 3> sum{};
            double weight{};

            for (auto sy = beginY; sy < endY; ++sy) {
              const auto solidAngle
                  = std::sin((static_cast<double>(sy) + 0.5) * 3.141592653589793 / image->height);

              for (auto sx = beginX; sx < endX; ++sx) {
                const auto offset = (static_cast<std::size_t>(sy) * image->width + sx) * 4;

                for (std::size_t channel = 0; channel < 3; ++channel) {
                  sum[channel] += image->pixels[offset + channel] * solidAngle;
                }

                weight += solidAngle;
              }
            }

            const auto offset = (static_cast<std::size_t>(y) * next.width + x) * 4;

            for (std::size_t channel = 0; channel < 3; ++channel) {
              next.pixels[offset + channel] = static_cast<float>(sum[channel] / weight);
            }

            next.pixels[offset + 3] = 1;
          }
        }

        owned = std::move(next);
        image = &*owned;
      }

      std::array<wgpu::BindGroupLayoutEntry, 3> entries{};
      entries[0].binding = 0;
      entries[0].visibility = wgpu::ShaderStage::Compute;
      entries[0].buffer.type = wgpu::BufferBindingType::Uniform;
      entries[0].buffer.minBindingSize = 32;
      entries[1].binding = 1;
      entries[1].visibility = wgpu::ShaderStage::Compute;
      entries[1].texture.sampleType = wgpu::TextureSampleType::UnfilterableFloat;
      entries[1].texture.viewDimension = wgpu::TextureViewDimension::e2D;
      entries[2].binding = 2;
      entries[2].visibility = wgpu::ShaderStage::Compute;
      entries[2].storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;
      entries[2].storageTexture.format = wgpu::TextureFormat::RGBA16Float;
      entries[2].storageTexture.viewDimension = wgpu::TextureViewDimension::e2DArray;
      wgpu::BindGroupLayoutDescriptor layoutDescriptor;
      layoutDescriptor.entryCount = entries.size();
      layoutDescriptor.entries = entries.data();
      auto layout = gpu.device.CreateBindGroupLayout(&layoutDescriptor);
      wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
      pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
      pipelineLayoutDescriptor.bindGroupLayouts = &layout;
      auto pipelineLayout = gpu.device.CreatePipelineLayout(&pipelineLayoutDescriptor);
      wgpu::ShaderSourceWGSL shaderSource;
      shaderSource.code = Detail::environmentShader;
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &shaderSource;
      auto shader = gpu.device.CreateShaderModule(&shaderDescriptor);
      wgpu::ComputePipelineDescriptor pipelineDescriptor;
      pipelineDescriptor.layout = pipelineLayout;
      pipelineDescriptor.compute.module = shader;
      pipelineDescriptor.compute.entryPoint = "bake";
      auto pipeline = gpu.device.CreateComputePipeline(&pipelineDescriptor);
      const std::array sizes{config.radianceSize, config.diffuseSize, config.specularSize,
                             config.brdfSize};
      const std::array levels{description->radianceLevels, 1U, description->specularLevels, 1U};
      auto encoder = gpu.device.CreateCommandEncoder();
      std::vector<wgpu::Buffer> parameters;

      for (std::uint32_t kind = 0; kind < 4; ++kind) {
        wgpu::TextureDescriptor descriptor;
        descriptor.size = {sizes[kind], sizes[kind], kind == 3 ? 1U : 6U};
        descriptor.mipLevelCount = levels[kind];
        descriptor.format = wgpu::TextureFormat::RGBA16Float;
        descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding
                           | wgpu::TextureUsage::CopySrc;
        map->textures[kind] = gpu.device.CreateTexture(&descriptor);
        wgpu::TextureViewDescriptor view;
        view.dimension
            = kind == 3 ? wgpu::TextureViewDimension::e2D : wgpu::TextureViewDimension::Cube;
        map->views[kind] = map->textures[kind].CreateView(&view);

        for (std::uint32_t level = 0; level < levels[kind]; ++level) {
          const auto side = std::max(1U, sizes[kind] >> level);

          struct Params {
            std::uint32_t kind, side, samples, sourceLevels;
            float roughness;
            std::array<float, 3> padding{};
          };

          static_assert(sizeof(Params) == 32);
          const Params params{
              kind, side, config.samples, sourceLevels,
              kind == 2 ? static_cast<float>(level) / static_cast<float>(levels[kind] - 1) : 0};
          wgpu::BufferDescriptor buffer;
          buffer.size = sizeof(params);
          buffer.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
          parameters.push_back(gpu.device.CreateBuffer(&buffer));
          gpu.queue.WriteBuffer(parameters.back(), 0, &params, sizeof(params));
          view = {};
          view.dimension = wgpu::TextureViewDimension::e2DArray;
          view.baseMipLevel = level;
          view.mipLevelCount = 1;
          auto destination = map->textures[kind].CreateView(&view);
          std::array<wgpu::BindGroupEntry, 3> bindings{};
          bindings[0].binding = 0;
          bindings[0].buffer = parameters.back();
          bindings[0].size = sizeof(params);
          bindings[1].binding = 1;
          bindings[1].textureView = inputView;
          bindings[2].binding = 2;
          bindings[2].textureView = destination;
          wgpu::BindGroupDescriptor groupDescriptor;
          groupDescriptor.layout = layout;
          groupDescriptor.entryCount = bindings.size();
          groupDescriptor.entries = bindings.data();
          auto group = gpu.device.CreateBindGroup(&groupDescriptor);
          auto pass = encoder.BeginComputePass();
          pass.SetPipeline(pipeline);
          pass.SetBindGroup(0, group);
          pass.DispatchWorkgroups((side + 7) / 8, (side + 7) / 8, kind == 3 ? 1 : 6);
          pass.End();
        }
      }

      wgpu::SamplerDescriptor sampler;
      sampler.magFilter = sampler.minFilter = wgpu::FilterMode::Linear;
      sampler.mipmapFilter = wgpu::MipmapFilterMode::Linear;
      map->sampler = gpu.device.CreateSampler(&sampler);
      auto commands = encoder.Finish();
      gpu.queue.Submit(1, &commands);
      checked = scope.finish();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      auto pending = std::make_shared<Pending<bool>>();
      pending->keepAlive = gpu.diagnostics;
      pending->error = "Environment GPU completion failed";
      auto future = gpu.queue.OnSubmittedWorkDone(
          wgpu::CallbackMode::WaitAnyOnly,
          [pending](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) noexcept {
            pending->value = status == wgpu::QueueWorkDoneStatus::Success;

            if (!pending->value) {
              describeFailure(pending->error, "Environment GPU completion",
                              static_cast<unsigned>(status), message);
            }
          });
      checked = wait(gpu.instance, future, "Environment preprocessing");

      if (!checked || !pending->value) {
        gpu.diagnostics->failed.store(true);
        return checked ? Core::fail(Core::ErrorCode::Unavailable, pending->error)
                       : std::unexpected(checked.error());
      }

      input.Destroy();
      map->tracked = true;
      map->owner->environments.fetch_add(1);
      map->owner->environmentBytes.fetch_add(description->bytes);
      return EnvironmentMap(std::move(map));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Bake HDR environment: " + std::string(error.what()));
    }
  }

  auto Renderer::readEnvironment(const EnvironmentMap& map, EnvironmentSurface surface,
                                 std::uint32_t face, std::uint32_t level)
      -> Core::Result<HdrImage> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto info = map.info();
    const auto kind = static_cast<std::uint32_t>(surface);

    if (!info) {
      return std::unexpected(info.error());
    }

    if (map.m_impl->owner != m_impl->diagnostics || kind >= 4) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid/foreign environment readback");
    }

    const auto& c = info->config;
    const std::array sizes{c.radianceSize, c.diffuseSize, c.specularSize, c.brdfSize};
    const std::array levels{info->radianceLevels, 1U, info->specularLevels, 1U};

    if (level >= levels[kind] || face >= (kind == 3 ? 1U : 6U)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid environment face/mip");
    }

    const auto side = std::max(1U, sizes[kind] >> level);
    return m_impl->readback<HdrImage>(
        side, side,
        [&](const wgpu::Texture&, std::uint32_t, std::uint32_t)
            -> Core::Result<wgpu::CommandEncoder> { return m_impl->device.CreateCommandEncoder(); },
        map.m_impl->textures[kind], wgpu::TextureFormat::RGBA16Float, face, level);
  }

  auto Renderer::setEnvironment(std::optional<EnvironmentLighting> environment)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (environment) {
      auto info = environment->map.info();

      if (!info) {
        return std::unexpected(info.error());
      }

      if (environment->map.m_impl->owner != m_impl->diagnostics
          || !std::isfinite(environment->intensity) || environment->intensity < 0
          || environment->intensity > 64 || !std::isfinite(environment->rotationDegrees)
          || std::abs(environment->rotationDegrees) > 360) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid/foreign environment lighting");
      }

      if (!m_impl->environmentPipeline || !m_impl->skyPipeline) {
        ErrorScope scope(m_impl->instance, m_impl->device);
        auto surface = m_impl->pipeline(wgpu::TextureFormat::RGBA16Float, true);
        auto sky = m_impl->skyPipelineFor();
        checked = scope.finish();

        if (!checked) {
          return checked;
        }

        if (!surface || !sky) {
          return Core::fail(Core::ErrorCode::External,
                            "Create environment render pipelines failed");
        }

        m_impl->environmentPipeline = std::move(surface);
        m_impl->skyPipeline = std::move(sky);
      }
    }

    m_impl->environment = std::move(environment);
    return {};
  }

  auto Renderer::environment() const -> Core::Result<std::optional<EnvironmentLighting>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->environment;
  }

  auto Renderer::setOcclusion(std::optional<OcclusionSettings> settings) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (settings) {
      checked = validateOcclusion(*settings);

      if (!checked) {
        return checked;
      }
    }

    if (m_impl->occlusionRevision == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Occlusion binding generations exhausted");
    }

    m_impl->occlusion = settings;
    ++m_impl->occlusionRevision;

    if (!settings) {
      m_impl->occlusionTextures = {};
      m_impl->occlusionStorageBytes = 0;
      m_impl->occlusionWidth = m_impl->occlusionHeight = 0;
      m_impl->occlusionReady = m_impl->occlusionPending = false;
    }

    return {};
  }

  auto Renderer::occlusion() const -> Core::Result<std::optional<OcclusionSettings>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::optional<OcclusionSettings>>(m_impl->occlusion)
                   : std::unexpected(checked.error());
  }

  auto Renderer::occlusionRevision() const -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<std::uint64_t>(m_impl->occlusionRevision)
                   : std::unexpected(checked.error());
  }

  auto Renderer::readOcclusion() -> Core::Result<Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->occlusionReady) {
      return Core::fail(Core::ErrorCode::InvalidState, "Occlusion has no committed mesh frame");
    }

    return m_impl->readback(
        m_impl->occlusionWidth, m_impl->occlusionHeight,
        [&](const wgpu::Texture&, std::uint32_t, std::uint32_t)
            -> Core::Result<wgpu::CommandEncoder> { return m_impl->device.CreateCommandEncoder(); },
        m_impl->occlusionTextures[3]);
  }

  auto Renderer::setLighting(const Lighting& lighting) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    auto validated = validatedLighting(lighting);

    if (!validated) {
      return std::unexpected(validated.error());
    }

    m_impl->config.lighting = std::move(*validated);

    if (!m_impl->shadowSource()) {
      m_impl->shadowTexture = nullptr;
      m_impl->shadowResolution = 0;
      m_impl->shadowLayers = 0;
      m_impl->shadowBytes = 0;
    }

    return {};
  }

  auto Renderer::lighting() const -> Core::Result<Lighting> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return validatedLighting(m_impl->config.lighting);
  }

  auto Renderer::setPostProcess(const PostProcessSettings& settings) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    checked = validatePostProcess(settings);

    if (!checked) {
      return checked;
    }

    m_impl->config.postProcess = settings;
    return {};
  }

  auto Renderer::postProcess() const -> Core::Result<PostProcessSettings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->config.postProcess;
  }

  auto Renderer::createTarget(std::uint32_t width, std::uint32_t height)
      -> Core::Result<RenderTarget> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto maximum = std::min(maxReadbackDimension, m_impl->info.maxTextureDimension);

    if (width == 0 || height == 0 || width > maximum || height > maximum) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Render target dimensions must be 1..4096 within device limits");
    }

    try {
      const auto bytes = static_cast<std::uint64_t>(width) * height * 16;

      if (m_impl->diagnostics->targets.load() >= 128
          || m_impl->diagnostics->targetBytes.load() + bytes > 256 * 1024 * 1024) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Renderer target count/byte budget exhausted");
      }

      auto target = std::make_shared<RenderTarget::Impl>();
      target->owner = m_impl->diagnostics;
      target->info = {width, height, 0};
      target->bytes = bytes;
      ErrorScope scope(m_impl->instance, m_impl->device);
      wgpu::TextureDescriptor descriptor;
      descriptor.size = {width, height, 1};
      descriptor.format = wgpu::TextureFormat::RGBA8Unorm;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding
                         | wgpu::TextureUsage::CopySrc;
      target->color = m_impl->device.CreateTexture(&descriptor);
      target->view = target->color.CreateView();
      descriptor.format = wgpu::TextureFormat::RGBA16Float;
      target->hdr = m_impl->device.CreateTexture(&descriptor);
      descriptor.format = wgpu::TextureFormat::Depth24Plus;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment;
      target->depth = m_impl->device.CreateTexture(&descriptor);
      target->depthView = target->depth.CreateView();
      checked = scope.finish();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (!target->color || !target->view || !target->hdr || !target->depth || !target->depthView) {
        return Core::fail(Core::ErrorCode::External, "GPU render target allocation failed");
      }

      target->owner->targets.fetch_add(1);
      target->owner->targetBytes.fetch_add(target->bytes);
      target->tracked = true;
      return RenderTarget(std::move(target));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create render target: " + std::string(error.what()));
    }
  }

  auto Renderer::renderMeshesInto(RenderTarget& target, std::span<const MeshDraw> draws,
                                  const Math::Mat4& viewProjection, Math::Vec4 clear)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (!target.m_impl || target.m_impl->owner != m_impl->diagnostics) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Render target belongs to another renderer or is empty");
    }

    auto info = target.info();

    if (!info) {
      return std::unexpected(info.error());
    }

    if (info->frames == std::numeric_limits<std::uint64_t>::max() - 1) {
      return Core::fail(Core::ErrorCode::Unavailable, "Render target frame index exhausted");
    }

    auto prepared = m_impl->prepare(draws, viewProjection, clear);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    ErrorScope scope(m_impl->instance, m_impl->device);
    auto encoder
        = m_impl->encodeMeshes(target.m_impl->color, *prepared, viewProjection, info->width,
                               info->height, clear, target.m_impl->hdr, target.m_impl->depthView);

    if (!encoder) {
      return std::unexpected(encoder.error());
    }

    auto commands = encoder->Finish();
    m_impl->queue.Submit(1, &commands);
    checked = scope.finish();

    if (!checked) {
      target.m_impl->faulted = true;
      return checked;
    }

    if (m_impl->occlusionPending) {
      m_impl->occlusionReady = true;
      m_impl->occlusionPending = false;
    }

    ++target.m_impl->info.frames;
    return {};
  }

  auto Renderer::readTarget(const RenderTarget& target) -> Core::Result<Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!target.m_impl || target.m_impl->owner != m_impl->diagnostics) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Render target belongs to another renderer or is empty");
    }

    auto info = target.info();

    if (!info) {
      return std::unexpected(info.error());
    }

    if (info->frames == 0) {
      return Core::fail(Core::ErrorCode::InvalidState, "Render target has no completed frame");
    }

    return m_impl->readback(
        info->width, info->height,
        [&](const auto&, auto, auto) {
          return Core::Result<wgpu::CommandEncoder>(m_impl->device.CreateCommandEncoder());
        },
        target.m_impl->color);
  }

  auto Renderer::readHdrTarget(const RenderTarget& target) -> Core::Result<HdrImage> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!target.m_impl || target.m_impl->owner != m_impl->diagnostics) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Render target belongs to another renderer or is empty");
    }

    auto info = target.info();

    if (!info) {
      return std::unexpected(info.error());
    }

    if (info->frames == 0) {
      return Core::fail(Core::ErrorCode::InvalidState, "Render target has no completed frame");
    }

    return m_impl->readback<HdrImage>(
        info->width, info->height,
        [&](const auto&, auto, auto) {
          return Core::Result<wgpu::CommandEncoder>(m_impl->device.CreateCommandEncoder());
        },
        target.m_impl->hdr, wgpu::TextureFormat::RGBA16Float);
  }

  auto Renderer::enableGui(const GuiConfig& config) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (!Platform::isMainThread()) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "GUI contexts require the process main thread");
    }

    if (m_impl->gui) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer GUI is already enabled");
    }

    const auto format = m_impl->surface ? m_impl->surfaceFormat : wgpu::TextureFormat::RGBA8Unorm;

    if (format != wgpu::TextureFormat::RGBA8Unorm && format != wgpu::TextureFormat::BGRA8Unorm
        && format != wgpu::TextureFormat::RGBA8UnormSrgb
        && format != wgpu::TextureFormat::BGRA8UnormSrgb) {
      return Core::fail(Core::ErrorCode::Unavailable,
                        "GUI requires an RGBA8/BGRA8 rendering surface");
    }

    ErrorScope scope(m_impl->instance, m_impl->device);
    auto gui = Detail::GuiBackend::create(
        m_impl->device.Get(), static_cast<WGPUTextureFormat>(format),
        m_impl->window ? static_cast<SDL_Window*>(m_impl->window->nativeHandle()) : nullptr,
        config);
    checked = scope.finish();

    if (!gui) {
      return std::unexpected(gui.error());
    }

    if (!checked) {
      return checked;
    }

    m_impl->gui = std::move(*gui);
    return {};
  }

  auto Renderer::closeGui() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    if (m_impl->thread != std::this_thread::get_id() || (m_impl->gui && m_impl->gui->building())) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "GUI shutdown requires owner thread without reentry");
    }

    m_impl->guiTargets.clear();
    m_impl->gui.reset();
    return {};
  }

  auto Renderer::guiEvent(const Platform::Event& event) -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->gui) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer GUI is not enabled");
    }

    return m_impl->gui->event(event);
  }

  auto Renderer::guiStats() const -> Core::Result<GuiStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->gui ? m_impl->gui->stats() : GuiStats{};
  }

  auto Renderer::guiImage(const RenderTarget& target, float width, float height)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    if (!m_impl->gui || !m_impl->gui->building()) {
      return Core::fail(Core::ErrorCode::InvalidState, "GUI image requires an active GUI callback");
    }

    if (!target.m_impl || target.m_impl->owner != m_impl->diagnostics) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "GUI image target belongs to another renderer or is empty");
    }

    auto info = target.info();

    if (!info) {
      return std::unexpected(info.error());
    }

    if (info->frames == 0) {
      return Core::fail(Core::ErrorCode::InvalidState, "GUI image target has no completed frame");
    }

    m_impl->guiTargets.push_back(target);
    return m_impl->gui->image(target.m_impl->view.Get(), width, height);
  }

  auto Renderer::renderGui(const GuiDraw& draw, std::uint32_t width, std::uint32_t height,
                           double seconds, float scale) -> Core::Result<Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto maximum = std::min(maxReadbackDimension, m_impl->info.maxTextureDimension);

    if (!m_impl->gui || width == 0 || height == 0 || width > maximum || height > maximum) {
      return Core::fail(
          m_impl->gui ? Core::ErrorCode::InvalidArgument : Core::ErrorCode::InvalidState,
          "GUI rendering requires an enabled context and dimensions within 1..4096/device limits");
    }

    m_impl->guiTargets.clear();

    try {
      ErrorScope scope(m_impl->instance, m_impl->device);
      auto built = m_impl->gui->build(draw, width, height, seconds, scale, scale);
      checked = scope.finish();

      if (!built || !checked) {
        m_impl->guiTargets.clear();
        return std::unexpected(built ? checked.error() : built.error());
      }

      const auto format = m_impl->surface ? m_impl->surfaceFormat : wgpu::TextureFormat::RGBA8Unorm;
      auto image = m_impl->readback(
          width, height,
          [&](const auto& texture, auto, auto) {
            return Core::Result<wgpu::CommandEncoder>(m_impl->encodeGui(texture));
          },
          {}, format);
      m_impl->guiTargets.clear();

      if (image) {
        m_impl->gui->submitted();
      }

      return image;
    } catch (const std::exception& error) {
      m_impl->guiTargets.clear();
      return Core::fail(Core::ErrorCode::External, "Render GUI: " + std::string(error.what()));
    }
  }

  auto Renderer::presentGui(const GuiDraw& draw, double seconds) -> Core::Result<PresentResult> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->gui || !m_impl->surface) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "GUI presentation requires an enabled context and surface");
    }

    auto pixels = m_impl->window->pixelSize();
    auto logical = m_impl->window->size();

    if (!pixels || !logical) {
      return std::unexpected(pixels ? logical.error() : pixels.error());
    }

    if (pixels->width <= 0 || pixels->height <= 0 || logical->width <= 0 || logical->height <= 0) {
      return PresentResult::Deferred;
    }

    const auto width = static_cast<std::uint32_t>(pixels->width);
    const auto height = static_cast<std::uint32_t>(pixels->height);

    if (width > m_impl->info.maxTextureDimension || height > m_impl->info.maxTextureDimension) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "GUI surface exceeds texture limits");
    }

    m_impl->guiTargets.clear();

    try {
      ErrorScope scope(m_impl->instance, m_impl->device);
      auto built = m_impl->gui->build(
          draw, width, height, seconds,
          static_cast<float>(pixels->width) / static_cast<float>(logical->width),
          static_cast<float>(pixels->height) / static_cast<float>(logical->height));
      checked = scope.finish();

      if (!built || !checked) {
        m_impl->guiTargets.clear();
        return std::unexpected(built ? checked.error() : built.error());
      }

      auto presented = m_impl->presentFrame([&](const auto& texture, auto, auto) {
        return Core::Result<wgpu::CommandEncoder>(m_impl->encodeGui(texture));
      });
      m_impl->guiTargets.clear();

      if (presented && *presented == PresentResult::Presented) {
        m_impl->gui->submitted();
      }

      return presented;
    } catch (const std::exception& error) {
      m_impl->guiTargets.clear();
      return Core::fail(Core::ErrorCode::External, "Present GUI: " + std::string(error.what()));
    }
  }

  auto Renderer::adapterInfo() const -> Core::Result<AdapterInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->info;
  }

  auto Renderer::surfaceSize() const -> Core::Result<Platform::Extent> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->window) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has no native surface");
    }

    return m_impl->window->pixelSize();
  }

  auto Renderer::render(std::span<const Vertex> vertices, const Math::Mat4& transform,
                        std::uint32_t width, std::uint32_t height, Math::Vec4 clear)
      -> Core::Result<Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validate(vertices, transform, clear);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->readback(
        width, height, [&](const wgpu::Texture& texture, auto targetWidth, auto targetHeight) {
          return m_impl->encode(texture, vertices, transform, targetWidth, targetHeight, clear);
        });
  }

  auto Renderer::present(std::span<const Vertex> vertices, const Math::Mat4& transform,
                         Math::Vec4 clear) -> Core::Result<PresentResult> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto& gpu = *m_impl;
    auto checked = gpu.check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!gpu.surface) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has no native surface");
    }

    checked = validate(vertices, transform, clear);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return gpu.presentFrame([&](const wgpu::Texture& texture, auto width, auto height) {
      return gpu.encode(texture, vertices, transform, width, height, clear);
    });
  }

  auto Renderer::renderMeshes(std::span<const MeshDraw> draws, const Math::Mat4& viewProjection,
                              std::uint32_t width, std::uint32_t height, Math::Vec4 clear)
      -> Core::Result<Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto prepared = m_impl->prepare(draws, viewProjection, clear);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    return m_impl->readback(
        width, height,
        [&](const wgpu::Texture& texture, auto targetWidth, auto targetHeight) {
          return m_impl->encodeMeshes(texture, *prepared, viewProjection, targetWidth, targetHeight,
                                      clear);
        },
        {}, wgpu::TextureFormat::RGBA8Unorm, 0, 0, true);
  }

  auto Renderer::pickMeshes(std::span<const MeshDraw> draws, const Math::Mat4& viewProjection,
                            std::uint32_t width, std::uint32_t height, Math::Vec2 uv)
      -> Core::Result<std::optional<std::size_t>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto projected = pixelProjection(viewProjection, width, height, uv);

    if (!projected) {
      return std::unexpected(projected.error());
    }

    auto prepared = m_impl->prepare(draws, *projected, {0, 0, 0, 0});

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    checked = m_impl->ensurePickingPipeline();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    struct PickData {
      Math::Mat4 transform;
      Math::Vec4 flags;
      std::array<std::uint32_t, 4> identifier;
    };

    static_assert(sizeof(PickData) == 96);
    auto image = m_impl->readback(
        1, 1,
        [&](const wgpu::Texture& target, auto, auto) -> Core::Result<wgpu::CommandEncoder> {
          wgpu::TextureDescriptor depthDescriptor;
          depthDescriptor.size = {1, 1, 1};
          depthDescriptor.format = wgpu::TextureFormat::Depth24Plus;
          depthDescriptor.usage = wgpu::TextureUsage::RenderAttachment;
          auto depth = m_impl->device.CreateTexture(&depthDescriptor);
          return m_impl->encodePass(
              target, 1, 1, {0, 0, 0, 0},
              [&](const wgpu::RenderPassEncoder& pass) {
                pass.SetPipeline(m_impl->pickingPipeline);

                for (std::size_t index = 0; index < prepared->size(); ++index) {
                  const auto& draw = (*prepared)[index];
                  const PickData data{draw.uniform.transform,
                                      draw.uniform.flags,
                                      {static_cast<std::uint32_t>(index + 1), 0, 0, 0}};
                  wgpu::BufferDescriptor descriptor;
                  descriptor.size = sizeof(data);
                  descriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
                  auto buffer = m_impl->device.CreateBuffer(&descriptor);
                  m_impl->queue.WriteBuffer(buffer, 0, &data, sizeof(data));
                  wgpu::BindGroupEntry entry;
                  entry.buffer = buffer;
                  entry.size = sizeof(data);
                  wgpu::BindGroupDescriptor group;
                  group.layout = m_impl->pickingPipeline.GetBindGroupLayout(0);
                  group.entryCount = 1;
                  group.entries = &entry;
                  pass.SetBindGroup(0, m_impl->device.CreateBindGroup(&group));
                  pass.SetVertexBuffer(0, draw.mesh->vertices);

                  if (draw.mesh->info.indices) {
                    pass.SetIndexBuffer(draw.mesh->indices, wgpu::IndexFormat::Uint32);
                    pass.DrawIndexed(draw.mesh->info.indices);
                  } else {
                    pass.Draw(draw.mesh->info.vertices);
                  }
                }
              },
              depth.CreateView());
        },
        {}, wgpu::TextureFormat::R32Uint);

    if (!image) {
      return std::unexpected(image.error());
    }

    std::uint32_t identifier{};
    std::memcpy(&identifier, image->pixels.data(), sizeof(identifier));

    if (identifier > draws.size()) {
      return Core::fail(Core::ErrorCode::External, "Integer picking returned an unknown draw");
    }

    return identifier ? std::optional<std::size_t>(identifier - 1) : std::nullopt;
  }

  auto Renderer::presentMeshes(std::span<const MeshDraw> draws, const Math::Mat4& viewProjection,
                               Math::Vec4 clear) -> Core::Result<PresentResult> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto prepared = m_impl->prepare(draws, viewProjection, clear);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    return m_impl->presentFrame(
        [&](const wgpu::Texture& texture, auto width, auto height) {
          return m_impl->encodeMeshes(texture, *prepared, viewProjection, width, height, clear);
        },
        true);
  }
}  // namespace Dot::Graphics
