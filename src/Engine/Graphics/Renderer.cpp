module;

#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <imgui.h>
#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

module Dot.Graphics.Renderer;

import Dot.Core.Result;
import Dot.Math;
import Dot.Graphics.View;
import Dot.Graphics.ShadowAtlas;
import Dot.Graphics.SpatialIndex;
import Dot.Graphics.RenderQueue;
import Dot.Platform.Window;
import Dot.Platform.System;
import Dot.Platform.Events;

#include "BuiltinShaders.hpp"
#include "FrameProfiler.hpp"
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
        auto valid = validateVertex(vertex);

        if (!valid) {
          return valid;
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

    struct InstanceData {
      Math::Mat4 transform;
      Math::Vec4 tint{1, 1, 1, 1};
      Math::Vec4 emissive{};
      Math::Mat4 model{};
      Math::Mat4 normalMatrix{};
      Math::Vec4 material{0, 0.5F, 1, 0};
      Math::Vec4 flags{0, 0, 0, 1};
      Math::Vec4 occlusion{1, 0, 0, 0};
      Math::Vec4 alpha{0, .5F, 0, 0};
    };

    static_assert(sizeof(Animation::SkinInfluence) == 64);
    static_assert(offsetof(Animation::SkinInfluence, weights) == 32);
    static_assert(sizeof(Math::Mat4) == 64);
    static_assert(sizeof(Vertex) == 64);
    static_assert(offsetof(Vertex, tangent) == 48);
    static_assert(sizeof(InstanceData) == 288);
    static_assert(offsetof(InstanceData, tint) == 64);
    static_assert(offsetof(InstanceData, emissive) == 80);
    static_assert(offsetof(InstanceData, model) == 96);
    static_assert(offsetof(InstanceData, normalMatrix) == 160);
    static_assert(offsetof(InstanceData, material) == 224);
    static_assert(offsetof(InstanceData, occlusion) == 256);
    static_assert(offsetof(InstanceData, alpha) == 272);

    struct CoverageData {
      Math::Mat4 transform;
      Math::Vec4 tint;
      Math::Vec4 flags;
      Math::Vec4 alpha;
      std::array<std::uint32_t, 4> identifier{};
    };

    static_assert(sizeof(CoverageData) == 128);

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
      std::atomic<std::uint64_t> m_skins{};
      std::atomic<std::uint64_t> m_skinBytes{};
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

    /** Callback-owned pipeline results are published only by owner-thread polling.
     * The explicit synchronous path shares the same job and remaining deadline.
     * 管线回调拥有结果，仅所属线程轮询发布；显式同步路径共用任务与剩余期限。 */
    template <typename Pipeline>
    struct PipelineJob {
      Pipeline pipeline;
      std::shared_ptr<Pending<Pipeline>> pending;
      wgpu::Future future{};
      std::chrono::steady_clock::time_point started;
      std::string context;
      std::optional<Core::Error> error;

      auto poll(const wgpu::Instance& instance) -> Core::Result<bool> {
        if (error) {
          return std::unexpected(*error);
        }

        if (pipeline) {
          return true;
        }

        if (!pending) {
          error = Core::fail(Core::ErrorCode::InvalidState, "Pipeline job was not started").error();
          return std::unexpected(*error);
        }

        const auto status = instance.WaitAny(future, 0);

        if (status == wgpu::WaitStatus::TimedOut
            && std::chrono::steady_clock::now() - started < std::chrono::seconds(10)) {
          return false;
        }

        if (status != wgpu::WaitStatus::Success || !pending->value) {
          error
              = Core::fail(Core::ErrorCode::Unavailable,
                           status == wgpu::WaitStatus::Success ? pending->error
                           : status == wgpu::WaitStatus::TimedOut
                               ? std::format("{}: compilation timed out after ten seconds", context)
                               : std::format("{}: GPU poll failed ({})", context,
                                             static_cast<unsigned>(status)))
                    .error();
          return std::unexpected(*error);
        }

        pipeline = std::move(pending->value);
        pending.reset();
        return true;
      }

      auto finish(const wgpu::Instance& instance) -> Core::Result<void> {
        auto ready = poll(instance);

        if (!ready || *ready) {
          return ready ? Core::Result<void>{} : std::unexpected(ready.error());
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
            started + std::chrono::seconds(10) - std::chrono::steady_clock::now());
        const auto status = instance.WaitAny(
            future, static_cast<std::uint64_t>(std::max<std::int64_t>(0, remaining.count())));

        if (status != wgpu::WaitStatus::Success) {
          error = Core::fail(Core::ErrorCode::Unavailable,
                             status == wgpu::WaitStatus::TimedOut
                                 ? context + ": compilation wait timed out"
                                 : std::format("{}: GPU wait failed ({})", context,
                                               static_cast<unsigned>(status)))
                      .error();
          return std::unexpected(*error);
        }

        ready = poll(instance);

        if (!ready) {
          return std::unexpected(ready.error());
        }

        if (!*ready) {
          return Core::fail(Core::ErrorCode::Unavailable, context + ": pipeline did not complete");
        }

        return {};
      }
    };

    template <typename Pipeline, typename Descriptor>
    auto compilePipeline(const wgpu::Device& device, const Descriptor& descriptor,
                         std::string context) -> PipelineJob<Pipeline> {
      PipelineJob<Pipeline> job;
      job.context = std::move(context);
      job.pending = std::make_shared<Pending<Pipeline>>();
      job.pending->error = job.context + " failed";
      job.started = std::chrono::steady_clock::now();
      auto callback = [pending = job.pending, context = job.context](
                          wgpu::CreatePipelineAsyncStatus status, Pipeline pipeline,
                          wgpu::StringView message) noexcept {
        if (status == wgpu::CreatePipelineAsyncStatus::Success && pipeline) {
          pending->value = std::move(pipeline);
        } else {
          describeFailure(pending->error, context, static_cast<unsigned>(status), message);
        }
      };

      if constexpr (std::is_same_v<Pipeline, wgpu::ComputePipeline>) {
        job.future = device.CreateComputePipelineAsync(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
                                                       std::move(callback));
      } else {
        job.future = device.CreateRenderPipelineAsync(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
                                                      std::move(callback));
      }

      return job;
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
    bool hasTangents{};
    bool m_uvFrame{};
    bool m_skinned{};
    std::uint64_t m_geometryRevision{};
    float m_poseOrientation{1};
    std::shared_ptr<Impl> m_source;
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

  struct SkinnedMesh::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    std::shared_ptr<Diagnostics> m_owner;
    Animation::Skin m_skin;
    Mesh m_mesh;
    std::array<wgpu::Buffer, 2> m_vertices;
    wgpu::Buffer m_weights;
    wgpu::Buffer m_palette;
    wgpu::Buffer m_report;
    wgpu::Buffer m_parameters;
    wgpu::Buffer m_indices;
    SkinInfo m_info{};
    std::uint32_t m_next{1};
    bool m_tracked{};

    ~Impl() {
      if (m_tracked) {
        m_owner->m_skins.fetch_sub(1);
        m_owner->m_skinBytes.fetch_sub(m_info.bytes);
      }
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || !m_owner->alive.load()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Skin requires its live device/owner thread");
      }

      return m_owner->failed.load() ? Core::fail(Core::ErrorCode::Unavailable, "Skin device failed")
                                    : Core::Result<void>{};
    }
  };

  SkinnedMesh::SkinnedMesh() noexcept = default;

  SkinnedMesh::SkinnedMesh(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SkinnedMesh::SkinnedMesh(const SkinnedMesh&) noexcept = default;

  auto SkinnedMesh::operator=(const SkinnedMesh&) noexcept -> SkinnedMesh& = default;

  SkinnedMesh::SkinnedMesh(SkinnedMesh&&) noexcept = default;

  auto SkinnedMesh::operator=(SkinnedMesh&&) noexcept -> SkinnedMesh& = default;

  SkinnedMesh::~SkinnedMesh() = default;

  auto SkinnedMesh::info() const -> Core::Result<SkinInfo> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Skin is empty or moved");
    }

    auto valid = m_impl->check();
    return valid ? Core::Result<SkinInfo>{m_impl->m_info} : std::unexpected(valid.error());
  }

  auto SkinnedMesh::mesh() const -> Core::Result<Mesh> {
    auto valid = info();
    return valid ? Core::Result<Mesh>{m_impl->m_mesh} : std::unexpected(valid.error());
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

    struct EnvironmentPreparation {
      wgpu::BindGroupLayout layout;
      PipelineJob<wgpu::ComputePipeline> job;
    };

    struct ShadowPreparation {
      std::array<PipelineJob<wgpu::RenderPipeline>, 3> jobs;
      std::optional<Core::Error> error;
    };

    struct OcclusionPreparation {
      std::array<PipelineJob<wgpu::RenderPipeline>, 3> render;
      std::array<PipelineJob<wgpu::ComputePipeline>, 2> compute;
      std::optional<Core::Error> error;
    };

    std::optional<EnvironmentPreparation> m_environmentPreparation;
    std::optional<ShadowPreparation> m_shadowPreparation;
    std::optional<OcclusionPreparation> m_occlusionPreparation;
    wgpu::BindGroupLayout m_skinLayout;
    wgpu::ComputePipeline m_skinVertices;
    wgpu::ComputePipeline m_skinFaces;
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
    std::array<wgpu::RenderPipeline, 8> blendPipelines;
    wgpu::RenderPipeline environmentPipeline;
    wgpu::RenderPipeline skyPipeline;
    wgpu::RenderPipeline shadowScenePipeline;
    wgpu::RenderPipeline shadowEnvironmentPipeline;
    wgpu::RenderPipeline shadowDepthPipeline;
    wgpu::Texture shadowTexture;

    struct ShadowCasterState {
      std::weak_ptr<Mesh::Impl> mesh;
      std::uint64_t revision;
      Math::Mat4 model;
      bool masked;
      float opacity;
      float cutoff;
      std::weak_ptr<Texture::Impl> texture;

      auto matches(const ShadowCasterState& other) const -> bool {
        const auto sameOwner = [](const auto& left, const auto& right) {
          return !left.owner_before(right) && !right.owner_before(left);
        };
        return sameOwner(mesh, other.mesh) && revision == other.revision && model == other.model
               && masked == other.masked
               && (!masked
                   || (opacity == other.opacity && cutoff == other.cutoff
                       && sameOwner(texture, other.texture)));
      }
    };

    struct ShadowFaceState {
      ShadowTile tile;
      Math::Mat4 matrix;
      std::vector<std::uint32_t> casters;
    };

    /** Exact depth dependencies, with weak resource identity and no content hashes.
     * One bounded draw table and face index lists avoid duplicating caster data.
     * 精确深度依赖，使用弱资源身份而非内容哈希；一份有界绘制表及逐面索引避免复制投射物数据。 */
    struct ShadowCacheState {
      std::vector<ShadowCasterState> casters;
      std::vector<ShadowFaceState> faces;
    };

    ShadowCacheState m_shadowCache;
    std::optional<ShadowCacheState> m_pendingShadowCache;

    auto commitShadowCache() -> void {
      if (m_pendingShadowCache) {
        m_shadowCache = std::move(*m_pendingShadowCache);
        m_pendingShadowCache.reset();
      }
    }

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
    std::uint32_t shadowLayerLimit{256};
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
    std::vector<Texture> guiTextures;

    struct GuiViewportRequest {
      Detail::GuiImageSlot slot;
      GuiViewportDraw draw;
    };

    std::vector<GuiViewportRequest> guiViewports;
    bool guiFrameActive{};
    VisibilityStats visibility;
    MeshFrameStats pendingVisibility;
    SpatialIndex spatialIndex;
    SpatialStats preparedSpatial;
    std::uint64_t maxInstanceBufferBytes{};
    bool m_timestampQueries{};
    std::uint64_t m_profilingSession{};
    std::unique_ptr<Detail::FrameProfiler> m_profiler;

    struct ProfileScope {
      Detail::FrameProfiler* profiler;

      explicit ProfileScope(Impl& owner) : profiler(owner.m_profiler.get()) {
        if (profiler) {
          profiler->begin();
        }
      }

      ProfileScope(const ProfileScope&) = delete;
      auto operator=(const ProfileScope&) -> ProfileScope& = delete;

      ~ProfileScope() {
        if (profiler) {
          profiler->cancel();
        }
      }

      auto prepared() -> void {
        if (profiler) {
          profiler->prepared();
        }
      }
    };

    auto profileWrites(Detail::ProfilePass pass) -> wgpu::PassTimestampWrites {
      return m_profiler ? m_profiler->writes(pass) : wgpu::PassTimestampWrites{};
    }

    struct GuiFrameScope {
      Impl& owner;

      explicit GuiFrameScope(Impl& value) : owner(value) {
        owner.guiFrameActive = true;
        owner.guiViewports.clear();
        owner.guiTargets.clear();
        owner.guiTextures.clear();
      }

      ~GuiFrameScope() {
        owner.guiViewports.clear();
        owner.guiTargets.clear();
        owner.guiTextures.clear();
        owner.guiFrameActive = false;
      }
    };

    auto resolveGuiViewports() -> Core::Result<void> {
      auto requests = std::exchange(guiViewports, {});

      for (const auto& request : requests) {
        auto target = request.draw(request.slot.width, request.slot.height);

        if (!target) {
          return Core::fail(target.error().code,
                            "Deferred GUI viewport: " + target.error().message);
        }

        WGPUTextureView view{};

        if (*target) {
          auto& resolved = (**target).target;

          if (!resolved.m_impl || resolved.m_impl->owner != diagnostics) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Deferred GUI viewport returned a foreign or empty target");
          }

          auto targetInfo = resolved.info();

          if (!targetInfo) {
            return std::unexpected(targetInfo.error());
          }

          if (!targetInfo->frames || targetInfo->width != request.slot.width
              || targetInfo->height != request.slot.height) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Deferred GUI viewport must complete its current pixel dimensions");
          }

          guiTargets.push_back(resolved);
          view = resolved.m_impl->view.Get();
        }

        auto resolved = gui->resolveImage(request.slot.index, view);

        if (!resolved) {
          return resolved;
        }

        if (*target) {
          auto overlay = gui->resolveOverlay(request.slot.index, (**target).overlay);

          if (!overlay) {
            return overlay;
          }
        }
      }

      return {};
    }

    ~Impl() {
      m_profiler.reset();
      guiTargets.clear();
      guiTextures.clear();
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

    template <typename Create>
    auto surfacePipeline(wgpu::TextureFormat format, bool withEnvironment, bool withShadows,
                         bool withOcclusion, bool blend, Create create)
        -> std::invoke_result_t<Create, const wgpu::RenderPipelineDescriptor*> {
      wgpu::ShaderSourceWGSL source;
      const auto shaderCode
          = std::string(withEnvironment ? Detail::surfaceEnvironmentShader
                                        : Detail::surfaceDirectShader)
            + "\n"
            + (withShadows ? Detail::visibilityShadowsShader : Detail::visibilityDirectShader)
            + "\n"
            + (withOcclusion ? Detail::ambientOcclusionShader : Detail::ambientUnoccludedShader)
            + "\n" + Detail::coverageShader + "\n" + Detail::surfaceShader;
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
                                                        .shaderLocation = 3},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4,
                                                        .offset = offsetof(Vertex, tangent),
                                                        .shaderLocation = 4}};
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.stepMode = wgpu::VertexStepMode::Vertex;
      layout.attributeCount = attributes.size();
      layout.attributes = attributes.data();
      wgpu::ColorTargetState target;
      target.format = format;
      wgpu::BlendState blending;
      blending.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
      blending.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
      blending.alpha.srcFactor = wgpu::BlendFactor::One;
      blending.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
      target.blend = blend ? &blending : nullptr;
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
      fragment.targetCount = 1;
      fragment.targets = &target;
      wgpu::DepthStencilState depth;
      depth.format = wgpu::TextureFormat::Depth24Plus;
      depth.depthWriteEnabled = !blend;
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
      return create(&descriptor);
    }

    auto pipeline(wgpu::TextureFormat format, bool withEnvironment = false,
                  bool withShadows = false, bool withOcclusion = false, bool blend = false)
        -> wgpu::RenderPipeline {
      return surfacePipeline(format, withEnvironment, withShadows, withOcclusion, blend,
                             [&](const wgpu::RenderPipelineDescriptor* descriptor) {
                               return device.CreateRenderPipeline(descriptor);
                             });
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

    auto shadowPlan(const Lighting& lighting) const -> Core::Result<ShadowAtlas> {
      std::array<ShadowAtlasRequest, 64> requests;
      std::size_t count{};

      for (const auto& light : lighting.lights) {
        if (light.shadows) {
          const auto maps = light.kind == LightKind::Point  ? 6u
                            : light.kind == LightKind::Spot ? 1u
                                                            : light.shadows->cascades;
          requests[count++] = {light.shadows->resolution, maps};
        }
      }

      return planShadowAtlas(std::span(requests.data(), count), config.maxShadowBytes,
                             shadowLayerLimit);
    }

    template <typename Create>
    auto shadowDepthPipelineFor(Create create)
        -> std::invoke_result_t<Create, const wgpu::RenderPipelineDescriptor*> {
      wgpu::ShaderSourceWGSL source;
      const auto shadowCode
          = std::string(Detail::coverageShader) + "\n" + Detail::shadowDepthShader;
      source.code = shadowCode.c_str();
      wgpu::ShaderModuleDescriptor module;
      module.nextInChain = &source;
      auto shader = device.CreateShaderModule(&module);
      const std::array attributes{wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, position),
                                                        .shaderLocation = 0},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4,
                                                        .offset = offsetof(Vertex, color),
                                                        .shaderLocation = 1},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2,
                                                        .offset = offsetof(Vertex, texCoord),
                                                        .shaderLocation = 2}};
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.attributeCount = attributes.size();
      layout.attributes = attributes.data();
      wgpu::FragmentState fragment;
      fragment.module = shader;
      fragment.entryPoint = "fragmentMain";
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
      return create(&descriptor);
    }

    /** Finish synchronous descriptor validation before issuing asynchronous work;
     * compilation errors belong to the job callback, never a borrowed error scope.
     * 异步提交前完成同步描述校验；编译错误归任务回调，不借用错误作用域。 */
    template <typename Build>
    auto compileRenderPipeline(Build build, std::string context)
        -> Core::Result<PipelineJob<wgpu::RenderPipeline>> {
      ErrorScope scope(instance, device);
      return build([&](const wgpu::RenderPipelineDescriptor* descriptor)
                       -> Core::Result<PipelineJob<wgpu::RenderPipeline>> {
        auto checked = scope.finish();

        if (!checked) {
          return Core::fail(checked.error().code, context + ": " + checked.error().message);
        }

        return compilePipeline<wgpu::RenderPipeline>(device, *descriptor, std::move(context));
      });
    }

    auto prepareShadowPipelines() -> Core::Result<bool> {
      if (shadowScenePipeline && shadowEnvironmentPipeline && shadowDepthPipeline) {
        return true;
      }

      if (m_shadowPreparation && m_shadowPreparation->error) {
        return std::unexpected(*m_shadowPreparation->error);
      }

      try {
        if (!m_shadowPreparation) {
          m_shadowPreparation.emplace();
          auto direct = compileRenderPipeline(
              [&](auto create) {
                return surfacePipeline(wgpu::TextureFormat::RGBA16Float, false, true, false, false,
                                       std::move(create));
              },
              "Shadow direct pipeline");

          if (!direct) {
            m_shadowPreparation->error = direct.error();
            return std::unexpected(direct.error());
          }

          m_shadowPreparation->jobs[0] = std::move(*direct);
          auto ambient = compileRenderPipeline(
              [&](auto create) {
                return surfacePipeline(wgpu::TextureFormat::RGBA16Float, true, true, false, false,
                                       std::move(create));
              },
              "Shadow environment pipeline");

          if (!ambient) {
            m_shadowPreparation->error = ambient.error();
            return std::unexpected(ambient.error());
          }

          m_shadowPreparation->jobs[1] = std::move(*ambient);
          auto depth = compileRenderPipeline(
              [&](auto create) { return shadowDepthPipelineFor(std::move(create)); },
              "Shadow depth pipeline");

          if (!depth) {
            m_shadowPreparation->error = depth.error();
            return std::unexpected(depth.error());
          }

          m_shadowPreparation->jobs[2] = std::move(*depth);
        }

        bool complete = true;

        for (auto& job : m_shadowPreparation->jobs) {
          auto ready = job.poll(instance);

          if (!ready) {
            m_shadowPreparation->error = ready.error();
            return std::unexpected(ready.error());
          }

          complete = complete && *ready;
        }

        if (!complete) {
          return false;
        }

        shadowScenePipeline = std::move(m_shadowPreparation->jobs[0].pipeline);
        shadowEnvironmentPipeline = std::move(m_shadowPreparation->jobs[1].pipeline);
        shadowDepthPipeline = std::move(m_shadowPreparation->jobs[2].pipeline);
        return true;
      } catch (const std::exception& error) {
        auto failure = Core::fail(Core::ErrorCode::External,
                                  "Prepare shadow pipelines: " + std::string(error.what()));
        m_shadowPreparation->error = failure.error();
        return failure;
      }
    }

    auto ensureShadowPipelines() -> Core::Result<void> {
      auto ready = prepareShadowPipelines();

      if (!ready || *ready) {
        return ready ? Core::Result<void>{} : std::unexpected(ready.error());
      }

      for (auto& job : m_shadowPreparation->jobs) {
        auto finished = job.finish(instance);

        if (!finished) {
          m_shadowPreparation->error = finished.error();
          return finished;
        }
      }

      ready = prepareShadowPipelines();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        return Core::fail(Core::ErrorCode::Unavailable, "Shadow pipelines did not complete");
      }

      return {};
    }

    auto ensureShadowMaps(const ShadowAtlas& atlas) -> Core::Result<void> {
      if (shadowResolution == atlas.dimension && shadowLayers == atlas.layers) {
        return {};
      }

      if (atlas.dimension > info.maxTextureDimension) {
        return Core::fail(Core::ErrorCode::Unavailable, "Shadow resolution exceeds device limits");
      }

      ErrorScope scope(instance, device);
      wgpu::TextureDescriptor descriptor;
      descriptor.size = {atlas.dimension, atlas.dimension, atlas.layers};
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
      m_shadowCache = {};
      m_pendingShadowCache.reset();
      shadowResolution = atlas.dimension;
      shadowLayers = atlas.layers;
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
      const auto pickCode = std::string(Detail::coverageShader) + "\n" + Detail::pickingShader;
      source.code = pickCode.c_str();
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
                                                        .shaderLocation = 2}};
      wgpu::VertexBufferLayout layout;
      layout.arrayStride = sizeof(Vertex);
      layout.attributeCount = attributes.size();
      layout.attributes = attributes.data();
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

    template <typename Create>
    auto geometryPipelineFor(Create create)
        -> std::invoke_result_t<Create, const wgpu::RenderPipelineDescriptor*> {
      const auto module = [&](const char* code) {
        wgpu::ShaderSourceWGSL source;
        source.code = code;
        wgpu::ShaderModuleDescriptor descriptor;
        descriptor.nextInChain = &source;
        return device.CreateShaderModule(&descriptor);
      };

      const auto geometryCode = std::string(Detail::coverageShader) + "\n" + Detail::geometryShader;
      auto shader = module(geometryCode.c_str());
      const std::array attributes{wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, position),
                                                        .shaderLocation = 0},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x3,
                                                        .offset = offsetof(Vertex, normal),
                                                        .shaderLocation = 3},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4,
                                                        .offset = offsetof(Vertex, color),
                                                        .shaderLocation = 1},
                                  wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2,
                                                        .offset = offsetof(Vertex, texCoord),
                                                        .shaderLocation = 2}};
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
      return create(&descriptor);
    }

    auto compileOcclusionCompute(const char* code, const char* entry, std::string context)
        -> Core::Result<PipelineJob<wgpu::ComputePipeline>> {
      ErrorScope scope(instance, device);
      wgpu::ShaderSourceWGSL source;
      source.code = code;
      wgpu::ShaderModuleDescriptor shaderDescriptor;
      shaderDescriptor.nextInChain = &source;
      auto shader = device.CreateShaderModule(&shaderDescriptor);
      wgpu::ComputePipelineDescriptor descriptor;
      descriptor.compute.module = shader;
      descriptor.compute.entryPoint = entry;
      auto checked = scope.finish();

      if (!checked) {
        return Core::fail(checked.error().code, context + ": " + checked.error().message);
      }

      return compilePipeline<wgpu::ComputePipeline>(device, descriptor, std::move(context));
    }

    auto prepareOcclusionPipelines() -> Core::Result<bool> {
      if (geometryPipeline && occlusionPipeline && occlusionFilterPipeline
          && occlusionEnvironmentPipeline && occlusionShadowEnvironmentPipeline) {
        return true;
      }

      if (m_occlusionPreparation && m_occlusionPreparation->error) {
        return std::unexpected(*m_occlusionPreparation->error);
      }

      try {
        if (!m_occlusionPreparation) {
          m_occlusionPreparation.emplace();
          auto geometry = compileRenderPipeline(
              [&](auto create) { return geometryPipelineFor(std::move(create)); },
              "Occlusion geometry pipeline");

          if (!geometry) {
            m_occlusionPreparation->error = geometry.error();
            return std::unexpected(geometry.error());
          }

          m_occlusionPreparation->render[0] = std::move(*geometry);
          auto ao = compileOcclusionCompute(Detail::occlusionShader, "compute",
                                            "Occlusion search pipeline");

          if (!ao) {
            m_occlusionPreparation->error = ao.error();
            return std::unexpected(ao.error());
          }

          m_occlusionPreparation->compute[0] = std::move(*ao);
          auto filter = compileOcclusionCompute(Detail::occlusionFilterShader, "filterOcclusion",
                                                "Occlusion filter pipeline");

          if (!filter) {
            m_occlusionPreparation->error = filter.error();
            return std::unexpected(filter.error());
          }

          m_occlusionPreparation->compute[1] = std::move(*filter);

          for (std::size_t index = 0; index < 2; ++index) {
            auto ambient = compileRenderPipeline(
                [&](auto create) {
                  return surfacePipeline(wgpu::TextureFormat::RGBA16Float, true, index == 1, true,
                                         false, std::move(create));
                },
                index == 0 ? "Occlusion environment pipeline"
                           : "Occlusion shadow environment pipeline");

            if (!ambient) {
              m_occlusionPreparation->error = ambient.error();
              return std::unexpected(ambient.error());
            }

            m_occlusionPreparation->render[index + 1] = std::move(*ambient);
          }
        }

        bool complete = true;
        const auto poll = [&](auto& jobs) -> Core::Result<void> {
          for (auto& job : jobs) {
            auto ready = job.poll(instance);

            if (!ready) {
              return std::unexpected(ready.error());
            }

            complete = complete && *ready;
          }

          return {};
        };

        auto checked = poll(m_occlusionPreparation->render);

        if (checked) {
          checked = poll(m_occlusionPreparation->compute);
        }

        if (!checked) {
          m_occlusionPreparation->error = checked.error();
          return std::unexpected(checked.error());
        }

        if (!complete) {
          return false;
        }

        geometryPipeline = std::move(m_occlusionPreparation->render[0].pipeline);
        occlusionEnvironmentPipeline = std::move(m_occlusionPreparation->render[1].pipeline);
        occlusionShadowEnvironmentPipeline = std::move(m_occlusionPreparation->render[2].pipeline);
        occlusionPipeline = std::move(m_occlusionPreparation->compute[0].pipeline);
        occlusionFilterPipeline = std::move(m_occlusionPreparation->compute[1].pipeline);
        return true;
      } catch (const std::exception& error) {
        auto failure = Core::fail(Core::ErrorCode::External,
                                  "Prepare occlusion pipelines: " + std::string(error.what()));

        if (m_occlusionPreparation) {
          m_occlusionPreparation->error = failure.error();
        }

        return failure;
      }
    }

    auto ensureOcclusionPipelines() -> Core::Result<void> {
      auto ready = prepareOcclusionPipelines();

      if (!ready || *ready) {
        return ready ? Core::Result<void>{} : std::unexpected(ready.error());
      }

      const auto finish = [&](auto& jobs) -> Core::Result<void> {
        for (auto& job : jobs) {
          auto checked = job.finish(instance);

          if (!checked) {
            m_occlusionPreparation->error = checked.error();
            return checked;
          }
        }

        return {};
      };

      auto checked = finish(m_occlusionPreparation->render);

      if (checked) {
        checked = finish(m_occlusionPreparation->compute);
      }

      if (!checked) {
        return checked;
      }

      ready = prepareOcclusionPipelines();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        return Core::fail(Core::ErrorCode::Unavailable, "Occlusion pipelines did not complete");
      }

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
                    const wgpu::Texture& output,
                    const wgpu::PassTimestampWrites* timestamps = nullptr) -> void {
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
      passDescriptor.timestampWrites = timestamps;
      auto pass = encoder.BeginRenderPass(&passDescriptor);
      pass.SetPipeline(renderPipeline);
      pass.SetBindGroup(0, group);
      pass.Draw(3);
      pass.End();
    }

    template <typename F>
    auto encodePass(const wgpu::Texture& target, std::uint32_t width, std::uint32_t height,
                    Math::Vec4 clear, F&& draw, wgpu::TextureView depthView = {},
                    wgpu::CommandEncoder encoder = {},
                    const wgpu::PassTimestampWrites* timestamps = nullptr) -> wgpu::CommandEncoder {
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
      descriptor.timestampWrites = timestamps;

      if (!encoder) {
        encoder = device.CreateCommandEncoder();
      }

      auto pass = encoder.BeginRenderPass(&descriptor);

      draw(pass);
      pass.End();
      return encoder;
    }

    auto bindCoverage(const wgpu::RenderPipeline& pipeline, const wgpu::Buffer& buffer,
                      const std::shared_ptr<Texture::Impl>& texture) -> wgpu::BindGroup {
      std::array<wgpu::BindGroupEntry, 3> entries{};
      entries[0].buffer = buffer;
      entries[0].size = buffer.GetSize();
      entries[1].binding = 1;
      entries[1].textureView = texture ? texture->view : whiteView;
      entries[2].binding = 2;
      entries[2].sampler = texture ? texture->sampler : whiteSampler;
      wgpu::BindGroupDescriptor group;
      group.layout = pipeline.GetBindGroupLayout(0);
      group.entryCount = entries.size();
      group.entries = entries.data();
      return device.CreateBindGroup(&group);
    }

    auto bindMaterial(const wgpu::RenderPipeline& renderPipeline, const wgpu::Buffer& buffer,
                      const std::array<std::shared_ptr<Texture::Impl>, 5>& textures = {})
        -> wgpu::BindGroup {
      std::array<wgpu::BindGroupEntry, 11> entries{};
      entries[0].binding = 0;
      entries[0].buffer = buffer;
      entries[0].size = buffer.GetSize();

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
              const InstanceData data{transform};
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

    /** Outward-rounded affine AABB encloses double-precision corner transforms.
     * Projective/float-unrepresentable bounds use homogeneous tests directly.
     * 仿射 AABB 向外舍入包围双精度角点；投影或不可用 float 表达的边界直接齐次测试。 */
    static auto worldBounds(const Bounds& local, const Math::Mat4& world) -> std::optional<Bounds> {
      const auto& m = world.data;

      if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1) {
        return std::nullopt;
      }

      std::array<double, 3> minimum;
      std::array<double, 3> maximum;
      minimum.fill(std::numeric_limits<double>::infinity());
      maximum.fill(-std::numeric_limits<double>::infinity());

      for (std::size_t corner = 0; corner < 8; ++corner) {
        const double p[4]{corner & 1 ? local.maximum.x : local.minimum.x,
                          corner & 2 ? local.maximum.y : local.minimum.y,
                          corner & 4 ? local.maximum.z : local.minimum.z, 1};

        for (std::size_t row = 0; row < 3; ++row) {
          double value{};

          for (std::size_t column = 0; column < 4; ++column) {
            value += static_cast<double>(m[column * 4 + row]) * p[column];
          }

          minimum[row] = std::min(minimum[row], value);
          maximum[row] = std::max(maximum[row], value);
        }
      }

      std::array<float, 3> low;
      std::array<float, 3> high;

      for (std::size_t i = 0; i < 3; ++i) {
        if (minimum[i] < -std::numeric_limits<float>::max()
            || maximum[i] > std::numeric_limits<float>::max()) {
          return std::nullopt;
        }

        low[i] = std::nextafter(static_cast<float>(minimum[i]),
                                -std::numeric_limits<float>::infinity());
        high[i] = std::nextafter(static_cast<float>(maximum[i]),
                                 std::numeric_limits<float>::infinity());

        if (!std::isfinite(low[i]) || !std::isfinite(high[i])) {
          return std::nullopt;
        }
      }

      return Bounds{{low[0], low[1], low[2]}, {high[0], high[1], high[2]}};
    }

    struct PreparedDraw {
      std::shared_ptr<Mesh::Impl> mesh;
      InstanceData uniform;
      std::array<std::shared_ptr<Texture::Impl>, 5> textures;
      bool castsShadow;
      bool visible;
    };

    auto prepare(std::span<const MeshDraw> draws, const Math::Mat4& viewProjection,
                 Math::Vec4 clear) -> Core::Result<std::vector<PreparedDraw>> {
      if (draws.size() > 10000 || !Math::isFinite(viewProjection) || !radianceValid(clear)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid mesh draw count, camera, or clear color");
      }

      auto frustum = Frustum::create(viewProjection);

      if (!frustum) {
        return std::unexpected(frustum.error());
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
        auto coverage = validateCoverage(material.alphaMode, material.alphaCutoff);

        if (!coverage) {
          return std::unexpected(coverage.error());
        }

        if ((material.model != SurfaceModel::Unlit
             && material.model != SurfaceModel::MetallicRoughness)
            || !std::isfinite(material.metallic) || material.metallic < 0 || material.metallic > 1
            || !std::isfinite(material.roughness) || material.roughness < 0
            || material.roughness > 1 || !std::isfinite(material.normalScale)
            || material.normalScale < 0 || material.normalScale > 8
            || !std::isfinite(material.occlusionStrength) || material.occlusionStrength < 0
            || material.occlusionStrength > 1) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid physical material properties");
        }

        if (material.model == SurfaceModel::Unlit
            && (material.metallicRoughnessTexture || material.normalTexture
                || material.occlusionTexture)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Physical data textures require a physical material");
        }

        std::array<std::shared_ptr<Texture::Impl>, 5> textures{};
        const std::array slots{&material.baseColorTexture, &material.metallicRoughnessTexture,
                               &material.normalTexture, &material.emissiveTexture,
                               &material.occlusionTexture};

        for (std::size_t index = 0; index < slots.size(); ++index) {
          if (*slots[index]) {
            textures[index] = (*slots[index])->m_impl;

            if (!textures[index] || textures[index]->owner != diagnostics
                || ((index == 1 || index == 2 || index == 4)
                    && textures[index]->info.options.colorSpace != ColorSpace::Linear)) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Invalid/foreign material texture or nonlinear data texture");
            }
          }
        }

        Math::Mat4 normalMatrix;
        float orientation = 1;
        float worldOrientation = 1;

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
          worldOrientation = determinant < 0 ? -1.0F : 1.0F;
          orientation = worldOrientation * draw.mesh->m_impl->m_poseOrientation;
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
               material.normalTexture ? (draw.mesh->m_impl->hasTangents ? 2.0F : 1.0F) : 0.0F,
               material.doubleSided ? 1.0F : 0.0F, orientation},
              {material.occlusionStrength, worldOrientation, 0, 0},
              {static_cast<float>(material.alphaMode), material.alphaCutoff, 0, 0}},
             std::move(textures),
             draw.castsShadow && material.alphaMode != AlphaMode::Blend,
             false});
      }

      std::vector<Bounds> bounds;
      std::vector<std::size_t> indexed;
      std::vector<std::size_t> unindexed;
      bounds.reserve(prepared.size());
      indexed.reserve(prepared.size());

      for (std::size_t i = 0; i < prepared.size(); ++i) {
        const auto& draw = prepared[i];
        const auto box = worldBounds(draw.mesh->bounds, draw.uniform.model);

        if (box) {
          bounds.push_back(*box);
          indexed.push_back(i);
        } else {
          unindexed.push_back(i);
        }
      }

      auto update = spatialIndex.update(bounds);

      if (!update) {
        return std::unexpected(update.error());
      }

      auto query = spatialIndex.query(*frustum);

      if (!query) {
        return std::unexpected(query.error());
      }

      SpatialStats stats;
      stats.indexedInstances = indexed.size();
      stats.nodeTests = query->nodeTests;
      stats.boundsTests = query->boundsTests;
      stats.rebuilt = update->rebuilt;
      stats.changedBounds = update->changedBounds;
      stats.refittedNodes = update->refittedNodes;
      const auto exact = [&](std::size_t i) -> Core::Result<void> {
        ++stats.exactTests;
        auto& draw = prepared[i];
        auto hit = frustum->intersects(draw.mesh->bounds, draw.uniform.model);

        if (!hit) {
          return std::unexpected(hit.error());
        }

        draw.visible = *hit;
        return {};
      };

      for (const auto& hit : query->hits) {
        const auto i = indexed[hit.index];

        if (hit.inside) {
          prepared[i].visible = true;
        } else if (auto result = exact(i); !result) {
          return std::unexpected(result.error());
        }
      }

      for (const auto i : unindexed) {
        ++stats.unindexedTests;
        auto result = exact(i);

        if (!result) {
          return std::unexpected(result.error());
        }
      }

      preparedSpatial = stats;
      return prepared;
    }

    enum class BatchPass { Surface, Geometry, Shadow, Picking };

    auto batches(std::span<const PreparedDraw> draws, BatchPass pass = BatchPass::Surface,
                 const Frustum* frustum = nullptr) -> Core::Result<DrawBatches> {
      std::vector<BatchItem> items;
      items.reserve(draws.size());

      for (const auto& draw : draws) {
        BatchItem item{
            reinterpret_cast<std::uintptr_t>(draw.mesh.get()),
            {},
            pass == BatchPass::Shadow
                ? draw.castsShadow
                : draw.visible && (pass != BatchPass::Geometry || draw.uniform.alpha.x != 2)};

        if (item.enabled && frustum) {
          auto hit = frustum->intersects(draw.mesh->bounds, draw.uniform.model);

          if (!hit) {
            return std::unexpected(hit.error());
          }

          item.enabled = *hit;
        }

        item.textures[0] = draw.uniform.alpha.x == 0
                               ? 0
                               : reinterpret_cast<std::uintptr_t>(draw.textures[0].get());

        if (pass == BatchPass::Surface) {
          for (std::size_t index = 0; index < draw.textures.size(); ++index) {
            item.textures[index] = reinterpret_cast<std::uintptr_t>(draw.textures[index].get());
          }
        }

        items.push_back(item);
      }

      return buildDrawBatches(items);
    }

    /** Clip-center depth orders perspective/orthographic blends. For a center
     * behind/on the eye horizon use the furthest positive-weight corner; this
     * remains approximate object sorting, not order-independent transparency.
     * 裁剪空间中心深度用于透视/正交排序；中心在眼平面后/上时使用最远正权重角点，
     * 仍是物体排序近似，不是顺序无关透明。 */
    auto surfaceQueue(std::span<const PreparedDraw> draws) -> Core::Result<RenderQueue> {
      std::vector<QueueItem> items;
      items.reserve(draws.size());

      for (const auto& draw : draws) {
        QueueItem item{{reinterpret_cast<std::uintptr_t>(draw.mesh.get()), {}, draw.visible},
                       static_cast<AlphaMode>(static_cast<int>(draw.uniform.alpha.x)),
                       0};

        for (std::size_t i = 0; i < draw.textures.size(); ++i) {
          item.binding.textures[i] = reinterpret_cast<std::uintptr_t>(draw.textures[i].get());
        }

        if (draw.visible && item.alpha == AlphaMode::Blend) {
          const auto& box = draw.mesh->bounds;
          const auto& m = draw.uniform.transform.data;
          const double center[4]{(double(box.minimum.x) + box.maximum.x) * .5,
                                 (double(box.minimum.y) + box.maximum.y) * .5,
                                 (double(box.minimum.z) + box.maximum.z) * .5, 1};
          double z{}, w{};

          for (std::size_t j = 0; j < 4; ++j) {
            z += m[j * 4 + 2] * center[j];
            w += m[j * 4 + 3] * center[j];
          }

          if (w > 0) {
            item.depth = z / w;
          } else {
            for (int corner = 0; corner < 8; ++corner) {
              const double p[4]{corner & 1 ? box.maximum.x : box.minimum.x,
                                corner & 2 ? box.maximum.y : box.minimum.y,
                                corner & 4 ? box.maximum.z : box.minimum.z, 1};
              double depth{}, weight{};

              for (std::size_t j = 0; j < 4; ++j) {
                depth += m[j * 4 + 2] * p[j];
                weight += m[j * 4 + 3] * p[j];
              }

              if (weight > 0) {
                item.depth = std::max(item.depth, depth / weight);
              }
            }
          }
        }

        items.push_back(item);
      }

      return buildRenderQueue(items);
    }

    auto instanceBuffer(const void* data, std::size_t bytes) -> Core::Result<wgpu::Buffer> {
      if (!bytes || bytes > maxInstanceBufferBytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Instance storage exceeds device binding limits");
      }

      wgpu::BufferDescriptor descriptor;
      descriptor.size = bytes;
      descriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);

      if (!buffer) {
        return Core::fail(Core::ErrorCode::External, "Allocate instance storage failed");
      }

      queue.WriteBuffer(buffer, 0, data, bytes);
      return buffer;
    }

    auto surfaceBuffer(std::span<const PreparedDraw> draws, const DrawBatches& plan)
        -> Core::Result<wgpu::Buffer> {
      std::vector<InstanceData> data;
      data.reserve(plan.indices.size());

      for (const auto index : plan.indices) {
        data.push_back(draws[index].uniform);
      }

      return instanceBuffer(data.data(), data.size() * sizeof(InstanceData));
    }

    static auto emitInstances(const wgpu::RenderPassEncoder& pass, const Mesh::Impl& mesh,
                              const DrawBatch& batch) -> void {
      pass.SetVertexBuffer(0, mesh.vertices);

      if (mesh.info.indices) {
        pass.SetIndexBuffer(mesh.indices, wgpu::IndexFormat::Uint32);
        pass.DrawIndexed(mesh.info.indices, batch.count, 0, 0, batch.first);
      } else {
        pass.Draw(mesh.info.vertices, batch.count, 0, batch.first);
      }
    }

    struct ShadowData {
      std::array<Math::Mat4, 6> matrices;
      std::array<float, 4> splits;
      Math::Vec4 origin;
      Math::Vec4 forward;
      Math::Vec4 bias;
      Math::Vec4 meta;
      std::array<Math::Vec4, 6> tiles;
    };

    static_assert(sizeof(ShadowData) == 560);

    struct FrameShadow {
      std::size_t source;
      ShadowProjection fit;
    };

    auto shadowCasterBounds(std::span<const PreparedDraw> draws)
        -> Core::Result<std::vector<Bounds>> {
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

      return casters;
    }

    auto fitShadows(const Math::Mat4& camera, std::size_t source, std::span<const Bounds> casters)
        -> Core::Result<ShadowProjection> {
      const auto& light = config.lighting.lights[source];

      if (light.kind == LightKind::Point) {
        return fitPointShadows(light.position, light.range, *light.shadows);
      }

      if (light.kind == LightKind::Spot) {
        return fitSpotShadow(light.position, light.direction, light.outerConeDegrees, light.range,
                             *light.shadows);
      }

      return fitDirectionalShadows(camera, config.lighting.viewPosition, light.direction,
                                   *light.shadows, casters);
    }

    auto encodeShadows(const wgpu::CommandEncoder& encoder, std::span<const PreparedDraw> draws,
                       std::span<const FrameShadow> sources, const ShadowAtlas& atlas)
        -> Core::Result<void> {
      /** Invalidate before encoding: an abandoned/failed submission cannot certify
       * atlas contents. Publish the candidate only after queue/error-scope success.
       * 编码前失效：放弃或失败提交不能证明图集内容；仅队列/错误作用域成功后发布候选。 */
      const auto previous = std::exchange(m_shadowCache, {});
      m_pendingShadowCache.reset();
      ShadowCacheState candidate;
      candidate.casters.reserve(draws.size());

      for (const auto& draw : draws) {
        candidate.casters.push_back({draw.mesh, draw.mesh->m_geometryRevision, draw.uniform.model,
                                     draw.uniform.alpha.x == 1, draw.uniform.tint.w,
                                     draw.uniform.alpha.y, draw.textures[0]});
      }

      struct Face {
        DrawBatches plan;
        wgpu::Buffer storage;
      };

      std::vector<Face> faces;

      for (std::size_t source = 0; source < sources.size(); ++source) {
        const auto& fit = sources[source].fit;

        for (std::uint32_t layer = 0; layer < fit.count; ++layer) {
          auto frustum = Frustum::create(fit.matrices[layer]);

          if (!frustum) {
            return std::unexpected(frustum.error());
          }

          auto plan = batches(draws, BatchPass::Shadow,
                              fit.kind == ShadowProjectionKind::Cascaded ? nullptr : &*frustum);

          if (!plan) {
            return std::unexpected(plan.error());
          }

          candidate.faces.push_back(
              {atlas.sources[source][layer], fit.matrices[layer], plan->indices});
          faces.push_back({std::move(*plan), {}});
        }
      }

      std::vector<std::vector<std::size_t>> oldPages(atlas.layers);
      std::vector<std::vector<std::size_t>> pages(atlas.layers);

      for (std::size_t i = 0; i < previous.faces.size(); ++i) {
        oldPages[previous.faces[i].tile.layer].push_back(i);
      }

      for (std::size_t i = 0; i < candidate.faces.size(); ++i) {
        pages[candidate.faces[i].tile.layer].push_back(i);
      }

      std::vector<bool> dirty(atlas.layers);

      for (std::uint32_t layer = 0; layer < atlas.layers; ++layer) {
        dirty[layer] = oldPages[layer].size() != pages[layer].size();

        for (std::size_t i = 0; !dirty[layer] && i < pages[layer].size(); ++i) {
          const auto& before = previous.faces[oldPages[layer][i]];
          const auto& after = candidate.faces[pages[layer][i]];
          dirty[layer] = before.tile != after.tile || before.matrix != after.matrix
                         || before.casters.size() != after.casters.size();

          for (std::size_t j = 0; !dirty[layer] && j < after.casters.size(); ++j) {
            dirty[layer]
                = !previous.casters[before.casters[j]].matches(candidate.casters[after.casters[j]]);
          }
        }

        if (!dirty[layer]) {
          ++pendingVisibility.shadowPagesReused;
          continue;
        }

        ++pendingVisibility.shadowPagesUpdated;

        for (const auto index : pages[layer]) {
          auto& face = faces[index];
          const auto& state = candidate.faces[index];
          std::vector<CoverageData> matrices;
          matrices.reserve(face.plan.indices.size());

          for (const auto drawIndex : face.plan.indices) {
            auto matrix = Math::multiply(state.matrix, draws[drawIndex].uniform.model);

            if (!matrix) {
              return Core::fail(matrix.error().code,
                                "Shadow instance matrix: " + matrix.error().message);
            }

            const auto& data = draws[drawIndex].uniform;
            matrices.push_back({*matrix, data.tint, data.flags, data.alpha, {}});
          }

          if (!matrices.empty()) {
            auto buffer = instanceBuffer(matrices.data(), matrices.size() * sizeof(CoverageData));

            if (!buffer) {
              return std::unexpected(buffer.error());
            }

            face.storage = *buffer;
          }
        }
      }

      /** Refresh whole physical pages: clearing one dirty tile would erase its
       * unchanged neighbours. Retained pages are neither cleared nor rendered.
       * 重绘整个物理页：清除单个脏图块会擦掉未变化邻块；复用页既不清除也不绘制。 */
      for (std::uint32_t layer = 0; layer < atlas.layers; ++layer) {
        if (!dirty[layer]) {
          continue;
        }

        wgpu::TextureViewDescriptor view;
        view.dimension = wgpu::TextureViewDimension::e2D;
        view.baseArrayLayer = layer;
        view.arrayLayerCount = 1;
        view.aspect = wgpu::TextureAspect::DepthOnly;
        wgpu::RenderPassDepthStencilAttachment depth;
        depth.view = shadowTexture.CreateView(&view);
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Store;
        depth.depthClearValue = 1;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.depthStencilAttachment = &depth;
        auto timestamps = profileWrites(Detail::ProfilePass::Shadows);
        descriptor.timestampWrites = timestamps.querySet ? &timestamps : nullptr;
        auto pass = encoder.BeginRenderPass(&descriptor);
        pass.SetPipeline(shadowDepthPipeline);

        for (const auto index : pages[layer]) {
          const auto& face = faces[index];
          const auto& tile = candidate.faces[index].tile;
          pass.SetViewport(static_cast<float>(tile.x), static_cast<float>(tile.y),
                           static_cast<float>(tile.size), static_cast<float>(tile.size), 0, 1);
          pass.SetScissorRect(tile.x, tile.y, tile.size, tile.size);

          for (const auto& batch : face.plan.batches) {
            const auto& draw = draws[face.plan.indices[batch.first]];
            pass.SetBindGroup(0,
                              bindCoverage(shadowDepthPipeline, face.storage,
                                           draw.uniform.alpha.x == 0 ? nullptr : draw.textures[0]));
            emitInstances(pass, *draw.mesh, batch);
            pendingVisibility.shadowInstances += batch.count;
            ++pendingVisibility.shadowDraws;
          }
        }

        pass.End();
      }

      m_pendingShadowCache = std::move(candidate);
      return {};
    }

    auto bindShadows(const wgpu::RenderPipeline& pipeline, std::span<const FrameShadow> sources,
                     const ShadowAtlas& atlas) -> wgpu::BindGroup {
      std::array<ShadowData, 64> data{};

      for (std::size_t index = 0; index < sources.size(); ++index) {
        const auto& frame = sources[index];
        const auto& fit = frame.fit;
        const auto& settings = *config.lighting.lights[frame.source].shadows;
        auto& light = data[frame.source];
        light
            = {fit.matrices,
               fit.splits,
               {fit.origin.x, fit.origin.y, fit.origin.z, fit.nearDepth},
               {fit.forward.x, fit.forward.y, fit.forward.z, 0},
               {settings.depthBias, settings.slopeBias, settings.filterRadius, settings.normalBias},
               {static_cast<float>(atlas.dimension), static_cast<float>(settings.resolution),
                static_cast<float>(fit.count), static_cast<float>(fit.kind)},
               {}};

        for (std::uint32_t face = 0; face < fit.count; ++face) {
          const auto& tile = atlas.sources[index][face];
          light.tiles[face] = {static_cast<float>(tile.x), static_cast<float>(tile.y),
                               static_cast<float>(tile.layer), 0};
        }
      }

      wgpu::BufferDescriptor descriptor;
      descriptor.size = sizeof(data);
      descriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
      auto buffer = device.CreateBuffer(&descriptor);
      queue.WriteBuffer(buffer, 0, data.data(), sizeof(data));
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
                         const OcclusionCamera& camera, const wgpu::Buffer& storage)
        -> Core::Result<void> {
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
      auto timestamps = profileWrites(Detail::ProfilePass::Occlusion);
      descriptor.timestampWrites = timestamps.querySet ? &timestamps : nullptr;
      auto pass = encoder.BeginRenderPass(&descriptor);
      pass.SetPipeline(geometryPipeline);
      pass.SetBindGroup(1, bindLighting(geometryPipeline));

      auto plan = batches(draws, BatchPass::Geometry);

      if (!plan) {
        pass.End();
        return std::unexpected(plan.error());
      }

      for (const auto& batch : plan->batches) {
        const auto& draw = draws[plan->indices[batch.first]];
        pass.SetBindGroup(0, bindCoverage(geometryPipeline, storage,
                                          draw.uniform.alpha.x == 0 ? nullptr : draw.textures[0]));
        emitInstances(pass, *draw.mesh, batch);
        pendingVisibility.occlusionInstances += batch.count;
        ++pendingVisibility.occlusionDraws;
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
        auto computeTime = profileWrites(Detail::ProfilePass::Occlusion);
        wgpu::ComputePassDescriptor computeDescriptor;
        computeDescriptor.timestampWrites = computeTime.querySet ? &computeTime : nullptr;
        auto compute = encoder.BeginComputePass(&computeDescriptor);
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
      if (m_profiler) {
        m_profiler->encoding(width, height);
      }

      occlusionPending = false;
      auto sorted = surfaceQueue(draws);

      if (!sorted) {
        return std::unexpected(sorted.error());
      }

      DrawBatches combined = sorted->solid;
      const auto solidCount = combined.indices.size();
      combined.indices.insert(combined.indices.end(), sorted->blended.indices.begin(),
                              sorted->blended.indices.end());
      auto plan = &combined;
      pendingVisibility = {};
      pendingVisibility.spatial = preparedSpatial;
      pendingVisibility.inputInstances = draws.size();
      pendingVisibility.culledInstances = draws.size() - plan->indices.size();
      wgpu::Buffer storage;

      if (!plan->indices.empty()) {
        auto buffer = surfaceBuffer(draws, *plan);

        if (!buffer) {
          return std::unexpected(buffer.error());
        }

        storage = *buffer;
      }

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

      auto atlas = shadowPlan(config.lighting);

      if (!atlas) {
        return std::unexpected(atlas.error());
      }

      const bool source = !atlas->sources.empty();
      std::vector<FrameShadow> shadows;
      wgpu::CommandEncoder shadowEncoder;

      if (source) {
        auto casters = shadowCasterBounds(draws);

        if (!casters) {
          return std::unexpected(casters.error());
        }

        for (std::size_t light = 0; light < config.lighting.lights.size(); ++light) {
          if (!config.lighting.lights[light].shadows) {
            continue;
          }

          auto fit = fitShadows(viewProjection, light, *casters);

          if (!fit) {
            return std::unexpected(fit.error());
          }

          shadows.push_back({light, *fit});
        }

        auto checked = ensureShadowPipelines();

        if (!checked) {
          return std::unexpected(checked.error());
        }

        checked = ensureShadowMaps(*atlas);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        shadowEncoder = device.CreateCommandEncoder();
        checked = encodeShadows(shadowEncoder, draws, shadows, *atlas);

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

        checked = encodeOcclusion(shadowEncoder, draws, *camera, storage);

        if (!checked) {
          return std::unexpected(checked.error());
        }
      }

      const auto& renderPipeline
          = occlusion && environment
                ? (source ? occlusionShadowEnvironmentPipeline : occlusionEnvironmentPipeline)
                : (source ? (environment ? shadowEnvironmentPipeline : shadowScenePipeline)
                          : (environment ? environmentPipeline : scenePipeline));
      wgpu::RenderPipeline blendPipeline;

      if (!sorted->blended.indices.empty()) {
        const auto index = (environment ? 4U : 0U) + (source ? 2U : 0U) + (occlusion ? 1U : 0U);

        if (!blendPipelines[index]) {
          blendPipelines[index]
              = pipeline(wgpu::TextureFormat::RGBA16Float, environment.has_value(), source,
                         occlusion.has_value() && environment.has_value(), true);
        }

        blendPipeline = blendPipelines[index];
      }

      auto surfaceTime = profileWrites(Detail::ProfilePass::Surface);
      auto encoder = encodePass(
          hdr, width, height, clear,
          [&](const wgpu::RenderPassEncoder& pass) {
            if (environment && environment->background) {
              drawSky(pass, inverse);
            }

            const auto emit = [&](const DrawBatches& batches, const wgpu::RenderPipeline& selected,
                                  std::uint32_t offset) {
              pass.SetPipeline(selected);

              if (environment) {
                pass.SetBindGroup(2, bindEnvironment(selected, occlusion.has_value()));
              }

              if (source) {
                pass.SetBindGroup(3, bindShadows(selected, shadows, *atlas));
              }

              pass.SetBindGroup(1, bindLighting(selected));

              for (auto batch : batches.batches) {
                const auto& draw = draws[batches.indices[batch.first]];
                pass.SetBindGroup(0, bindMaterial(selected, storage, draw.textures));
                batch.first += offset;
                emitInstances(pass, *draw.mesh, batch);
                pendingVisibility.visibleInstances += batch.count;
                ++pendingVisibility.colorDraws;

                if (selected.Get() == blendPipeline.Get()) {
                  pendingVisibility.blendedInstances += batch.count;
                  ++pendingVisibility.blendedDraws;
                } else {
                  pendingVisibility.solidInstances += batch.count;
                  ++pendingVisibility.solidDraws;
                }
              }
            };
            emit(sorted->solid, renderPipeline, 0);

            if (blendPipeline) {
              emit(sorted->blended, blendPipeline, static_cast<std::uint32_t>(solidCount));
            }
          },
          depthView, shadowEncoder, surfaceTime.querySet ? &surfaceTime : nullptr);
      auto postTime = profileWrites(Detail::ProfilePass::Post);
      encodePost(encoder, hdr, target, postTime.querySet ? &postTime : nullptr);

      if (m_profiler) {
        m_profiler->resolve(encoder);
        m_profiler->encoded();
      }

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
                  std::uint32_t layer = 0, std::uint32_t mip = 0, bool commitMeshState = false)
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

      if (commitMeshState) {
        commitShadowCache();

        if (m_profiler) {
          m_profiler->submitted(pendingVisibility);
        }
      }

      if (commitMeshState && occlusionPending) {
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
    auto presentFrame(F&& encodeTarget, bool commitMeshState = false)
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

      if (commitMeshState) {
        commitShadowCache();

        if (m_profiler) {
          m_profiler->submitted(pendingVisibility);
        }
      }

      if (commitMeshState && occlusionPending) {
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
        || config.maxTextureBytes > 512 * 1024 * 1024 || config.maxSkins == 0
        || config.maxSkins > 1024 || config.maxSkinBytes == 0
        || config.maxSkinBytes > 512 * 1024 * 1024 || config.maxShadowBytes == 0
        || config.maxShadowBytes > 512 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid renderer resource budget");
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
    const auto timestampFeature = wgpu::FeatureName::TimestampQuery;
    impl->m_timestampQueries = impl->adapter.HasFeature(timestampFeature);

    if (impl->m_timestampQueries) {
      deviceDescriptor.requiredFeatureCount = 1;
      deviceDescriptor.requiredFeatures = &timestampFeature;
    }

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
    impl->shadowLayerLimit = limits.maxTextureArrayLayers;
    auto atlas = impl->shadowPlan(impl->config.lighting);

    if (!atlas) {
      return std::unexpected(atlas.error());
    }

    impl->maxInstanceBufferBytes
        = std::min(limits.maxStorageBufferBindingSize, limits.maxBufferSize);
    ErrorScope scope(impl->instance, impl->device);
    impl->scenePipeline = impl->pipeline(wgpu::TextureFormat::RGBA16Float);
    impl->postPipeline = impl->postPipelineFor(wgpu::TextureFormat::RGBA8Unorm);
    wgpu::BufferDescriptor uniformDescriptor;
    uniformDescriptor.size = sizeof(InstanceData);
    uniformDescriptor.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
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
    mesh->hasTangents = surface->hasTangents;
    mesh->m_uvFrame = surface->hasNormals;
    const auto corners = data.indices.empty() ? data.vertices.size() : data.indices.size();

    for (std::size_t face = 0; !surface->hasNormalMapFrame && mesh->m_uvFrame && face < corners;
         face += 3) {
      const auto vertex = [&](std::size_t i) -> const Vertex& {
        return data.vertices[data.indices.empty() ? face + i : data.indices[face + i]];
      };
      const auto& a = vertex(0);
      const auto& b = vertex(1);
      const auto& c = vertex(2);
      const auto ux = static_cast<double>(b.texCoord.x) - a.texCoord.x;
      const auto uy = static_cast<double>(b.texCoord.y) - a.texCoord.y;
      const auto vx = static_cast<double>(c.texCoord.x) - a.texCoord.x;
      const auto vy = static_cast<double>(c.texCoord.y) - a.texCoord.y;
      const auto determinant = ux * vy - uy * vx;
      const auto scale = std::hypot(ux, uy) * std::hypot(vx, vy);
      mesh->m_uvFrame = mesh->hasTangents
                        || (scale > 0 && std::abs(determinant) >= 1e-12
                            && std::abs(determinant) > scale * 1e-6);

      for (const auto* v : {&a, &b, &c}) {
        mesh->m_uvFrame
            = mesh->m_uvFrame && std::abs(v->texCoord.x) <= 1e6 && std::abs(v->texCoord.y) <= 1e6;
      }
    }

    mesh->vertexBytes = data.vertices.size() * sizeof(Vertex);
    mesh->indexBytes = data.indices.size() * sizeof(std::uint32_t);
    ErrorScope scope(gpu.instance, gpu.device);
    wgpu::BufferDescriptor descriptor;
    descriptor.size = mesh->vertexBytes;
    descriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst
                       | wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::Storage;
    mesh->vertices = gpu.device.CreateBuffer(&descriptor);
    gpu.queue.WriteBuffer(mesh->vertices, 0, data.vertices.data(), mesh->vertexBytes);

    if (!data.indices.empty()) {
      descriptor.size = mesh->indexBytes;
      descriptor.usage
          = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Storage;
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

  namespace {
    auto mappedSkinWords(const wgpu::Instance& instance, const wgpu::Buffer& buffer,
                         std::size_t bytes) -> Core::Result<std::vector<std::uint32_t>> {
      auto pending = std::make_shared<Pending<bool>>();
      pending->error = "Skin buffer mapping failed";
      const auto future = buffer.MapAsync(
          wgpu::MapMode::Read, 0, bytes, wgpu::CallbackMode::WaitAnyOnly,
          [pending](wgpu::MapAsyncStatus status, wgpu::StringView message) noexcept {
            pending->value = status == wgpu::MapAsyncStatus::Success;

            if (!pending->value) {
              describeFailure(pending->error, "Skin readback", static_cast<unsigned>(status),
                              message);
            }
          });
      auto ready = wait(instance, future, "Skin readback");

      if (!ready) {
        buffer.Destroy();
        return std::unexpected(ready.error());
      }

      if (!pending->value) {
        return Core::fail(Core::ErrorCode::External, pending->error);
      }

      const auto* data = buffer.GetConstMappedRange(0, bytes);

      if (!data) {
        buffer.Unmap();
        return Core::fail(Core::ErrorCode::External, "Skin readback returned no bytes");
      }

      try {
        std::vector<std::uint32_t> result(bytes / 4);
        std::memcpy(result.data(), data, bytes);
        buffer.Unmap();
        return result;
      } catch (const std::exception& error) {
        buffer.Unmap();
        return Core::fail(Core::ErrorCode::External,
                          "Skin readback allocation: " + std::string(error.what()));
      }
    }

    auto skinBound(std::uint32_t encoded) -> float {
      const auto bits = (encoded & 0x80000000U) != 0 ? encoded ^ 0x80000000U : ~encoded;
      return std::bit_cast<float>(bits);
    }
  }  // namespace

  auto Renderer::createSkin(const Mesh& source, Animation::Skin skin) -> Core::Result<SkinnedMesh> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto description = skin.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    auto& gpu = *m_impl;

    if (!source.m_impl || source.m_impl->owner != gpu.diagnostics || source.m_impl->m_skinned
        || source.m_impl->info.vertices != (*description)->vertices.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Skin requires a same-device static mesh with matching vertex count");
    }

    const auto sourceBytes = source.m_impl->vertexBytes;
    const auto weightBytes = (*description)->vertices.size() * sizeof(Animation::SkinInfluence);
    const auto paletteBytes = (*description)->inverseBindMatrices.size() * sizeof(Math::Mat4);
    const auto bytes = sourceBytes + weightBytes + paletteBytes + 48
                       + (source.m_impl->info.indices == 0 ? 4U : 0U);

    if (gpu.diagnostics->m_skins.load() >= gpu.config.maxSkins || bytes > gpu.config.maxSkinBytes
        || gpu.diagnostics->m_skinBytes.load() > gpu.config.maxSkinBytes - bytes) {
      return Core::fail(Core::ErrorCode::Unavailable, "Skin residency budget exceeded");
    }

    try {
      auto state = std::make_shared<SkinnedMesh::Impl>();
      state->m_owner = gpu.diagnostics;
      state->m_skin = std::move(skin);
      state->m_info = {source.m_impl->info.vertices,
                       static_cast<std::uint32_t>((*description)->inverseBindMatrices.size()),
                       bytes, 0, source.m_impl->bounds};
      auto output = std::make_shared<Mesh::Impl>();
      output->owner = gpu.diagnostics;
      output->m_source = source.m_impl;
      output->m_skinned = true;
      output->info = source.m_impl->info;
      output->bounds = source.m_impl->bounds;
      output->hasNormals = source.m_impl->hasNormals;
      output->hasTangents = source.m_impl->hasTangents;
      output->hasNormalMapFrame = source.m_impl->hasNormalMapFrame;
      output->m_uvFrame = source.m_impl->m_uvFrame;
      output->indices = source.m_impl->indices;
      output->vertexBytes = sourceBytes;
      ErrorScope scope(gpu.instance, gpu.device);
      const auto allocate = [&](std::uint64_t size, wgpu::BufferUsage usage) {
        wgpu::BufferDescriptor descriptor;
        descriptor.size = size;
        descriptor.usage = usage;
        return gpu.device.CreateBuffer(&descriptor);
      };
      auto encoder = gpu.device.CreateCommandEncoder();

      for (auto& buffer : state->m_vertices) {
        buffer
            = allocate(sourceBytes, wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Storage
                                        | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc);
        encoder.CopyBufferToBuffer(source.m_impl->vertices, 0, buffer, 0, sourceBytes);
      }

      output->vertices = state->m_vertices[0];
      state->m_weights
          = allocate(weightBytes, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
      state->m_palette
          = allocate(paletteBytes, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
      state->m_report = allocate(
          32, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc);
      state->m_parameters = allocate(16, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
      state->m_indices = source.m_impl->indices;

      if (!state->m_indices) {
        state->m_indices = allocate(4, wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst);
      }

      const std::array<std::uint32_t, 4> parameters{
          output->info.indices,
          (output->info.indices == 0 ? output->info.vertices : output->info.indices) / 3, 0, 0};
      gpu.queue.WriteBuffer(state->m_parameters, 0, parameters.data(), sizeof(parameters));
      gpu.queue.WriteBuffer(state->m_weights, 0, (*description)->vertices.data(), weightBytes);
      auto commands = encoder.Finish();
      gpu.queue.Submit(1, &commands);
      valid = scope.finish();

      if (!valid) {
        return std::unexpected(valid.error());
      }

      gpu.diagnostics->meshes.fetch_add(1);
      gpu.diagnostics->vertexBytes.fetch_add(sourceBytes);
      output->tracked = true;
      state->m_mesh = Mesh(std::move(output));
      gpu.diagnostics->m_skins.fetch_add(1);
      gpu.diagnostics->m_skinBytes.fetch_add(bytes);
      state->m_tracked = true;
      return SkinnedMesh(std::move(state));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Create GPU skin: " + std::string(error.what()));
    }
  }

  auto Renderer::updateSkins(std::span<const SkinUpdate> updates) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return valid;
    }

    if (updates.size() > 128) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Skin update batch exceeds 128 controllers");
    }

    if (updates.empty()) {
      return {};
    }

    auto& gpu = *m_impl;
    std::set<const SkinnedMesh::Impl*> unique;
    std::size_t vertices{};

    for (const auto& update : updates) {
      if (!update.skin || !update.skin->m_impl || update.skin->m_impl->m_owner != gpu.diagnostics
          || !unique.insert(update.skin->m_impl.get()).second) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid/foreign/duplicate skin update");
      }

      const auto& state = *update.skin->m_impl;
      valid = Animation::validateSkinPalette(state.m_skin, update.palette);

      if (!valid) {
        return valid;
      }

      if (state.m_info.updates == std::numeric_limits<std::uint64_t>::max()
          || state.m_info.vertices > 4'000'000 - vertices) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Skin update vertex/counter capacity exceeded");
      }

      vertices += state.m_info.vertices;
    }

    try {
      ErrorScope scope(gpu.instance, gpu.device);

      if (!gpu.m_skinVertices) {
        std::array<wgpu::BindGroupLayoutEntry, 7> entries{};

        for (std::uint32_t i = 0; i < entries.size(); ++i) {
          entries[i].binding = i;
          entries[i].visibility = wgpu::ShaderStage::Compute;
          entries[i].buffer.type = i == 6             ? wgpu::BufferBindingType::Uniform
                                   : i == 3 || i == 4 ? wgpu::BufferBindingType::Storage
                                                      : wgpu::BufferBindingType::ReadOnlyStorage;
        }

        wgpu::BindGroupLayoutDescriptor layoutDescriptor;
        layoutDescriptor.entryCount = entries.size();
        layoutDescriptor.entries = entries.data();
        gpu.m_skinLayout = gpu.device.CreateBindGroupLayout(&layoutDescriptor);
        wgpu::PipelineLayoutDescriptor pipelineLayout;
        pipelineLayout.bindGroupLayoutCount = 1;
        pipelineLayout.bindGroupLayouts = &gpu.m_skinLayout;
        auto layout = gpu.device.CreatePipelineLayout(&pipelineLayout);
        wgpu::ShaderSourceWGSL source;
        source.code = Detail::skinningShader;
        wgpu::ShaderModuleDescriptor shaderDescriptor;
        shaderDescriptor.nextInChain = &source;
        auto shader = gpu.device.CreateShaderModule(&shaderDescriptor);
        wgpu::ComputePipelineDescriptor pipeline;
        pipeline.layout = layout;
        pipeline.compute.module = shader;
        pipeline.compute.entryPoint = "skinVertices";
        gpu.m_skinVertices = gpu.device.CreateComputePipeline(&pipeline);
        pipeline.compute.entryPoint = "validateFaces";
        gpu.m_skinFaces = gpu.device.CreateComputePipeline(&pipeline);
      }

      wgpu::BufferDescriptor readbackDescriptor;
      readbackDescriptor.size = updates.size() * 32;
      readbackDescriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
      auto readback = gpu.device.CreateBuffer(&readbackDescriptor);
      auto encoder = gpu.device.CreateCommandEncoder();

      for (std::size_t i = 0; i < updates.size(); ++i) {
        const auto& update = updates[i];
        auto& state = *update.skin->m_impl;
        const auto& mesh = *state.m_mesh.m_impl;
        const std::array<std::uint32_t, 8> initial{0, 0xffffffffU, 0xffffffffU, 0xffffffffU,
                                                   0, 0,           0,           0};
        gpu.queue.WriteBuffer(state.m_report, 0, initial.data(), sizeof(initial));
        gpu.queue.WriteBuffer(state.m_palette, 0, update.palette.data(),
                              update.palette.size_bytes());
        const std::array buffers{mesh.m_source->vertices, state.m_weights,
                                 state.m_palette,         state.m_vertices[state.m_next],
                                 state.m_report,          state.m_indices,
                                 state.m_parameters};
        std::array<wgpu::BindGroupEntry, 7> entries{};

        for (std::uint32_t binding = 0; binding < entries.size(); ++binding) {
          entries[binding].binding = binding;
          entries[binding].buffer = buffers[binding];
          entries[binding].size = buffers[binding].GetSize();
        }

        wgpu::BindGroupDescriptor groupDescriptor;
        groupDescriptor.layout = gpu.m_skinLayout;
        groupDescriptor.entryCount = entries.size();
        groupDescriptor.entries = entries.data();
        auto group = gpu.device.CreateBindGroup(&groupDescriptor);
        auto pass = encoder.BeginComputePass();
        pass.SetPipeline(gpu.m_skinVertices);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups((mesh.info.vertices + 63) / 64);
        pass.End();
        pass = encoder.BeginComputePass();
        pass.SetPipeline(gpu.m_skinFaces);
        pass.SetBindGroup(0, group);
        const auto faces = (mesh.info.indices == 0 ? mesh.info.vertices : mesh.info.indices) / 3;
        pass.DispatchWorkgroups((faces + 63) / 64);
        pass.End();
        encoder.CopyBufferToBuffer(state.m_report, 0, readback, i * 32, 32);
      }

      auto commands = encoder.Finish();
      gpu.queue.Submit(1, &commands);
      valid = scope.finish();

      if (!valid) {
        gpu.m_skinVertices = {};
        gpu.m_skinFaces = {};
        gpu.m_skinLayout = {};
        return valid;
      }

      auto report = mappedSkinWords(gpu.instance, readback, updates.size() * 32);

      if (!report) {
        return std::unexpected(report.error());
      }

      std::vector<Bounds> bounds;
      bounds.reserve(updates.size());

      for (std::size_t i = 0; i < updates.size(); ++i) {
        const auto* words = report->data() + i * 8;
        const Bounds value{{skinBound(words[1]), skinBound(words[2]), skinBound(words[3])},
                           {skinBound(words[4]), skinBound(words[5]), skinBound(words[6])}};

        if (words[0] != 0 || (words[7] & 6U) == 6U || !Math::isFinite(value.minimum)
            || !Math::isFinite(value.maximum) || value.minimum.x > value.maximum.x
            || value.minimum.y > value.maximum.y || value.minimum.z > value.maximum.z) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "GPU skin deformation invalid at batch index " + std::to_string(i)
                                + ": singular/overflow/tangent seam/mixed orientation ("
                                + std::to_string(words[0]) + ", " + std::to_string(words[7]) + ")");
        }

        bounds.push_back(value);
      }

      for (std::size_t i = 0; i < updates.size(); ++i) {
        auto& state = *updates[i].skin->m_impl;
        auto& mesh = *state.m_mesh.m_impl;
        mesh.vertices = state.m_vertices[state.m_next];
        mesh.bounds = bounds[i];
        const auto flags = (*report)[i * 8 + 7];
        mesh.hasNormalMapFrame = mesh.m_uvFrame && (flags & 1U) == 0;
        mesh.m_poseOrientation = (flags & 4U) != 0 ? -1.F : 1.F;
        state.m_info.bounds = bounds[i];
        ++state.m_info.updates;
        mesh.m_geometryRevision = state.m_info.updates;
        state.m_next ^= 1U;
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Update GPU skins: " + std::string(error.what()));
    }
  }

  auto Renderer::readSkin(const SkinnedMesh& skin) -> Core::Result<std::vector<Vertex>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer was moved");
    }

    auto valid = m_impl->check();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    if (!skin.m_impl || skin.m_impl->m_owner != m_impl->diagnostics) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Readback requires a same-device skin");
    }

    auto& gpu = *m_impl;
    const auto& mesh = *skin.m_impl->m_mesh.m_impl;
    ErrorScope scope(gpu.instance, gpu.device);
    wgpu::BufferDescriptor descriptor;
    descriptor.size = mesh.vertexBytes;
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto buffer = gpu.device.CreateBuffer(&descriptor);
    auto encoder = gpu.device.CreateCommandEncoder();
    encoder.CopyBufferToBuffer(mesh.vertices, 0, buffer, 0, mesh.vertexBytes);
    auto commands = encoder.Finish();
    gpu.queue.Submit(1, &commands);
    valid = scope.finish();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto words = mappedSkinWords(gpu.instance, buffer, static_cast<std::size_t>(mesh.vertexBytes));

    if (!words) {
      return std::unexpected(words.error());
    }

    std::vector<Vertex> vertices(mesh.info.vertices);
    std::memcpy(vertices.data(), words->data(), static_cast<std::size_t>(mesh.vertexBytes));
    return vertices;
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
      const Image* mip = &image;
      std::optional<Image> ownedMip;

      for (std::uint32_t level = 0; level < levels; ++level) {
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = texture->texture;
        destination.mipLevel = level;
        wgpu::TexelCopyBufferLayout layout;
        layout.bytesPerRow = mip->width * 4;
        layout.rowsPerImage = mip->height;
        const wgpu::Extent3D extent{mip->width, mip->height, 1};
        m_impl->queue.WriteTexture(&destination, mip->pixels.data(), mip->pixels.size(), &layout,
                                   &extent);

        if (level + 1 == levels) {
          break;
        }

        auto next = downsampleImage(*mip, options.colorSpace);

        if (!next) {
          return std::unexpected(next.error());
        }

        ownedMip = std::move(*next);
        mip = &*ownedMip;
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

  auto Renderer::visibilityStats() const -> Core::Result<VisibilityStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);
    return checked ? Core::Result<VisibilityStats>(m_impl->visibility)
                   : std::unexpected(checked.error());
  }

  auto Renderer::setProfiling(bool enabled) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    if (static_cast<bool>(m_impl->m_profiler) == enabled) {
      return {};
    }

    if (!enabled) {
      m_impl->m_profiler.reset();
      return {};
    }

    if (m_impl->m_profilingSession == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Mesh profiling session exhausted");
    }

    ErrorScope scope(m_impl->instance, m_impl->device);
    auto profiler = Detail::FrameProfiler::create(m_impl->instance, m_impl->device,
                                                  m_impl->m_timestampQueries);
    checked = scope.finish();

    if (!checked) {
      return checked;
    }

    if (!profiler) {
      return std::unexpected(profiler.error());
    }

    m_impl->m_profiler = std::move(*profiler);
    ++m_impl->m_profilingSession;
    return {};
  }

  auto Renderer::profiling() const -> Core::Result<MeshProfiling> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      auto result = m_impl->m_profiler ? m_impl->m_profiler->snapshot() : MeshProfiling{};
      result.gpuSupported = m_impl->m_timestampQueries;
      result.adapter = m_impl->info;
      result.session = m_impl->m_profilingSession;
      checked = m_impl->check(true);
      return checked ? Core::Result<MeshProfiling>{std::move(result)}
                     : std::unexpected(checked.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Read mesh profiling: " + std::string(error.what()));
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
                         m_impl->occlusionStorageBytes,
                         stats.m_skins.load(),
                         stats.m_skinBytes.load()};
  }

  auto Renderer::shadowPipelinesReady() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_shadowPreparation && m_impl->m_shadowPreparation->error) {
      return std::unexpected(*m_impl->m_shadowPreparation->error);
    }

    return m_impl->shadowScenePipeline && m_impl->shadowEnvironmentPipeline
           && m_impl->shadowDepthPipeline;
  }

  auto Renderer::prepareShadowPipelines() -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->prepareShadowPipelines() : std::unexpected(checked.error());
  }

  auto Renderer::occlusionPipelinesReady() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_occlusionPreparation && m_impl->m_occlusionPreparation->error) {
      return std::unexpected(*m_impl->m_occlusionPreparation->error);
    }

    return m_impl->geometryPipeline && m_impl->occlusionPipeline && m_impl->occlusionFilterPipeline
           && m_impl->occlusionEnvironmentPipeline && m_impl->occlusionShadowEnvironmentPipeline;
  }

  auto Renderer::prepareOcclusionPipelines() -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->prepareOcclusionPipelines() : std::unexpected(checked.error());
  }

  auto Renderer::environmentPipelineReady() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto& preparation = m_impl->m_environmentPreparation;

    if (preparation && preparation->job.error) {
      return std::unexpected(*preparation->job.error);
    }

    return preparation && preparation->job.pipeline;
  }

  auto Renderer::prepareEnvironmentPipeline() -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      auto& gpu = *m_impl;

      if (!gpu.m_environmentPreparation) {
        Impl::EnvironmentPreparation preparation;
        ErrorScope scope(gpu.instance, gpu.device);
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
        preparation.layout = gpu.device.CreateBindGroupLayout(&layoutDescriptor);
        wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor;
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &preparation.layout;
        auto layout = gpu.device.CreatePipelineLayout(&pipelineLayoutDescriptor);
        wgpu::ShaderSourceWGSL shaderSource;
        shaderSource.code = Detail::environmentShader;
        wgpu::ShaderModuleDescriptor shaderDescriptor;
        shaderDescriptor.nextInChain = &shaderSource;
        auto shader = gpu.device.CreateShaderModule(&shaderDescriptor);
        checked = scope.finish();

        if (!checked) {
          return std::unexpected(checked.error());
        }

        wgpu::ComputePipelineDescriptor descriptor;
        descriptor.layout = layout;
        descriptor.compute.module = shader;
        descriptor.compute.entryPoint = "bake";
        preparation.job = compilePipeline<wgpu::ComputePipeline>(
            gpu.device, descriptor, "Environment pipeline compilation");
        gpu.m_environmentPreparation = std::move(preparation);
      }

      return gpu.m_environmentPreparation->job.poll(gpu.instance);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare environment pipeline: " + std::string(error.what()));
    }
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
      auto ready = prepareEnvironmentPipeline();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        checked = gpu.m_environmentPreparation->job.finish(gpu.instance);

        if (!checked) {
          return std::unexpected(checked.error());
        }
      }

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

      const auto& layout = gpu.m_environmentPreparation->layout;
      const auto& pipeline = gpu.m_environmentPreparation->job.pipeline;
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

    auto atlas = m_impl->shadowPlan(*validated);

    if (!atlas) {
      return std::unexpected(atlas.error());
    }

    m_impl->config.lighting = std::move(*validated);

    if (atlas->sources.empty()) {
      m_impl->shadowTexture = nullptr;
      m_impl->m_shadowCache = {};
      m_impl->m_pendingShadowCache.reset();
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

    Impl::ProfileScope profile{*m_impl};
    auto prepared = m_impl->prepare(draws, viewProjection, clear);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    profile.prepared();

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

    m_impl->commitShadowCache();

    if (m_impl->m_profiler) {
      m_impl->m_profiler->submitted(m_impl->pendingVisibility);
    }

    ++target.m_impl->info.frames;
    m_impl->visibility.mesh = m_impl->pendingVisibility;
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

    if (m_impl->thread != std::this_thread::get_id() || m_impl->guiFrameActive
        || (m_impl->gui && m_impl->gui->building())) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "GUI shutdown requires owner thread without reentry");
    }

    m_impl->guiTargets.clear();
    m_impl->guiTextures.clear();
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

    auto checked = m_impl->check(true);

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

  auto Renderer::guiImage(const Texture& texture, float width, float height) -> Core::Result<void> {
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

    if (!texture.m_impl || texture.m_impl->owner != m_impl->diagnostics) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "GUI image texture is empty or foreign");
    }

    auto info = texture.info();

    if (!info) {
      return std::unexpected(info.error());
    }

    m_impl->guiTextures.push_back(texture);
    return m_impl->gui->image(texture.m_impl->view.Get(), width, height);
  }

  auto Renderer::guiViewport(const GuiViewportDraw& draw, float width, float height)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Renderer has been moved from");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return checked;
    }

    if (!draw || !m_impl->gui || !m_impl->gui->building() || m_impl->guiViewports.size() >= 16) {
      return Core::fail(
          Core::ErrorCode::InvalidState,
          "Deferred GUI viewports require an active layout and at most sixteen draws");
    }

    m_impl->guiViewports.push_back({{}, draw});
    auto slot = m_impl->gui->reserveImage(width, height, m_impl->info.maxTextureDimension);

    if (!slot) {
      m_impl->guiViewports.pop_back();
      return std::unexpected(slot.error());
    }

    m_impl->guiViewports.back().slot = *slot;
    return {};
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

    if (m_impl->guiFrameActive) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "GUI frames cannot reenter deferred rendering");
    }

    Impl::GuiFrameScope frame{*m_impl};

    try {
      ErrorScope scope(m_impl->instance, m_impl->device);
      auto built = m_impl->gui->build(draw, width, height, seconds, scale, scale);
      checked = scope.finish();

      if (!built || !checked) {
        return std::unexpected(built ? checked.error() : built.error());
      }

      checked = m_impl->resolveGuiViewports();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      const auto format = m_impl->surface ? m_impl->surfaceFormat : wgpu::TextureFormat::RGBA8Unorm;
      auto image = m_impl->readback(
          width, height,
          [&](const auto& texture, auto, auto) {
            return Core::Result<wgpu::CommandEncoder>(m_impl->encodeGui(texture));
          },
          {}, format);

      if (image) {
        m_impl->gui->submitted();
      }

      return image;
    } catch (const std::exception& error) {
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

    if (m_impl->guiFrameActive) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "GUI frames cannot reenter deferred rendering");
    }

    Impl::GuiFrameScope frame{*m_impl};

    try {
      ErrorScope scope(m_impl->instance, m_impl->device);
      auto built = m_impl->gui->build(
          draw, width, height, seconds,
          static_cast<float>(pixels->width) / static_cast<float>(logical->width),
          static_cast<float>(pixels->height) / static_cast<float>(logical->height));
      checked = scope.finish();

      if (!built || !checked) {
        return std::unexpected(built ? checked.error() : built.error());
      }

      checked = m_impl->resolveGuiViewports();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      auto presented = m_impl->presentFrame([&](const auto& texture, auto, auto) {
        return Core::Result<wgpu::CommandEncoder>(m_impl->encodeGui(texture));
      });

      if (presented && *presented == PresentResult::Presented) {
        m_impl->gui->submitted();
      }

      return presented;
    } catch (const std::exception& error) {
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

    Impl::ProfileScope profile{*m_impl};
    auto prepared = m_impl->prepare(draws, viewProjection, clear);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    profile.prepared();

    auto image = m_impl->readback(
        width, height,
        [&](const wgpu::Texture& texture, auto targetWidth, auto targetHeight) {
          return m_impl->encodeMeshes(texture, *prepared, viewProjection, targetWidth, targetHeight,
                                      clear);
        },
        {}, wgpu::TextureFormat::RGBA8Unorm, 0, 0, true);

    if (image) {
      m_impl->visibility.mesh = m_impl->pendingVisibility;
    }

    return image;
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

    auto plan = m_impl->batches(*prepared, Impl::BatchPass::Picking);

    if (!plan) {
      return std::unexpected(plan.error());
    }

    std::vector<CoverageData> data;
    data.reserve(plan->indices.size());

    for (const auto index : plan->indices) {
      const auto& draw = (*prepared)[index];
      data.push_back({draw.uniform.transform,
                      draw.uniform.tint,
                      draw.uniform.flags,
                      draw.uniform.alpha,
                      {index + 1, 0, 0, 0}});
    }

    wgpu::Buffer storage;

    if (!data.empty()) {
      auto buffer = m_impl->instanceBuffer(data.data(), data.size() * sizeof(CoverageData));

      if (!buffer) {
        return std::unexpected(buffer.error());
      }

      storage = *buffer;
    }

    PickFrameStats pickStats{};
    pickStats.spatial = m_impl->preparedSpatial;
    pickStats.inputInstances = draws.size();
    pickStats.culledInstances = draws.size() - plan->indices.size();
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

                for (const auto& batch : plan->batches) {
                  const auto& draw = (*prepared)[plan->indices[batch.first]];
                  pass.SetBindGroup(0, m_impl->bindCoverage(
                                           m_impl->pickingPipeline, storage,
                                           draw.uniform.alpha.x == 0 ? nullptr : draw.textures[0]));
                  Impl::emitInstances(pass, *draw.mesh, batch);
                  pickStats.candidateInstances += batch.count;
                  ++pickStats.draws;
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

    m_impl->visibility.pick = pickStats;
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

    Impl::ProfileScope profile{*m_impl};
    auto prepared = m_impl->prepare(draws, viewProjection, clear);

    if (!prepared) {
      return std::unexpected(prepared.error());
    }

    profile.prepared();

    auto presented = m_impl->presentFrame(
        [&](const wgpu::Texture& texture, auto width, auto height) {
          return m_impl->encodeMeshes(texture, *prepared, viewProjection, width, height, clear);
        },
        true);

    if (presented && *presented == PresentResult::Presented) {
      m_impl->visibility.mesh = m_impl->pendingVisibility;
    }

    return presented;
  }
}  // namespace Dot::Graphics
