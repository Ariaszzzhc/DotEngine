module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
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

    struct MaterialEntry {
      Material material;
      std::vector<std::string> textures;
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
    std::optional<MaterialPreview> preview;

    struct EnvironmentEntry {
      std::string asset;
      EnvironmentConfig processing;
      EnvironmentMap map;
      std::uint64_t bytes;
    };

    std::optional<EnvironmentEntry> environment;
    std::optional<std::uint64_t> occlusionRevision;

    /** Detach only our binding; another scene renderer may now own the active map.
     * 仅解绑本服务的资源；另一个场景渲染器可能已接管当前环境。 */
    auto detachEnvironment() -> Core::Result<void> {
      if (!environment) {
        return {};
      }

      auto current = renderer->environment();

      if (!current) {
        return std::unexpected(current.error());
      }

      if (*current && (**current).map == environment->map) {
        return renderer->setEnvironment(std::nullopt);
      }

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

    auto resolveMaterial(const Assets::MaterialDescription& value) -> Core::Result<MaterialEntry> {
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
      std::unordered_map<std::string, TextureEntry> pending;
      std::uint64_t pendingBytes{};

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

        std::unordered_set<std::string> needed;
        std::unordered_set<std::string> neededMaterials;
        std::size_t drawCount = 0;

        for (const auto& entity : *entities) {
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

        pruneSkins(scene, *entities, true);

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

        for (auto iterator = textures.begin(); iterator != textures.end();) {
          if (!neededTextures.contains(iterator->first)) {
            bytes -= iterator->second.bytes;
            iterator = textures.erase(iterator);
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

  auto SceneRenderer::render(const World::Scene& scene, std::uint32_t width, std::uint32_t height,
                             Math::Vec4 clear, SceneView camera) -> Core::Result<Image> {
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

  auto SceneRenderer::present(const World::Scene& scene, Math::Vec4 clear, SceneView camera)
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
                                 SceneView camera) -> Core::Result<void> {
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

    checked = m_impl->detachEnvironment();

    if (!checked) {
      return checked;
    }

    checked = m_impl->detachOcclusion();

    if (!checked) {
      return checked;
    }

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
