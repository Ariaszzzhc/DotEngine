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
import Dot.World.SceneFiles;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Runtime {
  auto testScriptComponents(const std::filesystem::path& executable,
                            const std::filesystem::path& root, bool render) -> int {
    auto files = Core::FileSystem::open(root);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto scene = World::Scene::create("Saved Script Components");

    if (!files || !compiler || !scene || !files->createDirectories("component-acceptance")) {
      std::println(stderr, "Component acceptance setup failed");
      return 1;
    }

    auto source = files->writeTextAtomic(
        "component-acceptance/actor.ts",
        "import {ScriptComponent} from 'dot:engine';"
        "export class Actor extends ScriptComponent<{speed:number}>{#ticks=0;"
        "onStart(){this.self.name+=' started';}"
        "async onUpdate(seconds:number){await Promise.resolve();++this.#ticks;const "
        "t=this.self.transform;"
        "t.position.x+=this.properties.speed*seconds;this.self.transform=t;}"
        "onFixedUpdate(seconds:number){const "
        "t=this.self.transform;t.position.y+=seconds;this.self.transform=t;}"
        "onStop(){if(this.#ticks!==60)throw Error('unexpected tick count');this.self.name+=' "
        "stopped';}}");
    auto camera = scene->createEntity("Camera");
    auto a = scene->createEntity("Blue");
    auto b = scene->createEntity("Red");

    if (!source || !camera || !a || !b) {
      std::println(stderr, "Component acceptance source/entities failed");
      return 1;
    }

    World::Camera lens;
    lens.primary = true;
    World::MeshRenderer blue;
    blue.tint = {0.2F, 0.7F, 1, 1};
    World::MeshRenderer red;
    red.tint = {1, 0.1F, 0.1F, 1};

    if (!scene->setTransform(*camera, {{0, 0, 4}}) || !scene->setCamera(*camera, lens)
        || !scene->setMeshRenderer(*a, blue) || !scene->setMeshRenderer(*b, red)
        || !scene->setTransform(*b, {{-1.5F, 0, 0}})
        || !scene->setScript(*a,
                             World::ScriptComponent{"actor.ts", "Actor", "{\"speed\":1.5}", true})
        || !scene->setScript(*b,
                             World::ScriptComponent{"actor.ts", "Actor", "{\"speed\":-1}", true})
        || !World::saveScene(*files, "component-acceptance/main.dotscene", *scene)) {
      std::println(stderr, "Component acceptance authoring/save failed");
      return 1;
    }

    auto loaded = World::loadScene(*files, "component-acceptance/main.dotscene");

    if (!loaded || loaded->id() == scene->id() || loaded->entity(*a)) {
      std::println(stderr, "Component acceptance persisted scene/scope failed");
      return 1;
    }

    auto live = std::make_shared<World::Scene>(std::move(*loaded));
    auto first = live->findEntity(a->value);
    auto second = live->findEntity(b->value);

    if (!first || !second || !live->entity(*first)->script || !live->entity(*second)->script) {
      std::println(stderr, "Component acceptance loaded descriptors failed");
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();
    std::filesystem::path output;

    {
      auto program = compiler->compileProject(*files, "component-acceptance");

      if (!program) {
        std::println(stderr, "{}", program.error().message);
        return 1;
      }

      output = program->files()->root().parent_path();
      auto scripts = Script::SceneScripts::create(live, *program);

      if (!scripts) {
        std::println(stderr, "{}", scripts.error().message);
        return 1;
      }

      auto started = scripts->start();

      if (!started || !started->failures.empty() || started->realmFaulted
          || scripts->stats()->running != 2) {
        std::println(stderr, "Component acceptance automatic start failed");
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
        auto image = graphics->render(*live, 64, 64);

        if (!image) {
          std::println(stderr, "{}", image.error().message);
          return 1;
        }

        before.emplace(std::move(*image));
      }

      for (int frame = 0; frame < 60; ++frame) {
        auto fixed = scripts->fixedUpdate(1.0 / 120);
        auto updated = scripts->update(1.0 / 60);

        if (!fixed || !updated || !fixed->failures.empty() || !updated->failures.empty()
            || fixed->realmFaulted || updated->realmFaulted) {
          std::println(stderr, "Component acceptance lifecycle update failed");
          return 1;
        }
      }

      auto stateA = live->entity(*first);
      auto stateB = live->entity(*second);

      if (!stateA || !stateB || std::abs(stateA->transform.position.x - 1.5F) > 0.00001F
          || std::abs(stateB->transform.position.x + 2.5F) > 0.00001F
          || std::abs(stateA->transform.position.y - 0.5F) > 0.00001F) {
        std::println(stderr, "Component acceptance native transform verification failed");
        return 1;
      }

      if (render) {
        auto after = graphics->render(*live, 64, 64);
        constexpr std::size_t center = (32 * 64 + 32) * 4;

        if (!after || before->pixels[center + 2] < 200 || after->pixels[center + 2] > 20
            || before->pixels == after->pixels || graphics->stats()->meshes != 1) {
          std::println(stderr, "Component acceptance actual GPU pixel change failed");
          return 1;
        }
      }

      auto stopped = scripts->stop();

      if (!stopped || !stopped->failures.empty() || scripts->stats()->runtime.instances != 0
          || live->entity(*first)->name != "Blue started stopped"
          || live->entity(*second)->name != "Red started stopped") {
        std::println(stderr, "Component acceptance automatic stop failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || std::filesystem::exists(output)
        || after.bytes != memory.bytes || after.allocations != memory.allocations
        || Core::processResources().handles != 0 || Core::processResources().children != 0) {
      std::println(stderr, "Component acceptance resource cleanup failed");
      return 1;
    }

    std::println(
        "Component acceptance passed: saved/loaded authoring, typed classes, "
        "automatic start/update/fixed/stop, independent entities, cleanup{}",
        render ? ", actual GPU pixels" : "");
    return 0;
  }
}  // namespace Dot::Runtime
