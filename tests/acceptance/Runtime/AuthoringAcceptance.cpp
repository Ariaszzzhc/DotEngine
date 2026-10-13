#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneHost;
import Dot.Script.SceneScripts;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Runtime {
  auto testScriptAuthoring(const std::filesystem::path& executable,
                           const std::filesystem::path& root, bool render) -> int {
    auto files = Core::FileSystem::open(root);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Component Authoring");

    if (!files || !compiler || !created
        || !files->createDirectories("script-authoring-acceptance")) {
      std::println(stderr, "Script authoring acceptance setup failed");
      return 1;
    }

    auto source = files->writeTextAtomic("script-authoring-acceptance/main.ts", R"ts(
import {components, scene, ScriptComponent} from 'dot:engine';
import type {HostError} from 'dot:engine';

export function author(): void {
  const camera = scene.create('Camera');
  const t = camera.transform;
  t.position.z = 4;
  camera.transform = t;
  camera.camera = components.camera({primary: true});
  camera.audioListener = components.audioListener(true);
  const actor = scene.create('Actor');
  actor.mesh = components.mesh({tint: {x: 0, y: 0, z: 1, w: 1}});
  actor.rigidBody = components.rigidBody({motion: 'kinematic', shape: 'box'});
  actor.audioSource = components.audioSource('audio/missing.wav');
  actor.script = components.script('main.ts', 'Actor');
  const before = actor.rigidBody!;
  before.mass = 0;
  let rejected = false;

  try {
    actor.rigidBody = before;
  } catch (error) {
    rejected = (error as HostError).code === 'InvalidArgument';
  }

  if (!rejected || actor.rigidBody!.mass !== 1 || !actor.exists
      || scene.primaryCamera?.id.value !== camera.id.value
      || scene.primaryListener?.id.value !== camera.id.value) {
    throw Error('authoring contract');
  }
}

export class Actor extends ScriptComponent {
  #ticks = 0;

  onUpdate(): void {
    const mesh = this.self.mesh!;
    mesh.tint = {x: 0, y: 1, z: 0, w: 1};
    mesh.visible = ++this.#ticks === 1;
    this.self.mesh = mesh;
    const body = this.self.rigidBody!;
    body.mass = 2;
    this.self.rigidBody = body;
    const audio = this.self.audioSource!;
    audio.voice.pitch = 2;
    this.self.audioSource = audio;
  }

  onStop(): void {
    this.self.mesh = null;
    this.self.rigidBody = null;
    this.self.audioSource = null;
  }
}
)ts");

    if (!source) {
      std::println(stderr, "{}", source.error().message);
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();
    std::filesystem::path output;

    {
      auto program = compiler->compileProject(*files, "script-authoring-acceptance");

      if (!program) {
        std::println(stderr, "{}", program.error().message);
        return 1;
      }

      output = program->files()->root().parent_path();
      auto scene = std::make_shared<World::Scene>(std::move(*created));

      {
        auto bindings = Script::sceneBindings(scene);
        auto vm = bindings ? Script::Runtime::create(program->files().value(), {}, *bindings)
                           : Core::Result<Script::Runtime>{std::unexpected(bindings.error())};

        if (!vm) {
          std::println(stderr, "{}", vm.error().message);
          return 1;
        }

        auto entry = vm->loadModule("main.js");
        auto authored
            = entry ? vm->call(*entry, "author")
                    : Core::Result<std::optional<std::string>>{std::unexpected(entry.error())};

        if (!authored) {
          std::println(stderr, "{}", authored.error().message);
          return 1;
        }
      }

      auto saved = World::saveScene(*files, "script-authoring-acceptance/main.dotscene", *scene);

      if (!saved) {
        std::println(stderr, "{}", saved.error().message);
        return 1;
      }

      auto loaded = World::loadScene(*files, "script-authoring-acceptance/main.dotscene");

      if (!loaded || loaded->id() == scene->id()) {
        std::println(stderr, "Script-authored persistence/scope failed");
        return 1;
      }

      auto live = std::make_shared<World::Scene>(std::move(*loaded));
      auto entities = live->entities();

      if (!entities || entities->size() != 2 || !(*entities)[0].camera
          || !(*entities)[0].audioListener || !(*entities)[1].mesh || !(*entities)[1].rigidBody
          || !(*entities)[1].audioSource || !(*entities)[1].script) {
        std::println(stderr, "Script-authored six-component round trip failed");
        return 1;
      }

      const auto actor = (*entities)[1].id;
      auto scripts = Script::SceneScripts::create(live, *program);

      if (!scripts) {
        std::println(stderr, "{}", scripts.error().message);
        return 1;
      }

      auto started = scripts->start();

      if (!started || !started->failures.empty() || started->realmFaulted) {
        std::println(stderr, "Script-authored lifecycle start failed");
        return 1;
      }

      std::optional<Graphics::Renderer> renderer;
      std::optional<Graphics::SceneRenderer> graphics;
      constexpr std::size_t center = (32 * 64 + 32) * 4;

      if (render) {
        auto device = Graphics::Renderer::create(
            nullptr,
            {.postProcess = {0, Graphics::ToneMap::LinearClamp, Graphics::ColorSpace::Linear}});

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
        auto blue = graphics->render(*live, 64, 64);

        if (!blue || blue->pixels[center + 2] < 200 || blue->pixels[center + 1] > 20) {
          std::println(stderr, "Script-authored camera/blue mesh GPU pixels failed");
          return 1;
        }
      }

      for (int tick = 0; tick < 2; ++tick) {
        auto updated = scripts->update(1.0 / 60);
        auto state = live->entity(actor);

        if (!updated || !updated->failures.empty() || updated->realmFaulted || !state
            || !state->rigidBody || !state->audioSource || !state->mesh
            || state->rigidBody->mass != 2 || state->audioSource->voice.pitch != 2
            || state->mesh->visible != (tick == 0)) {
          std::println(stderr, "Script component assignment/lifecycle update failed");
          return 1;
        }

        if (render) {
          auto image = graphics->render(*live, 64, 64);

          if (!image || image->pixels[center + 2] > 20
              || (tick == 0 ? image->pixels[center + 1] < 200 : image->pixels[center + 1] > 20)
              || graphics->stats()->meshes != (tick == 0 ? 1U : 0U)) {
            if (!image) {
              std::println(stderr, "Script-authored render: {}", image.error().message);
            } else {
              std::println(stderr,
                           "Script-authored tint/visibility GPU pixels failed: "
                           "tick {}, RGB ({},{},{}), cached meshes {}",
                           tick, image->pixels[center], image->pixels[center + 1],
                           image->pixels[center + 2], graphics->stats()->meshes);
            }

            return 1;
          }
        }
      }

      auto stopped = scripts->stop();
      auto state = live->entity(actor);

      if (!stopped || !stopped->failures.empty() || !state || state->mesh || state->rigidBody
          || state->audioSource || scripts->stats()->runtime.instances != 0) {
        std::println(stderr, "Script component removal/stop failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || std::filesystem::exists(output)
        || after.bytes != memory.bytes || after.allocations != memory.allocations
        || Core::processResources().handles != 0 || Core::processResources().children != 0) {
      std::println(stderr, "Script authoring compiler/process/VM cleanup failed");
      return 1;
    }

    std::println(
        "Script authoring acceptance passed: six typed components, validation, "
        "save/load, lifecycle edits/removal, resource cleanup{}",
        render ? ", actual camera/tint/visibility GPU pixels" : "");
    return 0;
  }
}  // namespace Dot::Runtime
