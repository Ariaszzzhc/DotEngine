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
import Dot.World.Scene;
import Dot.Physics.Body;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Script.PhysicsSource;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Runtime {
  auto testScriptPhysics(const std::filesystem::path& executable,
                         const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                         bool render) -> int {
    auto files = Core::FileSystem::open(root);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Scripted Physics");

    if (!files || !sdk || !compiler || !created
        || !files->createDirectories("script-physics-acceptance/dot")) {
      std::println(stderr, "Script physics acceptance setup failed");
      return 1;
    }

    for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
      auto source = sdk->readText(path);

      if (!source
          || !files->writeTextAtomic(std::filesystem::path{"script-physics-acceptance"} / path,
                                     *source)) {
        std::println(stderr, "Script physics SDK copy failed");
        return 1;
      }
    }

    auto writeActor = [&](int version) -> Core::Result<void> {
      std::string source = R"ts(
import {ScriptComponent, physics} from './dot/engine.js';
import type {EntityId, HostError, ScriptContext} from './dot/engine.js';

export class Actor extends ScriptComponent<{mover: EntityId}> {
  #ticks = 0;

  constructor(context: ScriptContext<{mover: EntityId}>) {
    super(context);
    this.self.physicsBody;
    physics.contacts;
  }

  onStart(): void {
    this.self.setVelocity({x: 1, y: 0, z: 0});
    this.self.addImpulse({x: 1, y: 0, z: 0});
    this.self.setAngularVelocity({x: 0, y: 0.25, z: 0});
    let rejected = false;

    try {
      this.self.setVelocity({x: 501, y: 0, z: 0});
    } catch (error) {
      rejected = (error as HostError).code === 'InvalidArgument';
    }

    if (!rejected || this.self.physicsBody.velocity.x !== 2) {
      throw Error('velocity/impulse/error recovery');
    }
  }

  onFixedUpdate(seconds: number): void {
    ++this.#ticks;
    const mover = physics.body(this.properties.mover);
    physics.moveKinematic(this.properties.mover,
      {x: mover.position.x, y: mover.position.y + seconds, z: mover.position.z},
      mover.rotation, seconds);
  }

  onUpdate(): void {
    const state = this.self.physicsBody;
    const ray = physics.rayCast({x: state.position.x, y: 2, z: 0}, {x: 0, y: -4, z: 0});
    const batch = physics.contacts;

    if (ray?.entity.value !== this.self.id.value || batch.events.length !== 1 ||
        !batch.events[0]!.sensor || physics.stats().generation !== '1' || state.velocity.x !== 2) {
      throw Error('ray/sensor/shared simulation state');
    }

    const version = 1;
    this.self.name = 'v' + version + ':' + this.#ticks;
  }

  onSaveState(): {ticks: number} {
    this.self.physicsBody;
    return {ticks: this.#ticks};
  }

  onLoadState(state: {ticks: number}): void {
    physics.stats();
    this.#ticks = state.ticks;
  }

  onStop(): void {
    if (this.#ticks !== 30) {
      throw Error('private tick state lost');
    }

    this.self.setVelocity({x: 0, y: 0, z: 0});
    this.self.setAngularVelocity({x: 0, y: 0, z: 0});
  }
}
)ts";
      source.replace(source.find("const version = 1;"), std::string("const version = 1;").size(),
                     "const version = " + std::to_string(version) + ";");
      return files->writeTextAtomic("script-physics-acceptance/actor.ts", source);
    };
    auto scene = std::make_shared<World::Scene>(std::move(*created));
    auto camera = scene->createEntity("Camera");
    auto cube = scene->createEntity("Cube");
    auto mover = scene->createEntity("Mover");
    auto sensor = scene->createEntity("Sensor");

    if (!camera || !cube || !mover || !sensor || !writeActor(1)) {
      std::println(stderr, "Script physics entities/source failed");
      return 1;
    }

    World::Camera lens;
    lens.primary = true;
    World::MeshRenderer mesh;
    mesh.tint = {0, 0, 1, 1};
    World::RigidBody dynamic;
    dynamic.shape = Physics::Shape::Box;
    dynamic.friction = 0;
    World::RigidBody kinematic = dynamic;
    kinematic.motion = Physics::Motion::Kinematic;
    World::RigidBody trigger = dynamic;
    trigger.motion = Physics::Motion::Static;
    trigger.sensor = true;
    trigger.halfExtents = {5, 0.5F, 5};
    const auto properties = "{\"mover\":{\"scene\":\"" + std::to_string(scene->id())
                            + "\",\"value\":\"" + std::to_string(mover->value) + "\"}}";

    if (!scene->setTransform(*camera, {{0, 0, 4}}) || !scene->setCamera(*camera, lens)
        || !scene->setMeshRenderer(*cube, mesh) || !scene->setRigidBody(*cube, dynamic)
        || !scene->setTransform(*mover, {{-4, 3, 0}}) || !scene->setRigidBody(*mover, kinematic)
        || !scene->setTransform(*sensor, {{0, -0.9F, 0}}) || !scene->setRigidBody(*sensor, trigger)
        || !scene->setScript(*cube, World::ScriptComponent{"actor.ts", "Actor", properties})) {
      std::println(stderr, "Script physics authoring failed");
      return 1;
    }

    Script::PhysicsSourceConfig physicsConfig;
    physicsConfig.simulation.gravity = {};
    auto physics = Script::PhysicsSource::create(scene, physicsConfig);

    if (!physics) {
      std::println(stderr, "{}", physics.error().message);
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();

    {
      auto program = compiler->compileProject(*files, "script-physics-acceptance");
      auto bindings = physics->bindings();

      if (!program || !bindings) {
        std::println(stderr, "{}", program ? bindings.error().message : program.error().message);
        return 1;
      }

      Script::SceneScriptsConfig config;
      config.services = std::move(*bindings);
      auto scripts = Script::SceneScripts::create(scene, *program, config);

      if (!scripts) {
        std::println(stderr, "{}", scripts.error().message);
        return 1;
      }

      auto check = [](Core::Result<Script::ScriptReport> report) -> Core::Result<void> {
        if (!report) {
          return std::unexpected(report.error());
        }

        if (!report->failures.empty()) {
          return std::unexpected(report->failures.front().error);
        }

        if (report->realmFaulted) {
          return Core::fail(Core::ErrorCode::InvalidState, "Scripted physics realm faulted");
        }

        return {};
      };
      auto started = check(scripts->start());

      if (!started) {
        std::println(stderr, "{}", started.error().message);
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

      for (int frame = 0; frame < 30; ++frame) {
        if (frame == 15) {
          auto written = writeActor(2);
          auto next = written
                          ? compiler->compileProject(*files, "script-physics-acceptance")
                          : Core::Result<Script::CompiledProgram>{std::unexpected(written.error())};

          if (!next) {
            std::println(stderr, "{}", next.error().message);
            return 1;
          }

          auto reload = scripts->reload(*next);

          if (!reload || reload->migrated != 1 || !reload->retired.failures.empty()) {
            std::println(stderr, "Script physics state migration failed");
            return 1;
          }
        }

        auto fixed = check(scripts->fixedUpdate(1.0 / 60));
        auto stepped = fixed ? physics->step(1.0 / 60) : fixed;
        auto updated = stepped ? check(scripts->update(1.0 / 60)) : stepped;

        if (!updated) {
          std::println(stderr, "{}", updated.error().message);
          return 1;
        }
      }

      auto actorState = scene->entity(*cube);
      auto moverState = scene->entity(*mover);

      if (!actorState || !moverState || std::abs(actorState->transform.position.x - 1) > 0.00001F
          || std::abs(moverState->transform.position.y - 3.5F) > 0.00001F
          || actorState->name != "v2:30") {
        std::println(stderr, "Script physics native motion/reloaded tick state failed");
        return 1;
      }

      if (render) {
        auto after = graphics->render(*scene, 64, 64);
        constexpr std::size_t center = (32 * 64 + 32) * 4;

        if (!after || before->pixels[center + 2] < 200 || after->pixels[center + 2] > 20
            || before->pixels == after->pixels) {
          std::println(stderr, "Script physics actual GPU movement pixels failed");
          return 1;
        }
      }

      auto stopped = check(scripts->stop());

      if (!stopped || scripts->stats()->runtime.instances != 0) {
        std::println(stderr, "Script physics explicit stop failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || after.bytes != memory.bytes
        || after.allocations != memory.allocations || Core::processResources().handles != 0
        || Core::processResources().children != 0) {
      std::println(stderr, "Script physics compiler/process/VM cleanup failed");
      return 1;
    }

    std::println(
        "Script physics acceptance passed: real velocity/impulse/angular/kinematic "
        "control, rays, sensor batches, same simulation across hot reload, cleanup{}",
        render ? ", actual GPU pixels" : "");
    return 0;
  }
}  // namespace Dot::Runtime
