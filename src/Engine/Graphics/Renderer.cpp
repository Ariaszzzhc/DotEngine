module;

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <imgui.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Math;
import Dot.Platform.Window;
import Dot.Platform.System;
import Dot.Platform.Events;

#include "GuiBackend.hpp"

namespace Dot::Graphics {
  namespace {
    constexpr std::uint64_t waitTimeout = 10'000'000'000;
    constexpr std::size_t maxVertices = 3'000'000;
    constexpr std::uint32_t maxReadbackDimension = 4096;
    constexpr auto shaderCode = R"(
struct Camera { transform: mat4x4f, tint: vec4f }
@group(0) @binding(0) var<uniform> camera: Camera;
struct Output { @builtin(position) position: vec4f, @location(0) color: vec4f }
@vertex fn vertexMain(@location(0) position: vec3f, @location(1) color: vec4f) -> Output {
  var output: Output;
  output.position = camera.transform * vec4f(position, 1.0);
  output.color = color * camera.tint;
  return output;
}
@fragment fn fragmentMain(input: Output) -> @location(0) vec4f { return input.color; }
)";

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

    auto validate(std::span<const Vertex> vertices, const Math::Mat4& transform, Math::Vec4 clear)
        -> Core::Result<void> {
      if (vertices.size() > maxVertices || vertices.size() % 3 != 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Geometry must be a triangle list with at most 3000000 vertices");
      }

      if (!Math::isFinite(transform) || !colorValid(clear)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid transform or clear color");
      }

      for (const auto& vertex : vertices) {
        if (!Math::isFinite(vertex.position) || !colorValid(vertex.color)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid vertex position or color");
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
    };

    static_assert(sizeof(UniformData) == 80);
    static_assert(offsetof(UniformData, tint) == 64);

    struct Diagnostics {
      std::atomic<bool> alive{true};
      std::atomic<std::uint64_t> meshes{};
      std::atomic<std::uint64_t> vertexBytes{};
      std::atomic<std::uint64_t> indexBytes{};
      std::atomic<std::uint64_t> targets{};
      std::atomic<std::uint64_t> targetBytes{};
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

  struct Renderer::Impl {
    std::thread::id thread{std::this_thread::get_id()};
    Platform::Window* window{};
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
    wgpu::RenderPipeline offscreenPipeline;
    wgpu::RenderPipeline surfacePipeline;
    wgpu::Buffer uniform;
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

    auto pipeline(wgpu::TextureFormat format) -> wgpu::RenderPipeline {
      wgpu::ShaderSourceWGSL source;
      source.code = shaderCode;
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &source;
      auto shader = device.CreateShaderModule(&shaderDescriptor);
      const std::array attributes{wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, position),
                                                        .shaderLocation = 0},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4,
                                                        .offset = offsetof(Vertex, color),
                                                        .shaderLocation = 1}};
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

    template <typename F>
    auto encodePass(const wgpu::Texture& target, std::uint32_t width, std::uint32_t height,
                    Math::Vec4 clear, F&& draw, wgpu::TextureView depthView = {})
        -> wgpu::CommandEncoder {
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
      auto encoder = device.CreateCommandEncoder();
      auto pass = encoder.BeginRenderPass(&descriptor);

      draw(pass);
      pass.End();
      return encoder;
    }

    auto encode(const wgpu::Texture& target, const wgpu::RenderPipeline& renderPipeline,
                std::span<const Vertex> vertices, const Math::Mat4& transform, std::uint32_t width,
                std::uint32_t height, Math::Vec4 clear) -> wgpu::CommandEncoder {
      return encodePass(target, width, height, clear, [&](const wgpu::RenderPassEncoder& pass) {
        if (!vertices.empty()) {
          wgpu::BufferDescriptor bufferDescriptor;
          bufferDescriptor.size = vertices.size_bytes();
          bufferDescriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
          auto buffer = device.CreateBuffer(&bufferDescriptor);
          queue.WriteBuffer(buffer, 0, vertices.data(), vertices.size_bytes());
          const UniformData data{transform};
          queue.WriteBuffer(uniform, 0, &data, sizeof(data));
          wgpu::BindGroupEntry entry;
          entry.binding = 0;
          entry.buffer = uniform;
          entry.size = sizeof(UniformData);
          wgpu::BindGroupDescriptor groupDescriptor;
          groupDescriptor.layout = renderPipeline.GetBindGroupLayout(0);
          groupDescriptor.entryCount = 1;
          groupDescriptor.entries = &entry;
          auto group = device.CreateBindGroup(&groupDescriptor);
          pass.SetPipeline(renderPipeline);
          pass.SetBindGroup(0, group);
          pass.SetVertexBuffer(0, buffer);
          pass.Draw(static_cast<std::uint32_t>(vertices.size()));
        }
      });
    }

    struct PreparedDraw {
      std::shared_ptr<Mesh::Impl> mesh;
      UniformData uniform;
    };

    auto prepare(std::span<const MeshDraw> draws, const Math::Mat4& viewProjection,
                 Math::Vec4 clear) -> Core::Result<std::vector<PreparedDraw>> {
      if (draws.size() > 4096 || !Math::isFinite(viewProjection) || !colorValid(clear)) {
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

        auto transform = Math::multiply(viewProjection, draw.transform);

        if (!transform) {
          return std::unexpected(transform.error());
        }

        prepared.push_back({draw.mesh->m_impl, {*transform, draw.tint}});
      }

      return prepared;
    }

    auto encodeMeshes(const wgpu::Texture& target, const wgpu::RenderPipeline& renderPipeline,
                      std::span<const PreparedDraw> draws, std::uint32_t width,
                      std::uint32_t height, Math::Vec4 clear, wgpu::TextureView depthView = {})
        -> wgpu::CommandEncoder {
      return encodePass(
          target, width, height, clear,
          [&](const wgpu::RenderPassEncoder& pass) {
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
              wgpu::BindGroupEntry entry;
              entry.binding = 0;
              entry.buffer = drawUniform;
              entry.size = sizeof(UniformData);
              wgpu::BindGroupDescriptor groupDescriptor;
              groupDescriptor.layout = renderPipeline.GetBindGroupLayout(0);
              groupDescriptor.entryCount = 1;
              groupDescriptor.entries = &entry;
              auto group = device.CreateBindGroup(&groupDescriptor);
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
          depthView);
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

    template <typename F>
    auto readback(std::uint32_t width, std::uint32_t height, F&& encodeTarget,
                  wgpu::Texture existing = {},
                  wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8Unorm)
        -> Core::Result<Image> {
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
      auto encoder = encodeTarget(texture, width, height);
      const auto rowBytes = width * 4;
      const auto paddedRowBytes = (rowBytes + 255U) & ~255U;
      const auto bufferBytes = static_cast<std::size_t>(paddedRowBytes) * height;
      wgpu::BufferDescriptor bufferDescriptor;
      bufferDescriptor.size = bufferBytes;
      bufferDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
      auto buffer = device.CreateBuffer(&bufferDescriptor);
      wgpu::TexelCopyTextureInfo source;
      source.texture = texture;
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

      Image image{width, height,
                  std::vector<std::uint8_t>(static_cast<std::size_t>(rowBytes) * height)};

      for (std::size_t row = 0; row < height; ++row) {
        std::memcpy(image.pixels.data() + row * rowBytes, bytes + row * paddedRowBytes, rowBytes);
      }

      buffer.Unmap();

      if (format == wgpu::TextureFormat::BGRA8Unorm
          || format == wgpu::TextureFormat::BGRA8UnormSrgb) {
        for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
          std::swap(image.pixels[offset], image.pixels[offset + 2]);
        }
      }

      return image;
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
    auto presentFrame(F&& encodeTarget) -> Core::Result<PresentResult> {
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
      auto commands = encoder.Finish();
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

      return PresentResult::Presented;
    }
  };

  Renderer::Renderer(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Renderer::Renderer(Renderer&&) noexcept = default;

  auto Renderer::operator=(Renderer&&) noexcept -> Renderer& = default;

  Renderer::~Renderer() = default;

  auto Renderer::create(Platform::Window* window) -> Core::Result<Renderer> {
    auto impl = std::make_unique<Impl>();
    impl->window = window;
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
    impl->offscreenPipeline = impl->pipeline(wgpu::TextureFormat::RGBA8Unorm);
    wgpu::BufferDescriptor uniformDescriptor;
    uniformDescriptor.size = sizeof(UniformData);
    uniformDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    impl->uniform = impl->device.CreateBuffer(&uniformDescriptor);

    if (impl->surface) {
      wgpu::SurfaceCapabilities capabilities;

      if (impl->surface.GetCapabilities(impl->adapter, &capabilities) != wgpu::Status::Success
          || capabilities.formatCount == 0 || capabilities.alphaModeCount == 0) {
        return Core::fail(Core::ErrorCode::Unavailable, "Surface has no supported configuration");
      }

      impl->surfaceFormat = capabilities.formats[0];
      impl->alphaMode = capabilities.alphaModes[0];
      impl->surfacePipeline = impl->pipeline(impl->surfaceFormat);
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

  auto Renderer::resourceStats() const -> Core::Result<ResourceStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto& stats = *m_impl->diagnostics;
    return ResourceStats{stats.meshes.load(), stats.vertexBytes.load(), stats.indexBytes.load(),
                         stats.targets.load(), stats.targetBytes.load()};
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
      const auto bytes = static_cast<std::uint64_t>(width) * height * 8;

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
      descriptor.format = wgpu::TextureFormat::Depth24Plus;
      descriptor.usage = wgpu::TextureUsage::RenderAttachment;
      target->depth = m_impl->device.CreateTexture(&descriptor);
      target->depthView = target->depth.CreateView();
      checked = scope.finish();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (!target->color || !target->view || !target->depth || !target->depthView) {
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
    auto encoder = m_impl->encodeMeshes(target.m_impl->color, m_impl->offscreenPipeline, *prepared,
                                        info->width, info->height, clear, target.m_impl->depthView);
    auto commands = encoder.Finish();
    m_impl->queue.Submit(1, &commands);
    checked = scope.finish();

    if (!checked) {
      target.m_impl->faulted = true;
      return checked;
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
        [&](const auto&, auto, auto) { return m_impl->device.CreateCommandEncoder(); },
        target.m_impl->color);
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
          [&](const auto& texture, auto, auto) { return m_impl->encodeGui(texture); }, {}, format);
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

      auto presented = m_impl->presentFrame(
          [&](const auto& texture, auto, auto) { return m_impl->encodeGui(texture); });
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

    return m_impl->readback(width, height,
                            [&](const wgpu::Texture& texture, auto targetWidth, auto targetHeight) {
                              return m_impl->encode(texture, m_impl->offscreenPipeline, vertices,
                                                    transform, targetWidth, targetHeight, clear);
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
      return gpu.encode(texture, gpu.surfacePipeline, vertices, transform, width, height, clear);
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
        width, height, [&](const wgpu::Texture& texture, auto targetWidth, auto targetHeight) {
          return m_impl->encodeMeshes(texture, m_impl->offscreenPipeline, *prepared, targetWidth,
                                      targetHeight, clear);
        });
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

    return m_impl->presentFrame([&](const wgpu::Texture& texture, auto width, auto height) {
      return m_impl->encodeMeshes(texture, m_impl->surfacePipeline, *prepared, width, height,
                                  clear);
    });
  }
}  // namespace Dot::Graphics
