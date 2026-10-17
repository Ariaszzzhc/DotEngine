module;

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

module Dot.Project.GameSession;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Platform.Input;
import Dot.Script.AudioSession;
import Dot.Script.AnimationSession;
import Dot.Assets.SceneSkins;
import Dot.Assets.Mesh;
import Dot.Assets.Material;
import Dot.Assets.Image;
import Dot.Graphics.Geometry;
import Dot.Graphics.Image;
import Dot.Graphics.Environment;
import Dot.Graphics.Renderer;
import Dot.Script.SceneFlow;
import Dot.Script.GameData;

namespace Dot::Project {
  namespace {
    auto validate(const GameSessionConfig& config) -> Core::Result<void> {
      if (config.audioMode != SessionAudio::Offline && config.audioMode != SessionAudio::Playback) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid game session audio mode");
      }

      if (config.pollInterval.count() < 0 || config.pollInterval.count() > 60000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Game poll interval must be 0..60000 ms");
      }

      return {};
    }

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
    auto sceneGraphics(const Core::FileSystem& files, const World::Scene& scene)
        -> Core::Result<void> {
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

    auto append(Script::ScriptReport& target, Script::ScriptReport source) -> void {
      target.failures.insert(target.failures.end(),
                             std::make_move_iterator(source.failures.begin()),
                             std::make_move_iterator(source.failures.end()));
      target.created += source.created;
      target.removed += source.removed;
      target.calls += source.calls;
      target.realmFaulted = target.realmFaulted || source.realmFaulted;
    }

    struct ActiveScope {
      bool& active;

      explicit ActiveScope(bool& value) : active(value) {
        active = true;
      }

      ~ActiveScope() {
        active = false;
      }
    };
  }  // namespace

  struct GameSession::Origin {
    std::string path;
    std::string bytes;
  };

  struct GameSession::Impl {
    std::shared_ptr<World::Scene> m_scene;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_identity{};
    Description m_description;
    SessionState m_state{SessionState::Ready};
    SessionAudio m_audioMode{SessionAudio::Offline};
    bool m_active{};
    std::optional<Core::FileSystem> m_files;
    GameSessionConfig m_config;
    std::optional<Script::TypeScriptCompiler> m_compiler;
    std::optional<Script::CompiledProgram> m_program;
    std::optional<Script::SceneFlow> m_flow;
    std::optional<Script::GameData> m_progress;
    std::shared_ptr<const Origin> m_origin;
    std::optional<Script::InputSource> m_input;
    std::optional<Script::PhysicsSource> m_physics;
    std::optional<Script::AudioSession> m_audio;
    std::optional<Script::AnimationSession> m_animations;
    /** Script callbacks retain services; destroy the realm before its dependencies.
     * 脚本回调持有服务，先销毁 realm 再销毁依赖。 */
    std::optional<Script::SceneScripts> m_scripts;
    std::optional<Script::LiveScripts> m_live;

    auto retireServices() noexcept -> void {
      if (m_flow) {
        m_flow->retire();
      }

      if (m_progress) {
        m_progress->retire();
      }
    }

    auto check(bool allowTerminal = false) const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id() || m_active) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Game session requires owner thread without reentry");
      }

      if (!allowTerminal && (m_state == SessionState::Closed || m_state == SessionState::Faulted)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game session is closed or faulted");
      }

      if (!allowTerminal && m_scene->id() != m_identity) {
        return Core::fail(Core::ErrorCode::InvalidState, "Game session scene was moved/replaced");
      }

      return {};
    }

    auto scriptStats() const -> Core::Result<Script::SceneScriptsStats> {
      if (m_live) {
        return m_live->stats().transform([](const auto& stats) { return stats.scripts; });
      }

      return m_scripts->stats();
    }

    auto shutdown() -> Core::Result<Script::ScriptReport> {
      auto result = m_live ? m_live->stop() : m_scripts->stop();
      m_live.reset();
      m_scripts.reset();
      m_animations.reset();
      m_audio.reset();
      m_physics.reset();
      m_input.reset();
      m_program.reset();
      m_origin.reset();
      m_compiler.reset();
      m_files.reset();
      m_config.scripts.services.clear();
      m_state = SessionState::Closed;
      return result;
    }

    auto frame(double seconds, bool fixed) -> Core::Result<Script::LiveScriptReport> {
      if (m_state != SessionState::Running) {
        return Core::fail(Core::ErrorCode::InvalidState, "Start game session before dispatch");
      }

      ActiveScope active{m_active};

      try {
        auto result = m_live
                          ? (fixed ? m_live->fixedUpdate(seconds) : m_live->update(seconds))
                          : (fixed ? m_scripts->fixedUpdate(seconds) : m_scripts->update(seconds))
                                .transform([](auto report) {
                                  return Script::LiveScriptReport{{}, std::move(report)};
                                });

        if (!result || result->frame.realmFaulted) {
          m_state = SessionState::Faulted;
          retireServices();
          return result;
        }

        if (fixed) {
          auto advanced = m_physics->advance(seconds);

          if (!advanced) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(advanced.error());
          }
        }

        if (!fixed) {
          auto animated = m_animations->advance(seconds);

          if (!animated) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(animated.error());
          }

          auto synchronized = m_audio->synchronize(seconds);

          if (!synchronized) {
            m_state = SessionState::Faulted;
            retireServices();
            return std::unexpected(synchronized.error());
          }
        }

        return result;
      } catch (const std::exception& error) {
        m_state = SessionState::Faulted;
        retireServices();
        return Core::fail(Core::ErrorCode::External,
                          "Dispatch game session: " + std::string(error.what()));
      }
    }
  };

  GameSession::GameSession(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  GameSession::GameSession(GameSession&&) noexcept = default;

  auto GameSession::operator=(GameSession&& other) noexcept -> GameSession& {
    if (this != &other) {
      if (m_impl && m_impl->m_flow) {
        m_impl->retireServices();
      }

      m_impl = std::move(other.m_impl);
    }

    return *this;
  }

  GameSession::~GameSession() {
    if (m_impl && m_impl->m_flow) {
      m_impl->retireServices();
    }
  }

  auto GameSession::create(const Project& project, const Script::TypeScriptCompiler& compiler,
                           const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto loaded = project.loadStartup();
    auto description = project.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    return loaded ? prepare(project, compiler, std::move(*loaded), config, description->startScene)
                  : std::unexpected(loaded.error());
  }

  auto GameSession::createFromScene(const Project& project,
                                    const Script::TypeScriptCompiler& compiler,
                                    const World::Scene& scene, const GameSessionConfig& config,
                                    std::optional<std::string> scenePath)
      -> Core::Result<GameSession> {
    auto valid = validate(config);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto encoded = World::encodeScene(scene);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto cloned = World::decodeScene(*encoded);
    return cloned ? prepare(project, compiler, std::move(*cloned), config, std::move(scenePath),
                            std::move(*encoded))
                  : std::unexpected(cloned.error());
  }

  auto GameSession::prepare(const Project& project, const Script::TypeScriptCompiler& compiler,
                            World::Scene scene, const GameSessionConfig& config,
                            std::optional<std::string> scenePath,
                            std::optional<std::string> originBytes) -> Core::Result<GameSession> {
    try {
      auto description = project.description();
      auto files = project.files();

      if (!description) {
        return std::unexpected(description.error());
      }

      if (!files) {
        return std::unexpected(files.error());
      }

      std::shared_ptr<const Origin> origin;

      if (scenePath && originBytes) {
        origin = std::make_shared<const Origin>(Origin{*scenePath, std::move(*originBytes)});
      }

      return prepareSession(*files, std::move(*description), std::move(scene), config, &compiler,
                            nullptr, std::move(scenePath), nullptr, std::move(origin));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare project session: " + std::string(error.what()));
    }
  }

  auto GameSession::createCompiled(const Core::FileSystem& assets, const Description& description,
                                   const World::Scene& scene,
                                   const Script::CompiledProgram& program,
                                   const GameSessionConfig& config) -> Core::Result<GameSession> {
    auto checked = validate(config);
    auto metadata = validateDescription(description);
    auto modules = program.modules();

    if (!checked || !metadata || !modules) {
      return std::unexpected(!checked    ? checked.error()
                             : !metadata ? metadata.error()
                                         : modules.error());
    }

    if (description.hotReload) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Compiled sessions require hotReload=false");
    }

    auto encoded = World::encodeScene(scene);

    if (!encoded) {
      return std::unexpected(encoded.error());
    }

    auto cloned = World::decodeScene(*encoded);
    return cloned ? prepareSession(assets, description, std::move(*cloned), config, nullptr,
                                   &program, description.startScene)
                  : std::unexpected(cloned.error());
  }

  auto GameSession::prepareSession(
      const Core::FileSystem& assets, Description description, World::Scene scene,
      const GameSessionConfig& config, const Script::TypeScriptCompiler* compiler,
      const Script::CompiledProgram* precompiled, std::optional<std::string> scenePath,
      const Script::SceneFlow* sharedFlow, std::shared_ptr<const Origin> origin,
      const Script::GameData* sharedProgress) -> Core::Result<GameSession> {
    try {
      if (sharedFlow) {
        auto graphics = sceneGraphics(assets, scene);

        if (!graphics) {
          return std::unexpected(graphics.error());
        }
      }

      auto skins = Assets::validateSceneSkins(assets, scene);

      if (!skins) {
        return std::unexpected(skins.error());
      }

      auto impl = std::make_unique<Impl>();
      impl->m_description = std::move(description);
      impl->m_audioMode = config.audioMode;
      impl->m_scene = std::make_shared<World::Scene>(std::move(scene));
      impl->m_identity = impl->m_scene->id();
      impl->m_files = assets;
      impl->m_config = config;
      impl->m_origin = std::move(origin);

      if (compiler) {
        impl->m_compiler = *compiler;
      }

      if (precompiled) {
        impl->m_program = *precompiled;
      }

      auto progress = sharedProgress ? Core::Result<Script::GameData>{*sharedProgress}
                                     : Script::GameData::create(config.progress);

      if (!progress) {
        return std::unexpected(progress.error());
      }

      impl->m_progress.emplace(std::move(*progress));
      auto flow = sharedFlow ? Core::Result<Script::SceneFlow>{*sharedFlow}
                             : Script::SceneFlow::create(impl->m_identity, scenePath);

      if (!flow) {
        return std::unexpected(flow.error());
      }

      impl->m_flow.emplace(std::move(*flow));
      auto input = Script::InputSource::create(config.input);

      if (!input) {
        return std::unexpected(input.error());
      }

      impl->m_input.emplace(std::move(*input));
      auto published = impl->m_input->publish(Platform::InputState{});

      if (!published) {
        return std::unexpected(published.error());
      }

      auto physics = Script::PhysicsSource::create(impl->m_scene, config.physics);

      if (!physics) {
        return std::unexpected(physics.error());
      }

      impl->m_physics.emplace(std::move(*physics));
      auto playbackConfig = config.audio;
      playbackConfig.deferredStart = true;
      auto audio = config.audioMode == SessionAudio::Playback
                       ? Script::AudioSession::createPlayback(impl->m_scene, assets, playbackConfig,
                                                              config.audioLimits)
                       : Script::AudioSession::create(impl->m_scene, assets, config.audio.mixer,
                                                      config.audioLimits);

      if (!audio) {
        return std::unexpected(audio.error());
      }

      impl->m_audio.emplace(std::move(*audio));
      auto animations = Script::AnimationSession::create(impl->m_scene, assets);

      if (!animations) {
        return std::unexpected(animations.error());
      }

      impl->m_animations.emplace(std::move(*animations));
      auto scriptsConfig = config.scripts;

      for (auto bindings :
           {impl->m_input->bindings(), impl->m_physics->bindings(), impl->m_audio->bindings(),
            impl->m_animations->bindings(), impl->m_progress->bindings(),
            impl->m_flow->bindings(impl->m_identity, scenePath)}) {
        if (!bindings) {
          return std::unexpected(bindings.error());
        }

        scriptsConfig.services.insert(scriptsConfig.services.end(),
                                      std::make_move_iterator(bindings->begin()),
                                      std::make_move_iterator(bindings->end()));
      }

      if (impl->m_description.hotReload) {
        Script::LiveScriptsConfig liveConfig{
            std::move(scriptsConfig), impl->m_description.scriptsDirectory, config.pollInterval};
        auto scripts = Script::LiveScripts::create(impl->m_scene, assets, *compiler, liveConfig);

        if (!scripts) {
          return std::unexpected(scripts.error());
        }

        impl->m_live.emplace(std::move(*scripts));
      } else {
        auto program = precompiled
                           ? Core::Result<Script::CompiledProgram>{*precompiled}
                           : compiler->compileProject(assets, impl->m_description.scriptsDirectory);

        if (!program) {
          return std::unexpected(program.error());
        }

        impl->m_program = *program;
        auto scripts = Script::SceneScripts::create(impl->m_scene, *program, scriptsConfig);

        if (!scripts) {
          return std::unexpected(scripts.error());
        }

        auto prepared = scripts->sync();

        if (!prepared) {
          return std::unexpected(prepared.error());
        }

        if (prepared->realmFaulted) {
          return Core::fail(Core::ErrorCode::InvalidState, "Prepare script realm faulted");
        }

        if (!prepared->failures.empty()) {
          return std::unexpected(prepared->failures.front().error);
        }

        impl->m_scripts.emplace(std::move(*scripts));
      }

      return GameSession(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create game session: " + std::string(error.what()));
    }
  }

  auto GameSession::scene() const -> Core::Result<std::shared_ptr<World::Scene>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_scene->id() != m_impl->m_identity) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session scene was moved/replaced");
    }

    return m_impl->m_scene;
  }

  auto GameSession::stats() const -> Core::Result<GameSessionStats> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    GameSessionStats result{m_impl->m_state,
                            m_impl->m_description.fixedHz,
                            m_impl->m_description.hotReload,
                            {},
                            {},
                            {}};
    auto location = m_impl->m_flow->location();
    auto latest = m_impl->m_flow->latest();

    if (!location || !latest) {
      return std::unexpected(!location ? location.error() : latest.error());
    }

    result.location = std::move(*location);
    result.transition = std::move(*latest);

    if (m_impl->m_state != SessionState::Closed) {
      auto scripts = m_impl->scriptStats();

      if (!scripts) {
        return std::unexpected(scripts.error());
      }

      result.scripts = *scripts;
    }

    return result;
  }

  auto GameSession::gameData() const -> Core::Result<Script::GameData> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);
    return checked ? Core::Result<Script::GameData>{*m_impl->m_progress}
                   : Core::Result<Script::GameData>{std::unexpected(checked.error())};
  }

  auto GameSession::requestScene(std::string_view path) -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state != SessionState::Running) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene requests require a running session");
    }

    return m_impl->m_flow->request(path, m_impl->m_identity);
  }

  auto GameSession::sceneRequest(std::uint64_t id) const -> Core::Result<Script::SceneRequest> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);
    return checked ? m_impl->m_flow->status(id)
                   : Core::Result<Script::SceneRequest>{std::unexpected(checked.error())};
  }

  auto GameSession::processSceneRequest(Script::ScriptReport& report) -> Core::Result<void> {
    auto pending = m_impl->m_flow->pending();

    if (!pending) {
      return std::unexpected(pending.error());
    }

    if (!*pending) {
      return {};
    }

    auto request = std::move(**pending);
    auto begun = m_impl->m_flow->begin(request.id);

    if (!begun) {
      return begun;
    }

    bool committed = false;
    bool published = false;
    std::unique_ptr<Impl> replacement;
    const auto failed = [&](Core::Error error) -> Core::Result<void> {
      if (committed && !published) {
        m_impl.swap(replacement);
        published = true;
      }

      if (committed) {
        replacement.reset();
        m_impl->m_state = SessionState::Faulted;
        error.message = "Scene transition committed; target startup failed: " + error.message;
      } else {
        error.message = "Scene transition preparation failed; old scene retained: " + error.message;
      }

      auto recorded = m_impl->m_flow->fail(request.id, error);

      if (!recorded) {
        m_impl->m_state = SessionState::Faulted;
        m_impl->retireServices();
        return recorded;
      }

      if (committed) {
        m_impl->retireServices();
        return std::unexpected(std::move(error));
      }

      return {};
    };

    try {
      {
        ActiveScope active{m_impl->m_active};
        auto loaded = [&]() -> Core::Result<World::Scene> {
          if (m_impl->m_origin && m_impl->m_origin->path == request.path) {
            return World::decodeScene(m_impl->m_origin->bytes);
          }

          auto checked = sceneFile(*m_impl->m_files, request.path);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          auto path
              = std::filesystem::path{std::u8string(request.path.begin(), request.path.end())};
          return World::loadScene(*m_impl->m_files, path);
        }();

        if (!loaded) {
          return failed(loaded.error());
        }

        auto prepared
            = prepareSession(*m_impl->m_files, m_impl->m_description, std::move(*loaded),
                             m_impl->m_config, m_impl->m_compiler ? &*m_impl->m_compiler : nullptr,
                             m_impl->m_program ? &*m_impl->m_program : nullptr, request.path,
                             &*m_impl->m_flow, m_impl->m_origin, &*m_impl->m_progress);

        if (!prepared) {
          return failed(prepared.error());
        }

        replacement = std::move(prepared->m_impl);
      }

      /** Stop hooks cannot be rolled back. Prepare all services/path ownership
       * before acceptance; retire old writers and allow the next Start to queue.
       * Stop 钩子不可回滚；先准备全部服务及路径所有权，再接受替换，使旧写入失效并允许
       * 新 Start 排队下一请求。 */
      auto accepted
          = m_impl->m_flow->commit(request.id, replacement->m_identity, std::move(request.path));

      if (!accepted) {
        return failed(accepted.error());
      }

      committed = true;
      Core::Result<Script::ScriptReport> stopped;

      {
        ActiveScope active{m_impl->m_active};
        stopped = m_impl->shutdown();
      }

      m_impl.swap(replacement);
      published = true;
      replacement.reset();

      if (!stopped) {
        return failed(stopped.error());
      }

      append(report, std::move(*stopped));

      if (report.realmFaulted) {
        if (!report.failures.empty()) {
          auto error = report.failures.back().error;
          error.message = "Retired scene Stop: " + error.message;
          return failed(std::move(error));
        }

        return failed(Core::Error{Core::ErrorCode::InvalidState,
                                  "Retired script realm faulted during Stop",
                                  std::source_location::current()});
      }

      auto started = start();

      if (!started) {
        return failed(started.error());
      }

      append(report, std::move(*started));

      if (report.realmFaulted) {
        if (!report.failures.empty()) {
          auto error = report.failures.back().error;
          error.message = "Target scene Start: " + error.message;
          return failed(std::move(error));
        }

        return failed(Core::Error{Core::ErrorCode::InvalidState,
                                  "Target script realm faulted during Start",
                                  std::source_location::current()});
      }

      return {};
    } catch (const std::exception& error) {
      return failed(Core::Error{Core::ErrorCode::External,
                                "Replace game scene: " + std::string(error.what()),
                                std::source_location::current()});
    }
  }

  auto GameSession::publish(const Platform::InputState& input, bool pointerCaptured)
      -> Core::Result<std::uint64_t> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->m_input->publish(input, pointerCaptured);
  }

  auto GameSession::pointerCaptureRequested() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();
    return checked ? m_impl->m_input->pointerCaptureRequested()
                   : Core::Result<bool>{std::unexpected(checked.error())};
  }

  auto GameSession::start() -> Core::Result<Script::ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    ActiveScope active{m_impl->m_active};
    const bool starting = m_impl->m_state == SessionState::Ready;
    auto result = m_impl->m_live ? m_impl->m_live->start() : m_impl->m_scripts->start();
    m_impl->m_state
        = !result || result->realmFaulted ? SessionState::Faulted : SessionState::Running;

    if (m_impl->m_state == SessionState::Faulted) {
      m_impl->retireServices();
    }

    if (m_impl->m_state == SessionState::Running) {
      auto animated = m_impl->m_animations->advance(0);

      if (!animated) {
        m_impl->m_state = SessionState::Faulted;
        m_impl->retireServices();
        return std::unexpected(animated.error());
      }

      if (starting) {
        /** Start can place listeners/sources; establish their first motion sample.
         * Start 可定位听者/声源；零时长同步建立首次运动采样，不将初始化当作瞬间移动。 */
        auto synchronized = m_impl->m_audio->synchronize(0);

        if (!synchronized) {
          m_impl->m_state = SessionState::Faulted;
          m_impl->retireServices();
          return std::unexpected(synchronized.error());
        }

        auto activated = m_impl->m_audio->startPlayback();

        if (!activated) {
          m_impl->m_state = SessionState::Faulted;
          m_impl->retireServices();
          return std::unexpected(activated.error());
        }
      }
    }

    return result;
  }

  auto GameSession::fixedUpdate() -> Core::Result<Script::LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    return m_impl->frame(1.0 / m_impl->m_description.fixedHz, true);
  }

  auto GameSession::update(double seconds) -> Core::Result<Script::LiveScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Game frame seconds must be finite [0,1]");
    }

    auto result = m_impl->frame(seconds, false);

    if (!result || result->frame.realmFaulted) {
      if (m_impl->m_state == SessionState::Faulted) {
        m_impl->retireServices();
      }

      return result;
    }

    auto transitioned = processSceneRequest(result->frame);
    return transitioned
               ? std::move(result)
               : Core::Result<Script::LiveScriptReport>{std::unexpected(transitioned.error())};
  }

  auto GameSession::renderAudio(std::span<float> samples) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    if (m_impl->m_audioMode == SessionAudio::Playback) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Project playback cannot render offline PCM");
    }

    auto result = m_impl->m_audio->render(samples);

    if (!result && result.error().code != Core::ErrorCode::InvalidArgument) {
      m_impl->m_state = SessionState::Faulted;
      m_impl->retireServices();
    }

    return result;
  }

  auto GameSession::close() -> Core::Result<Script::ScriptReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Game session was moved");
    }

    auto checked = m_impl->check(true);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_state == SessionState::Closed) {
      return Script::ScriptReport{};
    }

    ActiveScope active{m_impl->m_active};
    m_impl->m_flow->retire();
    auto stopped = m_impl->shutdown();
    m_impl->m_progress->retire();
    return stopped;
  }
}  // namespace Dot::Project
