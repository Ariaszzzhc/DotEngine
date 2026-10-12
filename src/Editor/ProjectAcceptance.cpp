#include <cmath>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Script.InputSource;
import Dot.Platform.Input;
import Dot.Platform.Events;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Editor {
  auto testProject(const std::filesystem::path& executable, const std::filesystem::path& sdkRoot,
                   const std::filesystem::path& parentRoot, bool render) -> int {
    auto parent = Core::FileSystem::open(parentRoot);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);

    if (!parent || !sdk || !compiler) {
      std::println(stderr, "Project acceptance setup failed");
      return 1;
    }

    auto owned = Core::StagedDirectory::create(*parent);

    if (!owned) {
      std::println(stderr, "{}", owned.error().message);
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();

    {
      auto project = Project::Project::create(owned->files().value(), "template",
                                              Project::Description{"模板"}, *sdk);

      if (!project) {
        std::println(stderr, "{}", project.error().message);
        return 1;
      }

      auto loaded = project->loadStartup();
      auto program = compiler->compileProject(project->files().value(),
                                              project->description()->scriptsDirectory);
      auto input = Script::InputSource::create();

      if (!loaded || !program || !input) {
        std::println(stderr, "Project template scene/compiler/input failed");
        return 1;
      }

      auto scene = std::make_shared<World::Scene>(std::move(*loaded));
      auto entities = scene->entities();

      if (!entities || entities->size() != 2 || !(*entities)[1].script) {
        std::println(stderr, "Project template attached entity failed");
        return 1;
      }

      const auto cube = (*entities)[1].id;
      Platform::InputState native;
      native.consume({0, 0, Platform::KeyChanged{Platform::Key::D, true, false}});
      auto published = input->publish(native);

      if (!published) {
        std::println(stderr, "{}", published.error().message);
        return 1;
      }

      Script::SceneScriptsConfig config;
      config.services = input->bindings().value();
      auto scripts = Script::SceneScripts::create(scene, *program, config);

      if (!scripts) {
        std::println(stderr, "{}", scripts.error().message);
        return 1;
      }

      std::optional<Graphics::Renderer> renderer;
      std::optional<Graphics::SceneRenderer> graphics;
      std::optional<Graphics::Image> before;

      if (render) {
        auto device = Graphics::Renderer::create();

        if (!device) {
          std::println(stderr, "{}", device.error().message);
          return 1;
        }

        renderer.emplace(std::move(*device));
        auto service = Graphics::SceneRenderer::create(*renderer);

        if (!service) {
          std::println(stderr, "{}", service.error().message);
          return 1;
        }

        graphics.emplace(std::move(*service));
        auto image = graphics->render(*scene, 64, 64);

        if (!image) {
          std::println(stderr, "{}", image.error().message);
          return 1;
        }

        before.emplace(std::move(*image));
      }

      for (int frame = 0; frame < 2; ++frame) {
        auto report = scripts->update(0.5);

        if (!report || !report->failures.empty() || report->realmFaulted) {
          std::println(stderr, "Project template real TypeScript execution failed");
          return 1;
        }
      }

      if (std::abs(scene->entity(cube)->transform.position.x - 1.5F) > 0.00001F) {
        std::println(stderr, "Project template movement verification failed");
        return 1;
      }

      if (render) {
        auto after = graphics->render(*scene, 64, 64);
        constexpr std::size_t center = (32 * 64 + 32) * 4;

        if (!after || before->pixels[center + 2] < 200 || after->pixels[center + 2] > 20
            || before->pixels == after->pixels) {
          std::println(stderr, "Project template actual GPU pixels failed");
          return 1;
        }
      }

      auto stopped = scripts->stop();
      auto saved = project->saveStartup(*scene);
      auto description = project->description().value();
      description.name = "Saved Template";
      auto configured = project->save(description);

      if (!stopped || !stopped->failures.empty() || !saved || !configured) {
        std::println(stderr, "Project template stop/save failed");
        return 1;
      }

      const auto oldRoot = project->files()->root();
      const auto newRoot = oldRoot.parent_path() / "relocated";
      std::filesystem::rename(oldRoot, newRoot);
      auto reopened = Project::Project::open(newRoot);

      if (!reopened || reopened->description()->name != "Saved Template") {
        std::println(stderr, "Project template relocation failed");
        return 1;
      }

      auto restored = reopened->loadStartup();
      auto freshProgram
          = compiler->compileProject(reopened->files().value(), description.scriptsDirectory);

      if (!restored || restored->id() == scene->id() || !freshProgram || restored->entity(cube)
          || std::abs(restored->entities()->at(1).transform.position.x - 1.5F) > 0.00001F) {
        std::println(stderr, "Relocated project scene/compiler independence failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || after.bytes != memory.bytes
        || after.allocations != memory.allocations || Core::processResources().handles != 0
        || Core::processResources().children != 0) {
      std::println(stderr, "Project acceptance compiler/process/VM cleanup failed");
      return 1;
    }

    std::println(
        "Project acceptance passed: exclusive complete template publication, actual typed "
        "attached script/input, native scene/config save, relocation/recompile, cleanup{}",
        render ? ", actual GPU pixels" : "");
    return 0;
  }
}  // namespace Dot::Editor
