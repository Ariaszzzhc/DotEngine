module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Dot.Project.ScenePreparation;

import Dot.UI.Files;

import Dot.Core.Paths;
import Dot.Core.Text;
import Dot.World.SceneFiles;
import Dot.Assets.SceneSkins;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Assets.Image;
import Dot.Graphics.Geometry;
import Dot.Graphics.Image;
import Dot.Graphics.Environment;
import Dot.Graphics.Renderer;
import Dot.Animation.Files;
import Dot.Animation.ControllerFiles;
import Dot.Animation.ScenePlayers;

namespace Dot::Project {
  namespace {
    auto sceneFile(const Core::FileSystem& files, const std::string& name) -> Core::Result<void> {
      namespace Fs = std::filesystem;
      const Fs::path relative{std::u8string(name.begin(), name.end())};
      auto current = files.root();
      std::size_t depth{};
      std::size_t entries{};

      for (const auto& part : relative) {
        if (++depth > 32) {
          return Core::fail(Core::ErrorCode::Unavailable, "Scene path exceeds 32 components");
        }

        bool exact = false;
        std::error_code error;
        Fs::directory_iterator it(current, error);

        if (error) {
          return Core::fail(Core::ErrorCode::Io, "Read scene directory: " + error.message());
        }

        for (const auto& entry : it) {
          if (++entries > 65536) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene path enumeration exceeds 65536 entries");
          }

          if (entry.path().filename() == part) {
            exact = true;
            break;
          }
        }

        if (!exact) {
          return Core::fail(Core::ErrorCode::NotFound,
                            "Scene path is missing or has different spelling: " + name);
        }

        current /= part;
        const auto status = Fs::symlink_status(current, error);
        const bool final = current == files.root() / relative;

        if (error || Fs::is_symlink(status)
            || (final ? !Fs::is_regular_file(status) : !Fs::is_directory(status))) {
          return Core::fail(Core::ErrorCode::Io,
                            "Scene path is inaccessible, a symlink or special file: " + name);
        }
      }

      return {};
    }

    /** CPU-only asset preflight; GPU residency remains a later renderer operation.
     * Retain material/surface facts, release decoded pixels/vertices between files.
     * 仅 CPU 预检，GPU 驻留仍由后续渲染负责；保留材质/表面事实，逐文件释放解码数据。 */
    auto sceneGraphics(const Core::FileSystem& files, const World::Scene& scene,
                       std::stop_token stop) -> Core::Result<void> {
      auto entities = scene.entities();
      auto environment = scene.environment();

      if (!entities || !environment) {
        return std::unexpected(!entities ? entities.error() : environment.error());
      }

      std::unordered_set<std::string> seen;
      std::unordered_map<std::string, Assets::MaterialDescription> materials;
      std::unordered_map<std::string, Graphics::MeshSurface> surfaces;
      std::uint64_t bytes{};
      const auto account = [&](std::uint64_t size) -> Core::Result<void> {
        if (size > 512ULL * 1024 * 1024 - bytes) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene graphics exceed 512 MiB decoded preflight budget");
        }

        bytes += size;
        return {};
      };
      const auto unique
          = [&](const std::string& kind, const std::string& asset) -> Core::Result<bool> {
        if (seen.contains(kind + asset)) {
          return false;
        }

        if (seen.size() >= 32768) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene graphics exceed 32768 unique preflight assets");
        }

        auto checked = sceneFile(files, asset);

        if (!checked) {
          return std::unexpected(checked.error());
        }

        seen.insert(kind + asset);
        return true;
      };
      const auto path = [](const std::string& text) {
        return std::filesystem::path{std::u8string(text.begin(), text.end())};
      };

      for (const auto& entity : *entities) {
        if (stop.stop_requested()) {
          return Core::fail(Core::ErrorCode::Unavailable, "Scene graphics preparation cancelled");
        }

        if (entity.mesh && entity.mesh->kind == World::MeshKind::Asset) {
          auto first = unique("mesh:", entity.mesh->asset);

          if (!first) {
            return std::unexpected(first.error());
          }

          if (*first) {
            auto mesh = Assets::loadMesh(files, path(entity.mesh->asset));

            if (!mesh) {
              return std::unexpected(mesh.error());
            }

            auto surface = Graphics::meshSurface(mesh->data);

            if (!surface) {
              return std::unexpected(surface.error());
            }

            surfaces.emplace(entity.mesh->asset, *surface);
            auto counted = account(mesh->data.vertices.size() * sizeof(Graphics::Vertex)
                                   + mesh->data.indices.size() * sizeof(std::uint32_t));

            if (!counted) {
              return counted;
            }
          }
        }

        if (!entity.material) {
          continue;
        }

        auto first = unique("material:", entity.material->asset);

        if (!first) {
          return std::unexpected(first.error());
        }

        auto cached = materials.find(entity.material->asset);

        if (*first) {
          auto loaded = Assets::loadMaterial(files, path(entity.material->asset));

          if (!loaded) {
            return std::unexpected(loaded.error());
          }

          cached = materials.emplace(entity.material->asset, std::move(*loaded)).first;
        }

        const auto* material = &cached->second;

        if (entity.mesh && material->model == Graphics::SurfaceModel::MetallicRoughness) {
          const auto key = entity.mesh->kind == World::MeshKind::Cube   ? "builtin:cube"
                           : entity.mesh->kind == World::MeshKind::Quad ? "builtin:quad"
                                                                        : entity.mesh->asset;
          auto found = surfaces.find(key);

          if (found == surfaces.end()) {
            auto geometry
                = entity.mesh->kind == World::MeshKind::Cube ? Graphics::cube() : Graphics::quad();

            if (entity.mesh->kind == World::MeshKind::Asset) {
              auto mesh = Assets::loadMesh(files, path(entity.mesh->asset));

              if (!mesh) {
                return std::unexpected(mesh.error());
              }

              geometry = std::move(mesh->data);
            }

            if (!geometry) {
              return std::unexpected(geometry.error());
            }

            auto surface = Graphics::meshSurface(*geometry);

            if (!surface) {
              return std::unexpected(surface.error());
            }

            found = surfaces.emplace(key, *surface).first;
          }

          if (!found->second.hasNormals
              || (material->normalTexture && !found->second.hasNormalMapFrame)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Scene physical mesh lacks normals/normal-map frame: " + entity.name);
          }
        }

        for (const auto* texture :
             {&material->baseColorTexture, &material->metallicRoughnessTexture,
              &material->normalTexture, &material->emissiveTexture, &material->occlusionTexture}) {
          if (!*texture) {
            continue;
          }

          auto newImage = unique("image:", (*texture)->path);

          if (!newImage) {
            return std::unexpected(newImage.error());
          }

          if (*newImage) {
            if (stop.stop_requested()) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Scene graphics preparation cancelled");
            }

            auto image = Assets::loadImage(files, path((*texture)->path));

            if (!image) {
              return std::unexpected(image.error());
            }

            auto counted = account(image->pixels.size());

            if (!counted) {
              return counted;
            }
          }
        }
      }

      if (stop.stop_requested()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene graphics preparation cancelled");
      }

      if (*environment) {
        auto first = unique("hdr:", (**environment).asset);

        if (!first) {
          return std::unexpected(first.error());
        }

        auto image = Assets::loadHdrImage(files, path((**environment).asset));

        if (!image) {
          return std::unexpected(image.error());
        }

        auto valid = Graphics::validateEnvironment(*image, (**environment).processing);

        if (!valid) {
          return std::unexpected(valid.error());
        }

        return account(image->pixels.size() * sizeof(float));
      }

      return {};
    }

    /** Initial libraries share immutable decoded clips by exact authored path.
     * Account references, not only unique data, against the existing player
     * channel/key limits. Native scene bindings and writer checks stay on owner.
     * 初始库按精确编辑路径共享不可变解码片段；按引用而非仅唯一数据累计既有通道/键预算。
     * 原生场景绑定及写入权限校验仍在所有者线程执行。 */
    auto sceneAnimations(const Core::FileSystem& files, const World::Scene& scene,
                         std::stop_token stop) -> Core::Result<std::vector<Animation::ClipAsset>> {
      auto players = scene.animationPlayers();

      if (!players) {
        return std::unexpected(players.error());
      }

      std::vector<Animation::ClipAsset> clips;
      std::unordered_map<std::string, std::size_t> unique;
      std::size_t references{}, channels{}, keys{};

      for (const auto& player : *players) {
        if (!player.properties.enabled) {
          continue;
        }

        if (player.properties.clips.size() > 1024 - references) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Scene animation exceeds 1024 clip references");
        }

        references += player.properties.clips.size();

        for (const auto& path : player.properties.clips) {
          if (stop.stop_requested()) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene animation preparation cancelled");
          }

          auto found = unique.find(path);

          if (found == unique.end()) {
            auto checked = sceneFile(files, path);

            if (!checked) {
              return std::unexpected(checked.error());
            }

            auto loaded = Animation::loadClip(
                files, std::filesystem::path{std::u8string(path.begin(), path.end())});

            if (!loaded) {
              return std::unexpected(loaded.error());
            }

            const auto index = clips.size();
            clips.push_back({path, std::move(*loaded)});
            found = unique.emplace(path, index).first;
          }

          auto description = clips[found->second].value.description();

          if (!description) {
            return std::unexpected(description.error());
          }

          if ((*description)->tracks.size() > 30000 - channels) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Scene animation library channel budget exceeded");
          }

          channels += (*description)->tracks.size();

          for (const auto& track : (*description)->tracks) {
            if (track.keys.size() > 1000000 - keys) {
              return Core::fail(Core::ErrorCode::Unavailable,
                                "Scene animation library key budget exceeded");
            }

            keys += track.keys.size();
          }
        }
      }

      return clips;
    }
  }  // namespace

  struct PreparedScene::Data {
    std::filesystem::path root;
    std::string text;
    std::vector<Animation::ClipAsset> clips;
    std::vector<Animation::ControllerAsset> controllers;
    std::optional<UI::Asset> ui;
  };

  PreparedScene::PreparedScene(std::shared_ptr<const Data> data) noexcept
      : m_data(std::move(data)) {
  }

  PreparedScene::PreparedScene(const PreparedScene&) noexcept = default;

  auto PreparedScene::operator=(const PreparedScene&) noexcept -> PreparedScene& = default;

  PreparedScene::PreparedScene(PreparedScene&&) noexcept = default;

  auto PreparedScene::operator=(PreparedScene&&) noexcept -> PreparedScene& = default;

  PreparedScene::~PreparedScene() = default;

  auto PreparedScene::validate(const Core::FileSystem& files, const std::string& path,
                               const std::optional<std::string>& initial, std::stop_token stop)
      -> Core::Result<PreparedScene> {
    if (stop.stop_requested()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Scene preparation cancelled");
    }

    auto text = [&]() -> Core::Result<std::string> {
      if (initial) {
        return *initial;
      }

      auto checked = sceneFile(files, path);
      return checked
                 ? files.readText(std::filesystem::path{std::u8string(path.begin(), path.end())})
                 : std::unexpected(checked.error());
    }();
    auto scene = text ? World::decodeScene(*text)
                      : Core::Result<World::Scene>{std::unexpected(text.error())};

    if (!scene) {
      return std::unexpected(scene.error());
    }

    auto graphics = sceneGraphics(files, *scene, stop);

    if (!graphics) {
      return std::unexpected(graphics.error());
    }

    auto skins = Assets::validateSceneSkins(files, *scene);

    if (!skins) {
      return std::unexpected(skins.error());
    }

    auto animations = sceneAnimations(files, *scene, stop);

    if (!animations) {
      return std::unexpected(animations.error());
    }

    if (stop.stop_requested()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Scene preparation cancelled");
    }

    std::vector<Animation::ControllerAsset> controllers;
    std::set<std::string> uniqueControllers;
    auto players = scene->animationPlayers();

    if (!players) {
      return std::unexpected(players.error());
    }

    for (const auto& player : *players) {
      if (!player.properties.enabled || !player.properties.controller
          || !uniqueControllers.insert(*player.properties.controller).second) {
        continue;
      }

      if (stop.stop_requested()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Controller preparation cancelled");
      }

      const auto& controllerPath = *player.properties.controller;
      auto loaded = Animation::loadController(
          files,
          std::filesystem::path{std::u8string(controllerPath.begin(), controllerPath.end())});

      if (!loaded) {
        return std::unexpected(loaded.error());
      }

      controllers.push_back({controllerPath, std::move(*loaded)});
    }

    std::optional<UI::Asset> ui;
    auto binding = scene->ui();

    if (!binding) {
      return std::unexpected(binding.error());
    }

    if (*binding) {
      auto loaded = UI::loadLayout(
          files, std::filesystem::path{std::u8string((**binding).begin(), (**binding).end())});

      if (!loaded) {
        return std::unexpected(loaded.error());
      }

      ui = UI::Asset{**binding, std::move(*loaded)};
    }

    if (stop.stop_requested()) {
      return Core::fail(Core::ErrorCode::Unavailable, "UI preparation cancelled");
    }

    auto canonical = World::encodeScene(*scene);
    return canonical ? Core::Result<PreparedScene>{PreparedScene{std::make_shared<const Data>(
                           Data{files.root(), std::move(*canonical), std::move(*animations),
                                std::move(controllers), std::move(ui)})}}
                     : std::unexpected(canonical.error());
  }

  auto PreparedScene::scene() const -> Core::Result<World::Scene> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Prepared scene was moved");
    }

    return World::decodeScene(m_data->text);
  }

  auto PreparedScene::animationClips() const
      -> Core::Result<std::span<const Animation::ClipAsset>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Prepared scene was moved");
    }

    return std::span<const Animation::ClipAsset>{m_data->clips};
  }

  auto PreparedScene::animationControllers() const
      -> Core::Result<std::span<const Animation::ControllerAsset>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Prepared scene was moved");
    }

    return std::span<const Animation::ControllerAsset>{m_data->controllers};
  }

  auto PreparedScene::ui() const -> Core::Result<std::optional<UI::Asset>> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Prepared scene was moved");
    }

    return m_data->ui;
  }

  auto PreparedScene::accepts(const Core::FileSystem& files, const World::Scene& scene) const
      -> Core::Result<bool> {
    if (!m_data) {
      return Core::fail(Core::ErrorCode::InvalidState, "Prepared scene was moved");
    }

    if (files.root() != m_data->root) {
      return false;
    }

    auto encoded = World::encodeScene(scene);
    return encoded ? Core::Result<bool>{*encoded == m_data->text}
                   : std::unexpected(encoded.error());
  }

  struct ScenePreparation::Impl {
    struct Completion {
      std::mutex m_mutex;
      std::optional<Core::Result<PreparedScene>> m_result;
    };

    std::thread::id m_owner{std::this_thread::get_id()};
    std::shared_ptr<Completion> m_completion{std::make_shared<Completion>()};
    bool m_taken{};
    std::jthread m_worker;

    Impl(Core::FileSystem files, std::string path, std::optional<std::string> initial)
        : m_worker([state = m_completion, files = std::move(files), path = std::move(path),
                    initial = std::move(initial)](std::stop_token stop) {
            auto result = [&]() -> Core::Result<PreparedScene> {
              try {
                return PreparedScene::validate(files, path, initial, stop);
              } catch (const std::exception& error) {
                return Core::fail(Core::ErrorCode::External,
                                  "Background scene preparation: " + std::string(error.what()));
              }
            }();
            std::lock_guard lock(state->m_mutex);
            state->m_result.emplace(std::move(result));
          }) {
    }

    auto check() const -> Core::Result<void> {
      if (m_owner != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene preparation requires its creating thread");
      }

      return {};
    }
  };

  ScenePreparation::ScenePreparation(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  ScenePreparation::ScenePreparation(ScenePreparation&&) noexcept = default;

  auto ScenePreparation::operator=(ScenePreparation&&) noexcept -> ScenePreparation& = default;

  ScenePreparation::~ScenePreparation() = default;

  auto ScenePreparation::begin(const Core::FileSystem& files, std::string path,
                               std::optional<std::string> initial)
      -> Core::Result<ScenePreparation> {
    if (!Core::isPortableRelativePath(path) || !path.ends_with(".dotscene")
        || path.starts_with(".dot-stage-") || path.find("/.dot-stage-") != std::string::npos
        || (initial
            && (initial->empty() || initial->size() > 64 * 1024 * 1024
                || !Core::isValidUtf8(*initial)))) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid scene preparation path/initial bytes");
    }

    try {
      return ScenePreparation{std::make_unique<Impl>(files, std::move(path), std::move(initial))};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start scene preparation: " + std::string(error.what()));
    }
  }

  auto ScenePreparation::ready() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene preparation was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);
    return lock.owns_lock() && m_impl->m_completion->m_result.has_value();
  }

  auto ScenePreparation::take() -> Core::Result<PreparedScene> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene preparation was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_taken) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene preparation was already taken");
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);

    if (!lock.owns_lock() || !m_impl->m_completion->m_result) {
      return Core::fail(Core::ErrorCode::Unavailable, "Scene preparation is pending");
    }

    m_impl->m_taken = true;
    return std::move(*m_impl->m_completion->m_result);
  }

  auto ScenePreparation::requestStop() noexcept -> void {
    if (m_impl) {
      m_impl->m_worker.request_stop();
    }
  }
}  // namespace Dot::Project
