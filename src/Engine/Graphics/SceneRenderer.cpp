module;

#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <utility>

module Dot.Graphics.SceneRenderer;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Math;
import Dot.World.Scene;
import Dot.Graphics.Renderer;
import Dot.Graphics.Geometry;
import Dot.Assets.Mesh;

namespace Dot::Graphics {
  namespace {
    auto validateClear(Math::Vec4 clear) -> Core::Result<void> {
      if (!Math::isFinite(clear) || clear.x < 0 || clear.x > 1 || clear.y < 0 || clear.y > 1
          || clear.z < 0 || clear.z > 1 || clear.w < 0 || clear.w > 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scene clear color must be finite and in [0,1]");
      }

      return {};
    }

    auto key(const World::MeshRenderer& component) -> Core::Result<std::string> {
      switch (component.kind) {
        case World::MeshKind::Cube:
          return "builtin:cube";
        case World::MeshKind::Quad:
          return "builtin:quad";
        case World::MeshKind::Asset:
          return "asset:" + component.asset;
        default:
          return Core::fail(Core::ErrorCode::InvalidState, "Scene has an unknown mesh kind");
      }
    }
  }  // namespace

  struct SceneRenderer::Impl {
    struct Entry {
      Mesh mesh;
      std::uint64_t bytes;
      std::string warning;
    };

    struct Frame {
      Math::Mat4 camera;
      std::vector<Mesh> meshes;
      std::vector<MeshDraw> draws;

      Frame() = default;

      Frame(const Frame&) = delete;

      auto operator=(const Frame&) -> Frame& = delete;

      Frame(Frame&&) noexcept = default;

      auto operator=(Frame&&) noexcept -> Frame& = default;
    };

    Renderer* renderer;
    std::optional<Core::FileSystem> assets;
    std::uint64_t budget;
    std::uint64_t bytes{};
    std::unordered_map<std::string, Entry> cache;

    auto check() const -> Core::Result<void> {
      auto info = renderer->adapterInfo();

      if (!info) {
        return std::unexpected(info.error());
      }

      return {};
    }

    auto load(const World::MeshRenderer& component) -> Core::Result<Entry> {
      MeshData data;
      std::string warning;

      if (component.kind == World::MeshKind::Asset) {
        if (!assets) {
          return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has no asset root");
        }

        const std::filesystem::path path(
            std::u8string(component.asset.begin(), component.asset.end()));
        auto loaded = Assets::loadMesh(*assets, path);

        if (!loaded) {
          return Core::fail(
              loaded.error().code,
              std::format("Load scene mesh '{}': {}", component.asset, loaded.error().message));
        }

        data = std::move(loaded->data);
        warning = std::move(loaded->warnings);
      } else {
        if (component.kind != World::MeshKind::Cube && component.kind != World::MeshKind::Quad) {
          return Core::fail(Core::ErrorCode::InvalidState, "Scene has an unknown mesh kind");
        }

        auto geometry = component.kind == World::MeshKind::Cube ? cube() : quad();

        if (!geometry) {
          return std::unexpected(geometry.error());
        }

        data = std::move(*geometry);
      }

      const auto resourceBytes
          = data.vertices.size() * sizeof(Vertex) + data.indices.size() * sizeof(std::uint32_t);

      if (resourceBytes > budget - bytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene mesh cache byte budget exceeded");
      }

      auto uploaded = renderer->upload(data);

      if (!uploaded) {
        return std::unexpected(uploaded.error());
      }

      return Entry{std::move(*uploaded), resourceBytes, std::move(warning)};
    }

    auto frame(const World::Scene& scene, float aspect, std::optional<World::EntityId> camera)
        -> Core::Result<Frame> {
      auto selected = camera ? Core::Result<World::EntityId>{*camera} : scene.primaryCamera();

      if (!selected) {
        return std::unexpected(selected.error());
      }

      auto view = scene.viewProjection(*selected, aspect);
      auto entities = scene.entities();

      if (!view) {
        return std::unexpected(view.error());
      }

      if (!entities) {
        return std::unexpected(entities.error());
      }

      std::unordered_set<std::string> needed;
      std::size_t drawCount = 0;

      for (const auto& entity : *entities) {
        if (entity.mesh && entity.mesh->visible) {
          auto resourceKey = key(*entity.mesh);

          if (!resourceKey) {
            return std::unexpected(resourceKey.error());
          }

          needed.insert(*resourceKey);
          ++drawCount;
        }
      }

      if (drawCount > 4096) {
        return Core::fail(Core::ErrorCode::Unavailable, "Visible scene exceeds 4096 mesh draws");
      }

      for (auto iterator = cache.begin(); iterator != cache.end();) {
        if (!needed.contains(iterator->first)) {
          bytes -= iterator->second.bytes;
          iterator = cache.erase(iterator);
        } else {
          ++iterator;
        }
      }

      Frame frame;
      frame.camera = *view;
      /** Draw pointers borrow these mesh handles; reserve once and keep the frame
       * move-only so their addresses remain stable throughout submission.
       * 绘制指针借用这些网格句柄；一次性预留空间并仅允许移动帧，确保提交期间地址稳定。
       */
      frame.meshes.reserve(drawCount);
      frame.draws.reserve(drawCount);

      for (const auto& entity : *entities) {
        if (!entity.mesh || !entity.mesh->visible) {
          continue;
        }

        const auto resourceKey = key(*entity.mesh);

        if (!resourceKey) {
          return std::unexpected(resourceKey.error());
        }

        auto found = cache.find(*resourceKey);

        if (found == cache.end()) {
          auto entry = load(*entity.mesh);

          if (!entry) {
            return std::unexpected(entry.error());
          }

          auto inserted = cache.emplace(*resourceKey, std::move(*entry));
          found = inserted.first;
          bytes += found->second.bytes;
        }

        auto world = scene.worldTransform(entity.id);

        if (!world) {
          return std::unexpected(world.error());
        }

        frame.meshes.push_back(found->second.mesh);
        frame.draws.push_back({&frame.meshes.back(), *world, entity.mesh->tint});
      }

      return Core::Result<Frame>{std::move(frame)};
    }
  };

  SceneRenderer::SceneRenderer(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SceneRenderer::SceneRenderer(SceneRenderer&&) noexcept = default;

  auto SceneRenderer::operator=(SceneRenderer&&) noexcept -> SceneRenderer& = default;

  SceneRenderer::~SceneRenderer() = default;

  auto SceneRenderer::create(Renderer& renderer, std::optional<Core::FileSystem> assets,
                             std::uint64_t maxCacheBytes) -> Core::Result<SceneRenderer> {
    auto info = renderer.adapterInfo();

    if (!info) {
      return std::unexpected(info.error());
    }

    if (maxCacheBytes == 0 || maxCacheBytes > 1024ULL * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene cache budget must be in [1, 1 GiB]");
    }

    auto impl = std::make_unique<Impl>();
    impl->renderer = &renderer;
    impl->assets = std::move(assets);
    impl->budget = maxCacheBytes;
    return SceneRenderer(std::move(impl));
  }

  auto SceneRenderer::render(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                             Math::Vec4 clear, std::optional<World::EntityId> camera)
      -> Core::Result<Image> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateClear(clear);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene readback dimensions must be in [1, 4096]");
    }

    auto frame
        = m_impl->frame(scene, static_cast<float>(width) / static_cast<float>(height), camera);

    if (!frame) {
      return std::unexpected(frame.error());
    }

    return m_impl->renderer->renderMeshes(frame->draws, frame->camera, width, height, clear);
  }

  auto SceneRenderer::present(const World::Scene& scene, Math::Vec4 clear,
                              std::optional<World::EntityId> camera)
      -> Core::Result<PresentResult> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = validateClear(clear);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto size = m_impl->renderer->surfaceSize();

    if (!size) {
      return std::unexpected(size.error());
    }

    if (size->width <= 0 || size->height <= 0) {
      return PresentResult::Deferred;
    }

    auto frame = m_impl->frame(
        scene, static_cast<float>(size->width) / static_cast<float>(size->height), camera);

    if (!frame) {
      return std::unexpected(frame.error());
    }

    return m_impl->renderer->presentMeshes(frame->draws, frame->camera, clear);
  }

  auto SceneRenderer::renderInto(const World::Scene& scene, RenderTarget& target, Math::Vec4 clear,
                                 std::optional<World::EntityId> camera) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    checked = validateClear(clear);

    if (!checked) {
      return checked;
    }

    auto info = target.info();

    if (!info) {
      return std::unexpected(info.error());
    }

    auto frame = m_impl->frame(
        scene, static_cast<float>(info->width) / static_cast<float>(info->height), camera);

    if (!frame) {
      return std::unexpected(frame.error());
    }

    return m_impl->renderer->renderMeshesInto(target, frame->draws, frame->camera, clear);
  }

  auto SceneRenderer::stats() const -> Core::Result<SceneRenderStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return SceneRenderStats{m_impl->cache.size(), m_impl->bytes};
  }

  auto SceneRenderer::warnings() const -> Core::Result<std::vector<std::string>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::vector<std::string> warnings;

    for (const auto& [resourceKey, entry] : m_impl->cache) {
      if (!entry.warning.empty()) {
        warnings.push_back(resourceKey + ": " + entry.warning);
      }
    }

    std::ranges::sort(warnings);
    return warnings;
  }

  auto SceneRenderer::clearCache() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    m_impl->cache.clear();
    m_impl->bytes = 0;
    return {};
  }
}  // namespace Dot::Graphics
