#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Script.PhysicsSource;
import Dot.World.Scene;
import Dot.Physics.Body;

namespace {
  using namespace Dot::Script;
  using Json = nlohmann::json;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-physics-sdk-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      REQUIRE(files().createDirectories("scripts"));
    }

    ~Project() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      auto result = Dot::Core::FileSystem::open(root);
      REQUIRE(result);
      return *result;
    }

    auto write(std::string_view source) const -> void {
      REQUIRE(files().writeTextAtomic("scripts/actor.ts", source));
    }

    auto compile() const -> CompiledProgram {
      auto compiler = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER);
      REQUIRE(compiler);
      auto result = compiler->compileProject(files());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Physics SDK");
    REQUIRE(result);
    auto native = std::make_shared<Dot::World::Scene>(std::move(*result));
    auto actor = native->createEntity("Actor").value();
    REQUIRE(native->setRigidBody(actor, Dot::World::RigidBody{}));
    REQUIRE(native->setScript(actor, Dot::World::ScriptComponent{"actor.ts", "Actor"}));
    return native;
  }

  auto source(const std::shared_ptr<Dot::World::Scene>& native) -> PhysicsSource {
    PhysicsSourceConfig config;
    config.simulation.gravity = {};
    auto result = PhysicsSource::create(native, config);
    REQUIRE(result);
    return std::move(*result);
  }

  auto service(const std::shared_ptr<Dot::World::Scene>& native, const CompiledProgram& program,
               const PhysicsSource& source) -> SceneScripts {
    SceneScriptsConfig config;
    config.services = source.bindings().value();
    auto result = SceneScripts::create(native, program, config);
    REQUIRE(result);
    return std::move(*result);
  }

  auto good(Dot::Core::Result<ScriptReport> result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->failures.empty());
    REQUIRE_FALSE(result->realmFaulted);
  }
}  // namespace

TEST_CASE("physics_sdk_real_typescript_controls_native_motion_rays_and_owned_runtime_state") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, physics} from 'dot:engine';

export class Actor extends ScriptComponent {
  onStart(): void {
    this.self.setVelocity({x: 1, y: 0, z: 0});
    this.self.addImpulse({x: 1, y: 0, z: 0});
    this.self.setAngularVelocity({x: 0, y: 0.5, z: 0});
  }

  onUpdate(): void {
    const before = this.self.physicsBody;
    const ray = physics.rayCast({x: before.position.x, y: 2, z: 0}, {x: 0, y: -4, z: 0});

    if (ray?.entity.value !== this.self.id.value || ray.normal === null) {
      throw Error('real sphere ray');
    }

    before.velocity.x = 99;

    if (physics.body(this.self.id).velocity.x !== 2 || physics.stats().bodies !== 1) {
      throw Error('runtime snapshot ownership');
    }

    this.self.name = 'queried';
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto actor = native->entities()->front().id;
  auto physics = source(native);
  auto scripts = service(native, program, physics);
  good(scripts.start());
  REQUIRE(physics.step(0.1));
  good(scripts.update(0.1));
  CHECK(native->entity(actor)->name == "queried");
  CHECK(native->entity(actor)->transform.position.x == doctest::Approx(0.2));
  good(scripts.stop());
  CHECK(scripts.stats()->runtime.instances == 0);
}

TEST_CASE("physics_sdk_preparation_and_migration_allow_queries_deny_controls_and_keep_motion") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, physics} from 'dot:engine';
import type {ScriptContext, HostError, Entity} from 'dot:engine';

function inspect(self: Entity): void {
  self.physicsBody;
  physics.stats();
  physics.contacts;
  physics.rayCast({x: 0, y: 2, z: 0}, {x: 0, y: -4, z: 0});
  const attempts = [() => self.setVelocity({x: 9, y: 0, z: 0}),
    () => self.setAngularVelocity({x: 0, y: 1, z: 0}),
    () => self.addImpulse({x: 9, y: 0, z: 0}),
    () => self.moveKinematic({x: 1, y: 0, z: 0}, {x: 0, y: 0, z: 0, w: 1}, 0.1)];

  for (const attempt of attempts) {
    let denied = false;

    try {
      attempt();
    } catch (error) {
      denied = (error as HostError).code === 'InvalidState';
    }

    if (!denied) {
      throw Error('physics write during preparation');
    }
  }
}

export class Actor extends ScriptComponent {
  #ticks = 0;

  constructor(context: ScriptContext) {
    super(context);
    inspect(this.self);
  }

  onStart(): void {
    this.self.setVelocity({x: 1, y: 0, z: 0});
  }

  onUpdate(): void {
    this.self.name = 'tick' + ++this.#ticks;
  }

  onSaveState(): {ticks: number} {
    inspect(this.self);
    return {ticks: this.#ticks};
  }

  onLoadState(state: {ticks: number}): void {
    inspect(this.self);
    this.#ticks = state.ticks;
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto actor = native->entities()->front().id;
  auto physics = source(native);
  auto scripts = service(native, program, physics);
  good(scripts.start());
  REQUIRE(physics.step(0.1));
  good(scripts.update(0.1));
  auto moved = std::move(physics);
  auto reload = scripts.reload(program);
  REQUIRE(reload);
  CHECK(reload->migrated == 1);
  REQUIRE(moved.step(0.1));
  good(scripts.update(0.1));
  CHECK(native->entity(actor)->name == "tick2");
  CHECK(native->entity(actor)->transform.position.x == doctest::Approx(0.2));
  REQUIRE(moved.reset());
  good(scripts.update(0.1));
  CHECK(native->entity(actor)->name == "tick3");
  good(scripts.stop());
}

TEST_CASE("physics_sdk_invalid_control_is_catchable_and_missing_bodies_are_not_fake_success") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, scene} from 'dot:engine';
import type {HostError} from 'dot:engine';

export class Actor extends ScriptComponent {
  onStart(): void {
    let invalid = false;

    try {
      this.self.setVelocity({x: 501, y: 0, z: 0});
    } catch (error) {
      invalid = (error as HostError).code === 'InvalidArgument';
    }

    const missing = scene.create('No Body');
    let absent = false;

    try {
      missing.addImpulse({x: 1, y: 0, z: 0});
    } catch (error) {
      absent = (error as HostError).code === 'NotFound';
    }

    missing.destroy();

    if (!invalid || !absent || this.self.physicsBody.velocity.x !== 0) {
      throw Error('invalid native control contract');
    }

    this.self.setVelocity({x: 1, y: 0, z: 0});
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto actor = native->entities()->front().id;
  auto physics = source(native);
  auto scripts = service(native, program, physics);
  good(scripts.start());
  REQUIRE(physics.step(0.1));
  CHECK(native->entity(actor)->transform.position.x == doctest::Approx(0.1));
  CHECK_FALSE(scripts.stats()->runtime.faulted);
  good(scripts.stop());
}

TEST_CASE("physics_sdk_two_components_read_the_same_contact_batch_without_consuming_it") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, physics} from 'dot:engine';

export class Actor extends ScriptComponent {
  onUpdate(): void {
    const batch = physics.contacts;

    if (batch.events.length !== 1 || batch.events[0]!.phase !== 'begin' ||
        batch.events[0]!.geometry === null) {
      throw Error('missing shared contact batch');
    }

    this.self.name = batch.index;
    batch.events.length = 0;
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto first = native->entities()->front().id;
  auto second = native->createEntity("Other").value();
  REQUIRE(native->setRigidBody(second, Dot::World::RigidBody{}));
  REQUIRE(native->setTransform(second, {{0, 0.9F, 0}}));
  REQUIRE(native->setScript(second, Dot::World::ScriptComponent{"actor.ts", "Actor"}));
  auto physics = source(native);
  auto scripts = service(native, program, physics);
  REQUIRE(physics.step(1.0 / 60));
  good(scripts.update(1.0 / 60));
  CHECK(native->entity(first)->name == "2");
  CHECK(native->entity(second)->name == "2");
  good(scripts.stop());
}

TEST_CASE("physics_sdk_typecheck_rejects_incomplete_vectors_states_and_unsupported_controls") {
  Project project;

  for (const auto* code :
       {"import {physics} from 'dot:engine';physics.rayCast({x:1}, {x:0,y:1,z:0});",
        "import {physics} from 'dot:engine';physics.body({scene:1,value:2});",
        "import {physics} from 'dot:engine';physics.step(0.1);",
        "import {scene} from "
        "'dot:engine';scene.create().moveKinematic({x:0,y:0,z:0},{w:1},0.1);",
        "import {physics} from 'dot:engine';physics.contacts={events:[]};"}) {
    project.write(code);
    auto compiler = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    auto result = compiler.compileProject(project.files());
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}
