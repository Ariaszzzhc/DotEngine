module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

module Dot.Graphics.SceneRenderer;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Math;
import Dot.World.Scene;
import Dot.Graphics.Renderer;
import Dot.Graphics.Geometry;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Assets.Image;
import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;

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

    auto assetPath(std::string_view text) -> std::filesystem::path {
      return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    auto textureKey(const Assets::TextureSource& source) -> std::string {
      const auto& options = source.sampling;
      return std::format("{}:{}:{}:{}:{}:{}:{}", source.path, static_cast<int>(options.colorSpace),
                         static_cast<int>(options.filter), static_cast<int>(options.addressU),
                         static_cast<int>(options.addressV), options.mipmaps,
                         options.maxAnisotropy);
    }

    auto textureBytes(const Image& image, bool mipmaps) -> std::uint64_t {
      auto width = image.width;
      auto height = image.height;
      std::uint64_t result{};

      do {
        result += static_cast<std::uint64_t>(width) * height * 4;

        if (!mipmaps || (width == 1 && height == 1)) {
          return result;
        }

        width = std::max(1U, width / 2);
        height = std::max(1U, height / 2);
      } while (true);
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
      Bounds bounds;
    };

    struct TextureEntry {
      Texture texture;
      std::uint64_t bytes;
    };

    struct SkinEntry {
      std::string mesh;
      World::SkinBinding properties;
      Animation::SceneSkin binding;
      SkinnedMesh controller;
      std::vector<Math::Mat4> palette;
      std::uint64_t bytes;
    };

    struct InitialSkin {
      std::uint64_t owner;
      std::vector<Math::Mat4> palette;
      auto operator==(const InitialSkin&) const -> bool = default;
    };

    struct PendingSkin {
      std::vector<InitialSkin> rows;
      SkinBatch batch;
      bool failed{};
    };

    struct MaterialEntry {
      Material material;
      std::vector<std::string> textures;
    };

    struct ImageResult {
      std::mutex mutex;
      std::optional<Core::Result<std::vector<Image>>> value;
    };

    struct ImageJob {
      std::shared_ptr<ImageResult> result;
      std::jthread thread;
      bool canceled{};
    };

    struct HdrResult {
      std::mutex mutex;
      std::optional<Core::Result<EnvironmentSource>> value;
    };

    struct HdrJob {
      std::shared_ptr<HdrResult> result;
      std::jthread thread;
      bool canceled{};
    };

    struct PendingEnvironment {
      World::Environment source;
      std::optional<EnvironmentBake> bake;
      std::optional<Core::Error> error;
    };

    struct PendingMaterial {
      std::string path;
      Assets::MaterialDescription description;
      std::vector<std::pair<std::string, Assets::TextureSource>> sources;
      std::unordered_map<std::string, TextureEntry> staged;
      std::size_t cursor{};
      std::optional<Core::Error> error;
    };

    struct Frame {
      Math::Mat4 camera;
      std::vector<Mesh> meshes;
      std::vector<MeshDraw> draws;
      std::vector<World::EntityId> entities;

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
    std::unordered_map<std::string, MaterialEntry> materials;
    std::unordered_map<std::string, TextureEntry> textures;
    std::uint64_t skinScene{};
    std::unordered_map<std::uint64_t, SkinEntry> skins;
    std::unordered_map<std::string, Animation::Skin> skinAssets;
    std::optional<PendingSkin> pendingSkin;
    std::optional<MaterialPreview> preview;
    std::optional<PendingMaterial> pendingMaterial;
    std::unique_ptr<ImageJob> imageJob;
    std::unique_ptr<HdrJob> hdrJob;
    std::optional<PendingEnvironment> pendingEnvironment;

    struct EnvironmentEntry {
      std::string asset;
      EnvironmentConfig processing;
      EnvironmentMap map;
      std::uint64_t bytes;
    };

    std::optional<EnvironmentEntry> environment;
    /** Preparation can replace the cache before drawing replaces the binding.
     * 准备可在绘制替换绑定前替换缓存，因此独立保留本服务实际绑定的资源。 */
    std::optional<EnvironmentMap> boundEnvironment;
    std::optional<std::uint64_t> occlusionRevision;

    /** Detach only our binding; another scene renderer may now own the active map.
     * 仅解绑本服务的资源；另一个场景渲染器可能已接管当前环境。 */
    auto detachEnvironment() -> Core::Result<void> {
      if (!boundEnvironment) {
        return {};
      }

      auto current = renderer->environment();

      if (!current) {
        return std::unexpected(current.error());
      }

      if (*current && (**current).map == *boundEnvironment) {
        auto detached = renderer->setEnvironment(std::nullopt);

        if (!detached) {
          return detached;
        }
      }

      boundEnvironment.reset();
      return {};
    }

    /** Compare generations, not equal settings: another owner may bind the same value.
     * 比较代次而非设置值，因为另一个所有者可能绑定完全相同的配置。 */
    auto detachOcclusion() -> Core::Result<void> {
      if (!occlusionRevision) {
        return {};
      }

      auto current = renderer->occlusionRevision();

      if (!current) {
        return std::unexpected(current.error());
      }

      if (*current == *occlusionRevision) {
        auto detached = renderer->setOcclusion(std::nullopt);

        if (!detached) {
          return detached;
        }
      }

      occlusionRevision.reset();
      return {};
    }

    ~Impl() {
      /** Device failure may prevent unbinding; renderer teardown releases its
       * retained binding. Destruction itself must not throw.
       * 设备故障可能阻止解绑；渲染器销毁会释放其绑定，析构本身不得抛异常。 */
      try {
        cancelMaterialPreparation();
        cancelEnvironmentPreparation();
        imageJob.reset();
        hdrJob.reset();
        static_cast<void>(detachEnvironment());
        static_cast<void>(detachOcclusion());
      } catch (...) {
      }
    }

    auto bindEnvironment(const World::Scene& scene) -> Core::Result<void> {
      auto source = scene.environment();

      if (!source) {
        return std::unexpected(source.error());
      }

      if (!*source) {
        auto cleared = renderer->setEnvironment(std::nullopt);

        if (!cleared) {
          return cleared;
        }

        if (environment) {
          bytes -= environment->bytes;
          environment.reset();
        }

        boundEnvironment.reset();
        return {};
      }

      const auto& description = **source;
      std::optional<EnvironmentEntry> pending;

      if (!environment || environment->asset != description.asset
          || environment->processing != description.processing) {
        if (!assets) {
          return Core::fail(Core::ErrorCode::NotFound, "Scene environment requires an asset root");
        }

        const auto path = std::filesystem::path(
            std::u8string(description.asset.begin(), description.asset.end()));
        auto image = Assets::loadHdrImage(*assets, path);

        if (!image) {
          return Core::fail(image.error().code, "Scene environment " + description.asset + ": "
                                                    + image.error().message);
        }

        auto info = validateEnvironment(*image, description.processing);

        if (!info) {
          return Core::fail(info.error().code,
                            "Scene environment " + description.asset + ": " + info.error().message);
        }

        if (info->bytes > budget - bytes) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene environment cache budget exceeded");
        }

        auto baked = renderer->bakeEnvironment(*image, description.processing);

        if (!baked) {
          return Core::fail(baked.error().code, "Bake scene environment " + description.asset + ": "
                                                    + baked.error().message);
        }

        pending = EnvironmentEntry{description.asset, description.processing, std::move(*baked),
                                   info->bytes};
      }

      const auto& map = pending ? pending->map : environment->map;
      auto bound = renderer->setEnvironment(EnvironmentLighting{
          map, description.intensity, description.rotationDegrees, description.background});

      if (!bound) {
        return bound;
      }

      boundEnvironment = map;

      if (pending) {
        if (environment) {
          bytes -= environment->bytes;
        }

        bytes += pending->bytes;
        environment = std::move(pending);
      }

      return {};
    }

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

      auto bounds = meshBounds(data);

      if (!bounds) {
        return std::unexpected(bounds.error());
      }

      return Entry{std::move(*uploaded), resourceBytes, std::move(warning), *bounds};
    }

    /** Retire controllers before shared inputs. Scoped identities prevent reuse
     * across scenes; bounds may retain hidden meshes until the next frame.
     * 先释放控制器再释放共用输入；场景范围防止跨场景复用，包围盒可保留隐藏网格至下一帧。 */
    auto pruneSkins(const World::Scene& scene, const std::vector<World::EntityInfo>& entities,
                    bool visibleOnly) -> void {
      if (skinScene != scene.id()) {
        pendingSkin.reset();

        for (const auto& [id, entry] : skins) {
          bytes -= entry.bytes;
        }

        skins.clear();
        skinAssets.clear();
        skinScene = scene.id();
      }

      std::unordered_map<std::uint64_t, const World::EntityInfo*> current;

      for (const auto& entity : entities) {
        current.emplace(entity.id.value, &entity);
      }

      std::unordered_set<std::string> needed;

      for (auto iterator = skins.begin(); iterator != skins.end();) {
        const auto found = current.find(iterator->first);
        const auto* entity = found == current.end() ? nullptr : found->second;
        const bool keep = entity && entity->mesh && (!visibleOnly || entity->mesh->visible)
                          && entity->skin && entity->skin->enabled
                          && *entity->skin == iterator->second.properties
                          && key(*entity->mesh).value() == iterator->second.mesh;

        if (!keep) {
          pendingSkin.reset();
          bytes -= iterator->second.bytes;
          iterator = skins.erase(iterator);
        } else {
          needed.insert(iterator->second.properties.asset);
          ++iterator;
        }
      }

      std::erase_if(skinAssets, [&](const auto& entry) { return !needed.contains(entry.first); });
    }

    auto skin(const World::Scene& scene, const World::EntityInfo& entity,
              const std::string& meshKey, const Entry& source) -> Core::Result<SkinEntry*> {
      auto existing = skins.find(entity.id.value);

      if (existing != skins.end()) {
        return &existing->second;
      }

      if (!assets) {
        return Core::fail(Core::ErrorCode::InvalidState, "Scene skin requires an asset root");
      }

      auto cached = skinAssets.find(entity.skin->asset);
      auto loaded = cached != skinAssets.end()
                        ? Core::Result<Animation::Skin>{cached->second}
                        : Animation::loadSkin(*assets, assetPath(entity.skin->asset));

      if (!loaded) {
        return std::unexpected(loaded.error());
      }

      auto binding = Animation::SceneSkin::bind(scene, entity.id, *loaded);

      if (!binding) {
        return std::unexpected(binding.error());
      }

      const auto data = loaded->description().value();
      const auto mesh = source.mesh.info().value();

      if (mesh.vertices != data->vertices.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scene skin vertex count does not match its mesh");
      }

      /** Charge both posed buffers, weights, palette and compact GPU controls.
       * Shared static input/index bytes are already charged by cache.
       * 计入两个变形缓冲、权重、调色板及 GPU 控制；共用静态输入/索引已由 cache 计费。 */
      const auto residency = 2ULL * mesh.vertices * sizeof(Vertex)
                             + data->vertices.size() * sizeof(Animation::SkinInfluence)
                             + data->inverseBindMatrices.size() * sizeof(Math::Mat4) + 48
                             + (mesh.indices == 0 ? 4U : 0U);

      if (residency > budget - bytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene skin cache byte budget exceeded");
      }

      auto controller = renderer->createSkin(source.mesh, *loaded);

      if (!controller) {
        return std::unexpected(controller.error());
      }

      skinAssets.try_emplace(entity.skin->asset, *loaded);
      auto inserted = skins.emplace(
          entity.id.value,
          SkinEntry{
              meshKey, *entity.skin, std::move(*binding), std::move(*controller), {}, residency});
      bytes += residency;
      return &inserted.first->second;
    }

    auto loadMaterial(std::string_view path) -> Core::Result<MaterialEntry> {
      if (!assets) {
        return Core::fail(Core::ErrorCode::InvalidState, "Scene material has no asset root");
      }

      auto description = preview && preview->path == path
                             ? Core::Result<Assets::MaterialDescription>{preview->value}
                             : Assets::loadMaterial(*assets, assetPath(path));

      if (!description) {
        return std::unexpected(description.error());
      }

      return resolveMaterial(*description);
    }

    auto resolveMaterial(const Assets::MaterialDescription& value,
                         std::unordered_map<std::string, TextureEntry> pending = {})
        -> Core::Result<MaterialEntry> {
      auto checked = Assets::validateMaterial(value);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      const auto* description = &value;
      MaterialEntry result;
      result.material.baseColor = description->baseColor;
      result.material.emissive = description->emissive;
      result.material.model = description->model;
      result.material.metallic = description->metallic;
      result.material.roughness = description->roughness;
      result.material.normalScale = description->normalScale;
      result.material.alphaMode = description->alphaMode;
      result.material.alphaCutoff = description->alphaCutoff;
      result.material.occlusionStrength = description->occlusionStrength;
      result.material.doubleSided = description->doubleSided;
      const std::array sources{&description->baseColorTexture,
                               &description->metallicRoughnessTexture, &description->normalTexture,
                               &description->emissiveTexture, &description->occlusionTexture};
      const std::array slots{&result.material.baseColorTexture,
                             &result.material.metallicRoughnessTexture,
                             &result.material.normalTexture, &result.material.emissiveTexture,
                             &result.material.occlusionTexture};
      std::uint64_t pendingBytes{};

      for (const auto& [identity, entry] : pending) {
        pendingBytes += entry.bytes;
      }

      if (pendingBytes > budget - bytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene staged texture budget exhausted");
      }

      for (std::size_t index = 0; index < sources.size(); ++index) {
        if (!*sources[index]) {
          continue;
        }

        const auto& source = **sources[index];
        const auto key = textureKey(source);
        result.textures.push_back(key);
        auto existing = textures.find(key);

        if (existing != textures.end()) {
          *slots[index] = existing->second.texture;
          continue;
        }

        auto local = pending.find(key);

        if (local != pending.end()) {
          *slots[index] = local->second.texture;
          continue;
        }

        auto image = Assets::loadImage(*assets, assetPath(source.path));

        if (!image) {
          return Core::fail(image.error().code,
                            "Material texture '" + source.path + "': " + image.error().message);
        }

        const auto size = textureBytes(*image, source.sampling.mipmaps);

        if (size > budget - bytes - pendingBytes || textures.size() + pending.size() >= 4096) {
          return Core::fail(Core::ErrorCode::Unavailable, "Scene texture cache budget exhausted");
        }

        auto uploaded = renderer->uploadTexture(*image, source.sampling);

        if (!uploaded) {
          return Core::fail(uploaded.error().code,
                            "Material texture '" + source.path + "': " + uploaded.error().message);
        }

        *slots[index] = *uploaded;
        pending.emplace(key, TextureEntry{std::move(*uploaded), size});
        pendingBytes += size;
      }

      /** Only complete material loads publish new textures into the cache.
       * 只有完整材质加载成功后，才将新上传的贴图发布到缓存。 */
      textures.reserve(textures.size() + pending.size());

      while (!pending.empty()) {
        auto node = pending.extract(pending.begin());
        const auto inserted = textures.insert(std::move(node));
        bytes += inserted.position->second.bytes;
      }

      return result;
    }

    auto lighting(const World::Scene& scene, std::optional<World::EntityId> camera,
                  const View* custom, const std::vector<World::EntityInfo>& entities)
        -> Core::Result<Lighting> {
      Lighting result;

      if (custom) {
        result.viewPosition = custom->position;

        if (custom->orthographic) {
          auto world = Math::compose({}, custom->rotation);

          if (!world) {
            return std::unexpected(world.error());
          }

          auto direction = Math::transformVector(*world, {0, 0, -1});

          if (!direction) {
            return std::unexpected(direction.error());
          }

          result.viewDirection = *direction;
        }
      } else {
        auto world = scene.worldTransform(*camera);
        auto info = scene.entity(*camera);

        if (!world || !info) {
          return std::unexpected(!world ? world.error() : info.error());
        }

        auto position = Math::transformPoint(*world, {});

        if (!position) {
          return std::unexpected(position.error());
        }

        result.viewPosition = *position;

        if (info->camera->projection == World::Projection::Orthographic) {
          auto direction = Math::transformVector(*world, {0, 0, -1});

          if (!direction) {
            return std::unexpected(direction.error());
          }

          result.viewDirection = *direction;
        }
      }

      for (const auto& entity : entities) {
        if (!entity.light || !entity.light->enabled) {
          continue;
        }

        if (result.lights.size() == 64) {
          return Core::fail(Core::ErrorCode::Unavailable, "Scene exceeds 64 enabled direct lights");
        }

        const auto& source = *entity.light;
        auto transform = scene.worldTransform(entity.id);

        if (!transform) {
          return std::unexpected(transform.error());
        }

        auto lightPosition = Math::transformPoint(*transform, {});
        auto direction = Math::transformVector(*transform, {0, 0, -1});

        if (!lightPosition || !direction) {
          return std::unexpected(!lightPosition ? lightPosition.error() : direction.error());
        }

        result.lights.push_back({.kind = source.kind,
                                 .direction = *direction,
                                 .position = *lightPosition,
                                 .color = source.color,
                                 .intensity = source.intensity,
                                 .range = source.range,
                                 .innerConeDegrees = source.innerConeDegrees,
                                 .outerConeDegrees = source.outerConeDegrees,
                                 .shadows = source.shadows});
      }

      return validatedLighting(result);
    }

    /** Cancellation never joins a running codec on a frame; canceled output is
     * discarded when polled. Destruction joins before borrowed renderer teardown.
     * 帧内取消不等待活动编解码，轮询时丢弃迟到结果；销毁在借用渲染器退出前等待线程。 */
    auto cancelMaterialPreparation() -> void {
      pendingMaterial.reset();

      if (imageJob) {
        imageJob->canceled = true;
        imageJob->thread.request_stop();
      }
    }

    auto takeImage() -> std::optional<Core::Result<std::vector<Image>>> {
      if (!imageJob) {
        return std::nullopt;
      }

      std::optional<Core::Result<std::vector<Image>>> result;

      {
        std::lock_guard lock(imageJob->result->mutex);
        result = std::move(imageJob->result->value);
      }

      if (result) {
        imageJob.reset();
      }

      return result;
    }

    auto startImage(const Assets::TextureSource& source) -> void {
      auto job = std::make_unique<ImageJob>();
      job->result = std::make_shared<ImageResult>();
      job->thread
          = std::jthread([state = job->result, files = *assets, source](std::stop_token stop) {
              Core::Result<std::vector<Image>> result
                  = Core::fail(Core::ErrorCode::External, "Image preparation did not run");

              try {
                const auto build = [&]() -> Core::Result<std::vector<Image>> {
                  if (stop.stop_requested()) {
                    return Core::fail(Core::ErrorCode::Unavailable, "Image preparation canceled");
                  }

                  auto image = Assets::loadImage(files, assetPath(source.path));

                  if (!image) {
                    return std::unexpected(image.error());
                  }

                  std::vector<Image> levels;
                  levels.reserve(14);
                  levels.push_back(std::move(*image));

                  while (source.sampling.mipmaps
                         && (levels.back().width > 1 || levels.back().height > 1)) {
                    if (stop.stop_requested()) {
                      return Core::fail(Core::ErrorCode::Unavailable, "Image preparation canceled");
                    }

                    auto next = downsampleImage(levels.back(), source.sampling.colorSpace);

                    if (!next) {
                      return std::unexpected(next.error());
                    }

                    levels.push_back(std::move(*next));
                  }

                  return levels;
                };

                result = build();
              } catch (const std::exception& error) {
                result = Core::fail(Core::ErrorCode::External,
                                    "Prepare image: " + std::string(error.what()));
              } catch (...) {
                result = Core::fail(Core::ErrorCode::External, "Unknown image preparation failure");
              }

              std::lock_guard lock(state->mutex);
              state->value.emplace(std::move(result));
            });
      imageJob = std::move(job);
    }

    auto cancelEnvironmentPreparation() -> void {
      pendingEnvironment.reset();

      if (hdrJob) {
        hdrJob->canceled = true;
        hdrJob->thread.request_stop();
      }
    }

    auto takeHdr() -> std::optional<Core::Result<EnvironmentSource>> {
      if (!hdrJob) {
        return std::nullopt;
      }

      std::optional<Core::Result<EnvironmentSource>> result;

      {
        std::lock_guard lock(hdrJob->result->mutex);
        result = std::move(hdrJob->result->value);
      }

      if (result) {
        hdrJob.reset();
      }

      return result;
    }

    auto startHdr(std::string path) -> void {
      auto job = std::make_unique<HdrJob>();
      job->result = std::make_shared<HdrResult>();
      job->thread = std::jthread([state = job->result, files = *assets,
                                  path = std::move(path)](std::stop_token stop) {
        Core::Result<EnvironmentSource> result
            = Core::fail(Core::ErrorCode::External, "HDR preparation did not run");

        try {
          if (stop.stop_requested()) {
            result = Core::fail(Core::ErrorCode::Unavailable, "HDR preparation canceled");
          } else {
            auto image = Assets::loadHdrImage(files, assetPath(path));
            result = image ? EnvironmentSource::prepare(std::move(*image), stop)
                           : Core::Result<EnvironmentSource>{std::unexpected(image.error())};
          }
        } catch (const std::exception& error) {
          result
              = Core::fail(Core::ErrorCode::External, "Prepare HDR: " + std::string(error.what()));
        } catch (...) {
          result = Core::fail(Core::ErrorCode::External, "Unknown HDR preparation failure");
        }

        std::lock_guard lock(state->mutex);
        state->value.emplace(std::move(result));
      });
      hdrJob = std::move(job);
    }

    auto prepareEnvironment(const World::Scene& scene) -> Core::Result<bool> {
      try {
        auto source = scene.environment();

        if (!source) {
          return std::unexpected(source.error());
        }

        if (!*source) {
          cancelEnvironmentPreparation();
          static_cast<void>(takeHdr());
          return true;
        }

        const auto& value = **source;
        const auto matches = [&](const auto& entry) {
          return entry.asset == value.asset && entry.processing == value.processing;
        };

        if (environment && matches(*environment)) {
          cancelEnvironmentPreparation();
          static_cast<void>(takeHdr());
          return true;
        }

        if (pendingEnvironment && !matches(pendingEnvironment->source)) {
          cancelEnvironmentPreparation();
        }

        if (hdrJob && hdrJob->canceled) {
          static_cast<void>(takeHdr());

          if (hdrJob) {
            return false;
          }
        }

        if (!pendingEnvironment) {
          cancelMaterialPreparation();
          pendingEnvironment.emplace();
          pendingEnvironment->source = value;
        }

        auto& pending = *pendingEnvironment;
        const auto fail = [&](Core::Error error) -> Core::Result<bool> {
          error.message = "Prepare scene environment '" + value.asset + "': " + error.message;
          pending.error = std::move(error);
          pending.bake.reset();
          return std::unexpected(*pending.error);
        };

        if (pending.error) {
          return std::unexpected(*pending.error);
        }

        if (pending.bake) {
          auto ready = renderer->pollEnvironmentBake(*pending.bake);

          if (!ready) {
            return fail(ready.error());
          }

          if (!*ready) {
            return false;
          }

          const auto size = (**ready).info()->bytes;
          EnvironmentEntry next{value.asset, value.processing, std::move(**ready), size};

          if (environment) {
            bytes -= environment->bytes;
          }

          bytes += size;
          environment = std::move(next);
          pendingEnvironment.reset();
          return true;
        }

        if (imageJob) {
          static_cast<void>(takeImage());

          if (imageJob) {
            return false;
          }
        }

        if (!assets) {
          return fail(
              Core::fail(Core::ErrorCode::NotFound, "Scene environment requires an asset root")
                  .error());
        }

        if (!hdrJob) {
          startHdr(value.asset);
          return false;
        }

        auto prepared = takeHdr();

        if (!prepared) {
          return false;
        }

        if (!*prepared) {
          return fail(prepared->error());
        }

        auto info = prepared->value().description(value.processing);

        if (!info) {
          return fail(info.error());
        }

        if (info->bytes > budget - bytes) {
          return fail(
              Core::fail(Core::ErrorCode::Unavailable, "Scene environment cache budget exceeded")
                  .error());
        }

        auto task = renderer->beginEnvironmentBake(prepared->value(), value.processing);

        if (!task) {
          return fail(task.error());
        }

        pending.bake.emplace(std::move(*task));
        return false;
      } catch (const std::exception& error) {
        auto failure = Core::fail(Core::ErrorCode::External,
                                  "Prepare scene environment: " + std::string(error.what()));

        if (pendingEnvironment) {
          pendingEnvironment->error = failure.error();
          pendingEnvironment->bake.reset();
        }

        return failure;
      }
    }

    struct Requirements {
      std::size_t draws;
      std::vector<std::string> materials;
    };

    auto prune(const World::Scene& scene, const std::vector<World::EntityInfo>& entities)
        -> Core::Result<Requirements> {
      std::unordered_set<std::string> needed;
      std::unordered_set<std::string> neededMaterials;
      std::size_t drawCount = 0;

      for (const auto& entity : entities) {
        if (entity.mesh && entity.mesh->visible) {
          auto resourceKey = key(*entity.mesh);

          if (!resourceKey) {
            return std::unexpected(resourceKey.error());
          }

          needed.insert(*resourceKey);

          if (entity.material) {
            neededMaterials.insert(entity.material->asset);
          }
          ++drawCount;
        }
      }

      if (drawCount > 10000) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Visible scene exceeds 10000 mesh instances");
      }

      if (pendingMaterial
          && (!neededMaterials.contains(pendingMaterial->path)
              || materials.contains(pendingMaterial->path))) {
        cancelMaterialPreparation();
      }

      pruneSkins(scene, entities, true);

      for (auto iterator = cache.begin(); iterator != cache.end();) {
        if (!needed.contains(iterator->first)) {
          bytes -= iterator->second.bytes;
          iterator = cache.erase(iterator);
        } else {
          ++iterator;
        }
      }

      for (auto iterator = materials.begin(); iterator != materials.end();) {
        if (!neededMaterials.contains(iterator->first)) {
          iterator = materials.erase(iterator);
        } else {
          ++iterator;
        }
      }

      std::unordered_set<std::string> neededTextures;

      for (const auto& [path, material] : materials) {
        neededTextures.insert(material.textures.begin(), material.textures.end());
      }

      if (pendingMaterial) {
        for (const auto& [identity, source] : pendingMaterial->sources) {
          neededTextures.insert(identity);
        }
      }

      if (preview && neededMaterials.contains(preview->path)) {
        const auto& value = preview->value;
        const std::array sources{&value.baseColorTexture, &value.metallicRoughnessTexture,
                                 &value.normalTexture, &value.emissiveTexture,
                                 &value.occlusionTexture};

        for (const auto* source : sources) {
          if (*source) {
            neededTextures.insert(textureKey(**source));
          }
        }
      }

      for (auto iterator = textures.begin(); iterator != textures.end();) {
        if (!neededTextures.contains(iterator->first)) {
          bytes -= iterator->second.bytes;
          iterator = textures.erase(iterator);
        } else {
          ++iterator;
        }
      }

      std::vector<std::string> paths(neededMaterials.begin(), neededMaterials.end());
      std::ranges::sort(paths);
      return Requirements{drawCount, std::move(paths)};
    }

    auto prepareMaterials(const World::Scene& scene) -> Core::Result<bool> {
      try {
        auto entities = scene.entities();

        if (!entities) {
          return std::unexpected(entities.error());
        }

        auto requirements = prune(scene, *entities);

        if (!requirements) {
          return std::unexpected(requirements.error());
        }

        if (imageJob && imageJob->canceled) {
          static_cast<void>(takeImage());
        }

        const auto missing = std::ranges::find_if(
            requirements->materials, [&](const auto& path) { return !materials.contains(path); });

        if (missing == requirements->materials.end()) {
          return true;
        }

        if ((imageJob && imageJob->canceled) || hdrJob) {
          return false;
        }

        if (!pendingMaterial) {
          if (materials.size() >= 4096) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene material cache capacity exhausted");
          }

          if (!assets) {
            return Core::fail(Core::ErrorCode::InvalidState, "Scene material has no asset root");
          }

          auto description = preview && preview->path == *missing
                                 ? Core::Result<Assets::MaterialDescription>{preview->value}
                                 : Assets::loadMaterial(*assets, assetPath(*missing));

          if (!description) {
            pendingMaterial.emplace();
            pendingMaterial->path = *missing;
            pendingMaterial->error = description.error();
            return std::unexpected(description.error());
          }

          PendingMaterial pending;
          pending.path = *missing;
          pending.description = std::move(*description);
          const auto& value = pending.description;
          const std::array sources{&value.baseColorTexture, &value.metallicRoughnessTexture,
                                   &value.normalTexture, &value.emissiveTexture,
                                   &value.occlusionTexture};
          std::unordered_set<std::string> seen;

          for (const auto* source : sources) {
            if (*source) {
              auto identity = textureKey(**source);

              if (seen.insert(identity).second) {
                pending.sources.emplace_back(std::move(identity), **source);
              }
            }
          }

          pendingMaterial = std::move(pending);
        }

        auto& pending = *pendingMaterial;

        if (pending.error) {
          return std::unexpected(*pending.error);
        }

        const auto fail = [&](Core::Error error) -> Core::Result<bool> {
          error.message = "Prepare scene material '" + pending.path + "': " + error.message;
          pending.error = std::move(error);
          pending.staged.clear();
          return std::unexpected(*pending.error);
        };

        std::erase_if(pending.staged,
                      [&](const auto& entry) { return textures.contains(entry.first); });

        while (pending.cursor < pending.sources.size()) {
          const auto& [identity, source] = pending.sources[pending.cursor];

          if (imageJob) {
            auto result = takeImage();

            if (!result) {
              return false;
            }

            if (!*result) {
              return fail(result->error());
            }

            if (!textures.contains(identity)) {
              std::uint64_t stagedBytes{};

              for (const auto& [name, entry] : pending.staged) {
                stagedBytes += entry.bytes;
              }

              const auto size = textureBytes(result->value().front(), source.sampling.mipmaps);

              if (stagedBytes > budget - bytes || size > budget - bytes - stagedBytes
                  || textures.size() + pending.staged.size() >= 4096) {
                return fail(
                    Core::fail(Core::ErrorCode::Unavailable, "Scene texture cache budget exhausted")
                        .error());
              }

              auto uploaded = renderer->uploadTextureLevels(result->value(), source.sampling);

              if (!uploaded) {
                return fail(uploaded.error());
              }

              pending.staged.emplace(identity, TextureEntry{std::move(*uploaded), size});
            }

            ++pending.cursor;
            return false;
          }

          if (textures.contains(identity) || pending.staged.contains(identity)) {
            ++pending.cursor;
            continue;
          }

          startImage(source);
          return false;
        }

        for (std::size_t index = 0; index < pending.sources.size(); ++index) {
          const auto& identity = pending.sources[index].first;

          if (!textures.contains(identity) && !pending.staged.contains(identity)) {
            pending.cursor = index;
            return false;
          }
        }

        auto resolved = resolveMaterial(pending.description, std::move(pending.staged));

        if (!resolved) {
          return fail(resolved.error());
        }

        materials.emplace(pending.path, std::move(*resolved));
        pendingMaterial.reset();
        return std::ranges::all_of(requirements->materials,
                                   [&](const auto& path) { return materials.contains(path); });
      } catch (const std::exception& error) {
        auto failure = Core::fail(Core::ErrorCode::External,
                                  "Prepare scene materials: " + std::string(error.what()));

        if (pendingMaterial) {
          pendingMaterial->error = failure.error();
          pendingMaterial->staged.clear();
        }

        return failure;
      }
    }

    /** Prepare only new/rebound controllers, never throttle current animation poses.
     * Compare owned palettes before polling so changed scenes cannot publish stale work.
     * 仅准备新建/改绑控制器，不节流已有动画姿态；轮询前比较拥有的调色板，防止变化场景发布旧工作。
     */
    auto prepareInitialSkins(const World::Scene& scene) -> Core::Result<bool> {
      try {
        auto entities = scene.entities();

        if (!entities) {
          return std::unexpected(entities.error());
        }

        auto requirements = prune(scene, *entities);

        if (!requirements) {
          return std::unexpected(requirements.error());
        }

        std::vector<InitialSkin> rows;

        for (const auto& entity : *entities) {
          if (!entity.mesh || !entity.mesh->visible || !entity.skin || !entity.skin->enabled) {
            continue;
          }

          auto resourceKey = key(*entity.mesh);

          if (!resourceKey) {
            return std::unexpected(resourceKey.error());
          }

          auto source = cache.find(*resourceKey);

          if (source == cache.end()) {
            auto loaded = load(*entity.mesh);

            if (!loaded) {
              return std::unexpected(loaded.error());
            }

            source = cache.emplace(*resourceKey, std::move(*loaded)).first;
            bytes += source->second.bytes;
          }

          auto bound = skin(scene, entity, *resourceKey, source->second);

          if (!bound) {
            return std::unexpected(bound.error());
          }

          if ((*bound)->palette.empty()) {
            auto palette = (*bound)->binding.palette(scene);

            if (!palette) {
              return std::unexpected(palette.error());
            }

            rows.push_back({entity.id.value, std::move(*palette)});
          }
        }

        std::ranges::sort(rows, {}, &InitialSkin::owner);

        if (pendingSkin
            && (!std::ranges::equal(pendingSkin->rows, rows, {}, &InitialSkin::owner,
                                    &InitialSkin::owner)
                || (pendingSkin->failed && pendingSkin->rows != rows))) {
          pendingSkin.reset();
        }

        if (rows.empty()) {
          return true;
        }

        if (!pendingSkin) {
          std::vector<SkinUpdate> updates;
          updates.reserve(rows.size());

          for (const auto& row : rows) {
            updates.push_back({&skins.at(row.owner).controller, row.palette});
          }

          auto submitted = renderer->beginSkinUpdate(updates);

          if (!submitted) {
            return std::unexpected(submitted.error());
          }

          pendingSkin.emplace(PendingSkin{std::move(rows), std::move(*submitted)});
          return false;
        }

        auto ready = renderer->pollSkinUpdate(pendingSkin->batch);

        if (!ready) {
          pendingSkin->failed = true;
          return std::unexpected(ready.error());
        }

        if (!*ready) {
          return false;
        }

        for (auto& row : pendingSkin->rows) {
          skins.at(row.owner).palette = std::move(row.palette);
        }

        pendingSkin.reset();
        return true;
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Prepare initial skin poses: " + std::string(error.what()));
      }
    }

    auto frame(const World::Scene& scene, float aspect, const SceneView& camera)
        -> Core::Result<Frame> {
      try {
        const auto* custom = std::get_if<View>(&camera);
        std::optional<World::EntityId> selected;
        Core::Result<Math::Mat4> view = Math::Mat4{};

        if (custom) {
          view = Graphics::viewProjection(*custom, aspect);
        } else {
          auto entity = std::get_if<World::EntityId>(&camera);
          auto resolved = entity ? Core::Result<World::EntityId>(*entity) : scene.primaryCamera();

          if (!resolved) {
            return std::unexpected(resolved.error());
          }

          selected = *resolved;
          view = scene.viewProjection(*selected, aspect);
        }

        if (!view) {
          return std::unexpected(view.error());
        }

        auto entities = scene.entities();

        if (!entities) {
          return std::unexpected(entities.error());
        }

        auto lights = lighting(scene, selected, custom, *entities);

        if (!lights) {
          return std::unexpected(lights.error());
        }

        auto requirements = prune(scene, *entities);

        if (!requirements) {
          return std::unexpected(requirements.error());
        }

        /** Explicit synchronous drawing supersedes unpublished preparation.
         * 显式同步绘制取代未发布的准备事务。 */
        pendingSkin.reset();
        Frame frame;
        frame.camera = *view;
        /** Draw pointers borrow these mesh handles; reserve once and keep the frame
         * move-only so their addresses remain stable throughout submission.
         * 绘制指针借用这些网格句柄；一次性预留空间并仅允许移动帧，确保提交期间地址稳定。
         */
        frame.meshes.reserve(requirements->draws);
        frame.draws.reserve(requirements->draws);
        std::vector<std::vector<Math::Mat4>> palettes;
        std::vector<SkinUpdate> updates;
        std::vector<SkinEntry*> changedSkins;
        palettes.reserve(128);
        updates.reserve(128);
        changedSkins.reserve(128);

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

          Material material;

          if (entity.material) {
            auto cached = materials.find(entity.material->asset);

            if (cached == materials.end()) {
              auto loaded = loadMaterial(entity.material->asset);

              if (!loaded) {
                return Core::fail(loaded.error().code, "Scene material '" + entity.material->asset
                                                           + "': " + loaded.error().message);
              }

              cached = materials.emplace(entity.material->asset, std::move(*loaded)).first;
            }

            material = cached->second.material;
          }

          frame.entities.push_back(entity.id);
          Mesh mesh = found->second.mesh;

          if (entity.skin && entity.skin->enabled) {
            auto bound = skin(scene, entity, *resourceKey, found->second);

            if (!bound) {
              return Core::fail(bound.error().code, "Scene skin owner "
                                                        + std::to_string(entity.id.value) + ": "
                                                        + bound.error().message);
            }

            auto palette = (*bound)->binding.palette(scene);

            if (!palette) {
              return std::unexpected(palette.error());
            }

            if (*palette != (*bound)->palette) {
              palettes.push_back(std::move(*palette));
              updates.push_back({&(*bound)->controller, palettes.back()});
              changedSkins.push_back(*bound);
            }

            mesh = (*bound)->controller.mesh().value();
          }

          frame.meshes.push_back(std::move(mesh));
          frame.draws.push_back({&frame.meshes.back(), *world, entity.mesh->tint,
                                 std::move(material), entity.mesh->castsShadow,
                                 entity.mesh->receivesShadow});
        }

        auto changed = bindEnvironment(scene);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = renderer->setLighting(*lights);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        auto occlusion = scene.occlusion();

        if (!occlusion) {
          return std::unexpected(occlusion.error());
        }

        changed = renderer->setOcclusion(*occlusion);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        auto revision = renderer->occlusionRevision();

        if (!revision) {
          return std::unexpected(revision.error());
        }

        occlusionRevision = *revision;

        if (!updates.empty()) {
          auto updated = renderer->updateSkins(updates);

          if (!updated) {
            return std::unexpected(updated.error());
          }

          /** Commit pose dependencies only after the whole GPU batch validates.
           * 仅整个 GPU 批次校验成功后发布姿态依赖。 */
          for (std::size_t i = 0; i < changedSkins.size(); ++i) {
            changedSkins[i]->palette = std::move(palettes[i]);
          }
        }

        return Core::Result<Frame>{std::move(frame)};
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Prepare scene frame: " + std::string(error.what()));
      }
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

  auto SceneRenderer::readiness(const World::Scene& scene) const
      -> Core::Result<ScenePreparationStage> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto environment = scene.environment();
    auto environmentReady = m_impl->renderer->environmentPipelinesReady();
    auto shadowsReady = m_impl->renderer->shadowPipelinesReady();
    auto occlusionReady = m_impl->renderer->occlusionPipelinesReady();
    auto skinsReady = m_impl->renderer->skinPipelinesReady();

    if (!environment || !environmentReady || !shadowsReady) {
      return std::unexpected(!environment        ? environment.error()
                             : !environmentReady ? environmentReady.error()
                                                 : shadowsReady.error());
    }

    if (!occlusionReady) {
      return std::unexpected(occlusionReady.error());
    }

    if (!skinsReady) {
      return std::unexpected(skinsReady.error());
    }

    if (*environment && !*environmentReady) {
      return ScenePreparationStage::Environment;
    }

    if (!*shadowsReady) {
      auto entities = scene.entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      if (std::ranges::any_of(*entities, [](const auto& entity) {
            return entity.light && entity.light->enabled && entity.light->shadows;
          })) {
        return ScenePreparationStage::Shadows;
      }
    }

    if (!*occlusionReady) {
      auto occlusion = scene.occlusion();

      if (!occlusion) {
        return std::unexpected(occlusion.error());
      }

      if (*occlusion) {
        return ScenePreparationStage::Occlusion;
      }
    }

    auto entities = scene.entities();

    if (!entities) {
      return std::unexpected(entities.error());
    }

    if (!*skinsReady && std::ranges::any_of(*entities, [](const auto& entity) {
          return entity.mesh && entity.mesh->visible && entity.skin && entity.skin->enabled;
        })) {
      return ScenePreparationStage::Skins;
    }

    if (*environment
        && (!m_impl->environment || m_impl->environment->asset != (**environment).asset
            || m_impl->environment->processing != (**environment).processing)) {
      if (m_impl->pendingEnvironment
          && m_impl->pendingEnvironment->source.asset == (**environment).asset
          && m_impl->pendingEnvironment->source.processing == (**environment).processing
          && m_impl->pendingEnvironment->error) {
        return std::unexpected(*m_impl->pendingEnvironment->error);
      }

      return ScenePreparationStage::EnvironmentMap;
    }

    bool missingMaterial{};

    for (const auto& entity : *entities) {
      if (entity.mesh && entity.mesh->visible && entity.material
          && !m_impl->materials.contains(entity.material->asset)) {
        if (m_impl->pendingMaterial && m_impl->pendingMaterial->path == entity.material->asset
            && m_impl->pendingMaterial->error) {
          return std::unexpected(*m_impl->pendingMaterial->error);
        }

        missingMaterial = true;
      }
    }

    if (missingMaterial) {
      return ScenePreparationStage::Materials;
    }

    for (const auto& entity : *entities) {
      if (entity.mesh && entity.mesh->visible && entity.skin && entity.skin->enabled) {
        const auto found = m_impl->skins.find(entity.id.value);
        auto resourceKey = key(*entity.mesh);

        if (!resourceKey) {
          return std::unexpected(resourceKey.error());
        }

        if (m_impl->skinScene != scene.id() || found == m_impl->skins.end()
            || found->second.mesh != *resourceKey || found->second.properties != *entity.skin
            || found->second.palette.empty()) {
          return ScenePreparationStage::SkinPoses;
        }
      }
    }

    return ScenePreparationStage::Ready;
  }

  auto SceneRenderer::prepare(const World::Scene& scene) -> Core::Result<ScenePreparationStage> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto gui = m_impl->renderer->guiStats();

    if (!gui || gui->building) {
      return gui ? Core::fail(Core::ErrorCode::InvalidState,
                              "Scene preparation inside GUI callback")
                 : Core::Result<ScenePreparationStage>{std::unexpected(gui.error())};
    }

    auto stage = readiness(scene);

    if (!stage) {
      return std::unexpected(stage.error());
    }

    if (*stage == ScenePreparationStage::Environment) {
      auto ready = m_impl->renderer->prepareEnvironmentPipelines();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        return *stage;
      }

      stage = readiness(scene);

      if (!stage) {
        return std::unexpected(stage.error());
      }
    }

    if (*stage == ScenePreparationStage::Shadows) {
      auto ready = m_impl->renderer->prepareShadowPipelines();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        return *stage;
      }

      stage = readiness(scene);

      if (!stage) {
        return std::unexpected(stage.error());
      }
    }

    if (*stage == ScenePreparationStage::Occlusion) {
      auto ready = m_impl->renderer->prepareOcclusionPipelines();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        return *stage;
      }

      stage = readiness(scene);

      if (!stage) {
        return std::unexpected(stage.error());
      }
    }

    if (*stage == ScenePreparationStage::Skins) {
      auto ready = m_impl->renderer->prepareSkinPipelines();

      if (!ready) {
        return std::unexpected(ready.error());
      }

      if (!*ready) {
        return *stage;
      }
    }

    if (*stage == ScenePreparationStage::Ready && !m_impl->pendingMaterial && !m_impl->imageJob
        && !m_impl->pendingEnvironment && !m_impl->hdrJob && !m_impl->pendingSkin) {
      return ScenePreparationStage::Ready;
    }

    auto environment = m_impl->prepareEnvironment(scene);

    if (!environment) {
      return std::unexpected(environment.error());
    }

    if (!*environment) {
      return ScenePreparationStage::EnvironmentMap;
    }

    auto materials = m_impl->prepareMaterials(scene);

    if (!materials) {
      return std::unexpected(materials.error());
    }

    if (!*materials) {
      return ScenePreparationStage::Materials;
    }

    auto poses = m_impl->prepareInitialSkins(scene);
    return !poses   ? Core::Result<ScenePreparationStage>{std::unexpected(poses.error())}
           : *poses ? ScenePreparationStage::Ready
                    : ScenePreparationStage::SkinPoses;
  }

  auto SceneRenderer::render(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                             Math::Vec4 clear, SceneView camera, std::span<const ScreenDraw> screen)
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

    return m_impl->renderer->renderMeshes(frame->draws, frame->camera, width, height, clear,
                                          screen);
  }

  auto SceneRenderer::present(const World::Scene& scene, Math::Vec4 clear, SceneView camera,
                              std::span<const ScreenDraw> screen) -> Core::Result<PresentResult> {
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

    return m_impl->renderer->presentMeshes(frame->draws, frame->camera, clear, screen);
  }

  auto SceneRenderer::renderInto(const World::Scene& scene, RenderTarget& target, Math::Vec4 clear,
                                 SceneView camera, std::span<const ScreenDraw> screen)
      -> Core::Result<void> {
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

    return m_impl->renderer->renderMeshesInto(target, frame->draws, frame->camera, clear, screen);
  }

  auto SceneRenderer::pick(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                           Math::Vec2 uv, SceneView view)
      -> Core::Result<std::optional<World::EntityId>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!width || !height || width > 4096 || height > 4096 || !std::isfinite(uv.x)
        || !std::isfinite(uv.y) || uv.x < 0 || uv.x >= 1 || uv.y < 0 || uv.y >= 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scene picking viewport/UV");
    }

    auto frame = m_impl->frame(scene, static_cast<float>(width) / static_cast<float>(height), view);

    if (!frame) {
      return std::unexpected(frame.error());
    }

    auto selected = m_impl->renderer->pickMeshes(frame->draws, frame->camera, width, height, uv);

    if (!selected) {
      return std::unexpected(selected.error());
    }

    return *selected ? std::optional(frame->entities.at(**selected)) : std::nullopt;
  }

  auto SceneRenderer::bounds(const World::Scene& scene, World::EntityId entity)
      -> Core::Result<Bounds> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto info = scene.entity(entity);
    auto transform = scene.worldTransform(entity);

    if (!info || !transform) {
      return std::unexpected(!info ? info.error() : transform.error());
    }

    if (!info->mesh) {
      return transformedBounds({}, *transform);
    }

    auto resourceKey = key(*info->mesh);

    if (!resourceKey) {
      return std::unexpected(resourceKey.error());
    }

    try {
      auto entities = scene.entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      m_impl->pendingSkin.reset();
      m_impl->pruneSkins(scene, *entities, false);
      auto found = m_impl->cache.find(*resourceKey);

      if (found == m_impl->cache.end()) {
        auto loaded = m_impl->load(*info->mesh);

        if (!loaded) {
          return std::unexpected(loaded.error());
        }

        found = m_impl->cache.emplace(*resourceKey, std::move(*loaded)).first;
        m_impl->bytes += found->second.bytes;
      }

      if (info->skin && info->skin->enabled) {
        auto bound = m_impl->skin(scene, *info, *resourceKey, found->second);

        if (!bound) {
          return std::unexpected(bound.error());
        }

        auto palette = (*bound)->binding.palette(scene);

        if (!palette) {
          return std::unexpected(palette.error());
        }

        if (*palette != (*bound)->palette) {
          const std::array updates{SkinUpdate{&(*bound)->controller, *palette}};
          auto updated = m_impl->renderer->updateSkins(updates);

          if (!updated) {
            return std::unexpected(updated.error());
          }

          (*bound)->palette = std::move(*palette);
        }

        return transformedBounds((*bound)->controller.info()->bounds, *transform);
      }

      return transformedBounds(found->second.bounds, *transform);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Get scene bounds: " + std::string(error.what()));
    }
  }

  auto SceneRenderer::stats() const -> Core::Result<SceneRenderStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::uint64_t textureBytes{};
    std::uint64_t skinBytes{};

    for (const auto& [id, entry] : m_impl->skins) {
      skinBytes += entry.bytes;
    }

    for (const auto& [key, entry] : m_impl->textures) {
      textureBytes += entry.bytes;
    }

    return SceneRenderStats{m_impl->cache.size(),
                            m_impl->bytes,
                            m_impl->materials.size(),
                            m_impl->textures.size(),
                            textureBytes,
                            m_impl->environment ? 1U : 0U,
                            m_impl->environment ? m_impl->environment->bytes : 0U,
                            m_impl->skins.size(),
                            skinBytes};
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

  auto SceneRenderer::resolveMaterial(const Assets::MaterialDescription& value)
      -> Core::Result<Material> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!m_impl->assets) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Material resolution requires an asset root");
    }

    try {
      auto result = m_impl->resolveMaterial(value);
      return result ? Core::Result<Material>{std::move(result->material)}
                    : std::unexpected(result.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Resolve material: " + std::string(error.what()));
    }
  }

  auto SceneRenderer::previewMaterial(std::optional<MaterialPreview> preview)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (preview) {
      if (!Core::isPortableRelativePath(preview->path)
          || !preview->path.ends_with(".dotmaterial")) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid preview material path");
      }

      checked = Assets::validateMaterial(preview->value);

      if (!checked) {
        return checked;
      }
    }

    m_impl->cancelMaterialPreparation();

    if (m_impl->preview) {
      m_impl->materials.erase(m_impl->preview->path);
    }

    if (preview) {
      m_impl->materials.erase(preview->path);
    }

    m_impl->preview = std::move(preview);
    return {};
  }

  auto SceneRenderer::clearCache() -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene renderer has been moved from");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    m_impl->cancelMaterialPreparation();
    m_impl->cancelEnvironmentPreparation();
    checked = m_impl->detachEnvironment();

    if (!checked) {
      return checked;
    }

    checked = m_impl->detachOcclusion();

    if (!checked) {
      return checked;
    }

    m_impl->pendingSkin.reset();
    m_impl->environment.reset();
    m_impl->materials.clear();
    m_impl->textures.clear();
    m_impl->skins.clear();
    m_impl->skinAssets.clear();
    m_impl->cache.clear();
    m_impl->bytes = 0;
    return {};
  }
}  // namespace Dot::Graphics
