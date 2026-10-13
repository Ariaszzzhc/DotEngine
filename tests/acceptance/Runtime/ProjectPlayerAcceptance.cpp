#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Project.GamePackage;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Audio.Mixer;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.World.Scene;

#include "../../../src/Runtime/Player.hpp"

namespace Dot::Runtime {
  namespace {
    auto options(std::size_t frames) -> PlayerOptions {
      PlayerOptions options;
      options.frames = frames;
      options.pauseOnFocusLoss = false;
      options.volume = 0;
      options.driver = [frames](Core::Application& application, Project::GameSession& session,
                                Graphics::SceneRenderer& renderer, Core::LoopCallbacks& callbacks,
                                const std::size_t& presentations) -> Core::Result<void> {
        auto scene = session.scene();

        if (!scene) {
          return std::unexpected(scene.error());
        }

        const auto publish = callbacks.prepareFrame;
        callbacks.prepareFrame
            = [&application, publish](const Platform::InputState& input) -> Core::Result<void> {
          if (input.key(Platform::Key::Escape).pressed) {
            application.stop();
          }

          Platform::InputState driven;
          static_cast<void>(
              driven.consume({0, 0, Platform::KeyChanged{Platform::Key::D, true, false}}));
          return publish(driven);
        };
        auto before = renderer.render(**scene, 96, 96);

        if (!before) {
          return std::unexpected(before.error());
        }

        for (std::size_t frame = 0; frame < frames; ++frame) {
          auto advanced = application.step(std::chrono::milliseconds{16}, callbacks);

          if (!advanced) {
            return std::unexpected(advanced.error());
          }
        }

        auto after = renderer.render(**scene, 96, 96);
        auto entities = (*scene)->entities();

        if (!after || !entities) {
          return std::unexpected(!after ? after.error() : entities.error());
        }

        auto cube = std::ranges::find(*entities, "Cube", &World::EntityInfo::name);

        if (cube == entities->end() || std::abs(cube->transform.position.x - 0.72F) > 0.001F
            || before->pixels == after->pixels || presentations == 0) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Project player acceptance did not move/present the actual template");
        }

        return {};
      };
      return options;
    }
  }  // namespace

  auto verifyGame(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root);
    auto game = files ? Project::GamePackage::open(*files)
                      : Core::Result<Project::GamePackage>{std::unexpected(files.error())};

    if (!game) {
      std::println(stderr, "{}", game.error().message);
      return 1;
    }

    auto result = runGame(*game, options(30));

    if (!result) {
      std::println(stderr, "{}", result.error().message);
      return 1;
    }

    std::println("Standalone game acceptance passed: relocated native input/GPU/audio");
    return 0;
  }

  auto testGamePlayer(const std::filesystem::path& executable, const std::filesystem::path& sdkRoot,
                      const std::filesystem::path& parentRoot) -> int {
    auto parent = Core::FileSystem::open(parentRoot).value();
    auto storage = Core::StagedDirectory::create(parent).value();
    auto files = storage.files().value();
    auto sdk = Core::FileSystem::open(sdkRoot).value();
    auto project = Project::Project::create(files, "authoring", {}, sdk).value();
    auto compiler = Script::TypeScriptCompiler::create(executable).value();
    auto scene = project.loadStartup().value();
    auto published = Project::GamePackage::publish(project, scene, compiler, files, "game");

    if (!published) {
      std::println(stderr, "{}", published.error().message);
      return 1;
    }

    std::filesystem::rename(published->root(), files.root() / "relocated");
    std::filesystem::remove_all(project.files()->root());
    const auto generations = Script::compilerGenerations();
    const auto vm = Script::Runtime::backendMemory();
    const auto audio = Audio::Mixer::backendMemory();
    auto run = [&]() -> Core::Result<void> {
      auto source = Core::FileSystem::open(files.root() / "relocated").value();
      auto game = Project::GamePackage::open(source);

      if (!game) {
        return std::unexpected(game.error());
      }

      return runGame(*game, options(30));
    }();

    if (!run) {
      std::println(stderr, "{}", run.error().message);
      return 1;
    }

    if (Script::compilerGenerations() != generations
        || Script::Runtime::backendMemory().bytes != vm.bytes
        || Audio::Mixer::backendMemory().bytes != audio.bytes) {
      std::println(stderr, "Exported game retained program/VM/audio ownership");
      return 1;
    }

    std::println(
        "Exported game acceptance passed: relocated package, no authoring/compiler at playback, "
        "native input/GPU/audio and shutdown");
    return 0;
  }

  auto testProjectPlayer(const std::filesystem::path& executable,
                         const std::filesystem::path& sdkRoot,
                         const std::filesystem::path& parentRoot) -> int {
    auto parent = Core::FileSystem::open(parentRoot);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);

    if (!parent || !sdk || !compiler) {
      std::println(stderr, "Project player acceptance setup failed");
      return 1;
    }

    auto staging = Core::StagedDirectory::create(*parent);

    if (!staging) {
      std::println(stderr, "{}", staging.error().message);
      return 1;
    }

    auto project = Project::Project::create(
        staging->files().value(), "game",
        Project::Description{"DotEngine Project Test", "scenes/main.dotscene", "scripts", 320, 240},
        *sdk);

    if (!project) {
      std::println(stderr, "{}", project.error().message);
      return 1;
    }

    const auto generations = Script::compilerGenerations();
    const auto vm = Script::Runtime::backendMemory();
    const auto audio = Audio::Mixer::backendMemory();
    for (const bool throws : {false, true}) {
      auto failureOptions = options(0);
      failureOptions.driver
          = [throws](Core::Application&, Project::GameSession&, Graphics::SceneRenderer&,
                     Core::LoopCallbacks&, const std::size_t&) -> Core::Result<void> {
        if (throws) {
          throw std::runtime_error("expected driver failure");
        }

        return Core::fail(Core::ErrorCode::Io, "expected driver failure");
      };
      auto failed = runProject(*project, *compiler, failureOptions);

      if (failed
          || failed.error().code != (throws ? Core::ErrorCode::External : Core::ErrorCode::Io)
          || failed.error().message.find("expected driver failure") == std::string::npos
          || Script::compilerGenerations() != generations
          || Script::Runtime::backendMemory().bytes != vm.bytes
          || Audio::Mixer::backendMemory().bytes != audio.bytes) {
        std::println(stderr, "Player driver failure lost context or retained resources");
        return 1;
      }
    }

    auto played = runProject(*project, *compiler, options(30));

    if (!played) {
      std::println(stderr, "{}", played.error().message);
      return 1;
    }

    if (Script::compilerGenerations() != generations
        || Script::Runtime::backendMemory().bytes != vm.bytes
        || Audio::Mixer::backendMemory().bytes != audio.bytes) {
      std::println(stderr, "Project player retained compiler/VM/audio resources");
      return 1;
    }

    std::println(
        "Project player acceptance passed: published input, attached TypeScript, native "
        "window/GPU presentation and pixel changes, real audio endpoint, explicit shutdown");
    return 0;
  }
}  // namespace Dot::Runtime
