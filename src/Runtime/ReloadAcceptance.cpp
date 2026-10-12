#include <chrono>
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
import Dot.Core.Process;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.LiveScripts;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Runtime {
  auto testScriptReload(const std::filesystem::path& executable,
                        const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                        bool render) -> int {
    auto files = Core::FileSystem::open(root);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Live Reload");

    if (!files || !sdk || !compiler || !created
        || !files->createDirectories("reload-acceptance/dot")) {
      std::println(stderr, "Reload acceptance setup failed");
      return 1;
    }

    for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
      auto source = sdk->readText(path);

      if (!source
          || !files->writeTextAtomic(std::filesystem::path{"reload-acceptance"} / path, *source)) {
        return 1;
      }
    }

    const auto source = [](int factor) -> std::string {
      return "import {ScriptComponent} from './dot/engine.js';export class Actor extends ScriptComponent{"
             "#ticks=0;onStart(){const t=this.self.transform;t.position.x=0;this.self.transform=t;}"
             "async onUpdate(seconds:number){await Promise.resolve();++this.#ticks;const t=this.self.transform;"
             "t.position.x+=" + std::to_string(factor) + "*seconds;this.self.transform=t;this.self.name='tick'+this.#ticks;}"
             "onSaveState(){return {ticks:this.#ticks};}onLoadState(state:{ticks:number}){this.#ticks=state.ticks;}"
             "onStop(){this.self.name='stopped';}}";
    };
    auto scene = std::make_shared<World::Scene>(std::move(*created));
    auto camera = scene->createEntity("Camera");
    auto cube = scene->createEntity("Cube");
    World::Camera lens;
    lens.primary = true;
    World::MeshRenderer mesh;
    mesh.tint = {0.2F, 0.7F, 1, 1};

    if (!camera || !cube || !scene->setTransform(*camera, {{0, 0, 4}})
        || !scene->setCamera(*camera, lens) || !scene->setMeshRenderer(*cube, mesh)
        || !scene->setScript(*cube, World::ScriptComponent{"actor.ts", "Actor", "{}", true})
        || !files->writeTextAtomic("reload-acceptance/actor.ts", source(1))) {
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();

    {
      Script::LiveScriptsConfig config;
      config.directory = "reload-acceptance";
      config.pollInterval = std::chrono::milliseconds{0};
      auto live = Script::LiveScripts::create(scene, *files, *compiler, config);

      if (!live) {
        std::println(stderr, "{}", live.error().message);
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
          return 1;
        }

        graphics.emplace(std::move(*service));
        auto image = graphics->render(*scene, 64, 64);

        if (!image) {
          return 1;
        }

        before.emplace(std::move(*image));
      }

      for (int frame = 0; frame < 30; ++frame) {
        auto report = live->update(1.0 / 60);

        if (!report || report->source.error || !report->frame.failures.empty()
            || report->frame.realmFaulted) {
          return 1;
        }
      }

      if (!files->writeTextAtomic("reload-acceptance/actor.ts",
                                  "export const n:number='type failure';")) {
        return 1;
      }

      auto failed = live->update(0);
      auto state = scene->entity(*cube);

      if (!failed || !failed->source.error
          || failed->source.error->message.find("TS2322") == std::string::npos
          || !failed->frame.failures.empty() || failed->frame.realmFaulted || !state
          || state->name != "tick31" || std::abs(state->transform.position.x - 0.5F) > 0.00001F) {
        std::println(stderr, "Reload acceptance compile failure retention failed");
        return 1;
      }

      if (!files->writeTextAtomic("reload-acceptance/actor.ts", source(2))) {
        return 1;
      }

      auto reloaded = live->poll(true);

      if (!reloaded || !reloaded->committed || reloaded->error || !reloaded->reload
          || reloaded->reload->migrated != 1 || scene->entity(*cube)->name != "tick31") {
        std::println(stderr, "Reload acceptance generation/state migration failed");
        return 1;
      }

      for (int frame = 0; frame < 30; ++frame) {
        auto report = live->update(1.0 / 60);

        if (!report || report->source.error || !report->frame.failures.empty()
            || report->frame.realmFaulted) {
          return 1;
        }
      }

      state = scene->entity(*cube);

      if (!state || state->name != "tick61"
          || std::abs(state->transform.position.x - 1.5F) > 0.00001F || live->stats()->reloads != 1
          || live->stats()->compileAttempts != 3) {
        std::println(stderr, "Reload acceptance new behavior/private state failed");
        return 1;
      }

      if (render) {
        auto after = graphics->render(*scene, 64, 64);
        constexpr std::size_t center = (32 * 64 + 32) * 4;

        if (!after || before->pixels[center + 2] < 200 || after->pixels[center + 2] > 20
            || before->pixels == after->pixels) {
          std::println(stderr, "Reload acceptance actual GPU pixel change failed");
          return 1;
        }
      }

      auto stopped = live->stop();

      if (!stopped || !stopped->failures.empty() || scene->entity(*cube)->name != "stopped") {
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || after.bytes != memory.bytes
        || after.allocations != memory.allocations || Core::processResources().children != 0
        || Core::processResources().handles != 0) {
      std::println(stderr, "Reload acceptance resource cleanup failed");
      return 1;
    }

    std::println(
        "Reload acceptance passed: actual TypeScript polling, failed compile retention, "
        "new realm/private state migration, new behavior, cleanup{}",
        render ? ", real GPU pixels" : "");
    return 0;
  }
}  // namespace Dot::Runtime
