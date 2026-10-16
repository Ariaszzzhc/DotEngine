#include <doctest/doctest.h>

#include <algorithm>
#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Script.Compiler;
import Dot.Script.PhysicsSource;
import Dot.Script.SceneScripts;
import Dot.World.Scene;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage;
    std::shared_ptr<World::Scene> scene;
    World::EntityId camera;
    Script::CompiledProgram program;

    Fixture()
        : storage(Core::StagedDirectory::create(
                      Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          scene(std::make_shared<World::Scene>(World::Scene::create("Camera regression").value())),
          camera(scene->createEntity("Camera").value()),
          program([&] {
            auto files = storage.files().value();
            REQUIRE(files.createDirectories("scripts"));
            auto source
                = Core::FileSystem::open(DOT_EXAMPLE_PROJECT)->readText("scripts/camera.ts");
            REQUIRE(source);
            REQUIRE(files.writeTextAtomic("scripts/camera.ts", *source));
            REQUIRE(files.writeTextAtomic("scripts/probe.ts", R"ts(
import {scene, ScriptComponent} from 'dot:engine';
import {ShoulderCamera} from './camera';
export class Probe extends ScriptComponent {
  #rig = new ShoulderCamera(scene.primaryCamera!);
  onUpdate(seconds: number): void {
    this.#rig.update({x: 0, y: 0, z: 0}, seconds);
    this.self.name = JSON.stringify(this.#rig.state);
  }
}
)ts"));
            auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
            auto result = compiler.compileProject(files);
            REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
            return std::move(*result);
          }()) {
      World::Camera view;
      view.primary = true;
      REQUIRE(scene->setCamera(camera, view));
      const auto probe = scene->createEntity("Probe").value();
      REQUIRE(scene->setScript(probe, World::ScriptComponent{"probe.ts", "Probe"}));
    }

    auto box(Math::Vec3 position, Math::Vec3 halfExtents) -> World::EntityId {
      const auto id = scene->createEntity("Obstacle").value();
      REQUIRE(scene->setTransform(id, {position}));
      World::RigidBody body;
      body.motion = Physics::Motion::Static;
      body.shape = Physics::Shape::Box;
      body.halfExtents = halfExtents;
      REQUIRE(scene->setRigidBody(id, body));
      return id;
    }

    auto scripts(const Script::PhysicsSource& physics) -> Script::SceneScripts {
      Script::SceneScriptsConfig config;
      config.services = physics.bindings().value();
      auto result = Script::SceneScripts::create(scene, program, config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }

    auto position() const -> Math::Vec3 {
      return scene->entity(camera)->transform.position;
    }
  };

  auto update(Script::SceneScripts& scripts, double seconds) -> void {
    auto result = scripts.update(seconds);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE_FALSE(result->realmFaulted);

    for (const auto& failure : result->failures) {
      FAIL_CHECK(failure.error.message);
    }

    REQUIRE(result->failures.empty());
  }

  auto separation(const Script::PhysicsSource& physics, Math::Vec3 point) -> float {
    const auto services = physics.bindings().value();
    const auto found
        = std::ranges::find(services, "physics.sphereCast", &Script::HostFunction::name);
    REQUIRE(found != services.end());
    const Json args = Json::array({{{"x", point.x}, {"y", point.y}, {"z", point.z}},
                                   .22,
                                   {{"x", 0}, {"y", 0}, {"z", .001}},
                                   {{"includeSensors", false}, {"ignoreEntity", nullptr}}});
    auto result = found->callback(args.dump());
    REQUIRE(result);
    REQUIRE(result->has_value());
    const auto hit = Json::parse(**result);
    return hit.is_null() ? 0 : hit["penetrationDepth"].get<float>();
  }
}  // namespace

TEST_CASE("prism.camera_volume_catches_corner_and_recovers_smoothly_after_removal") {
  Fixture fixture;
  const auto obstacle = fixture.box({.4F, 1.66F, 1.5F}, {.05F, .3F, .05F});
  auto physics = Script::PhysicsSource::create(fixture.scene).value();
  auto scripts = fixture.scripts(physics);
  update(scripts, 1);
  const auto clipped = fixture.position();
  CHECK(clipped.z < 1.45F);
  CHECK(clipped.z > .8F);
  CHECK(separation(physics, clipped) < .0001F);
  REQUIRE(fixture.scene->destroyEntity(obstacle));
  REQUIRE(physics.synchronize());
  update(scripts, 0);
  CHECK(fixture.position() == clipped);
  update(scripts, .05);
  CHECK(fixture.position().z > clipped.z);
  CHECK(fixture.position().z < 3.1F);

  for (int frame = 0; frame < 10; ++frame) {
    update(scripts, .1);
  }

  CHECK(fixture.position().z > 3.17F);
  CHECK(fixture.position().z < 3.18F);
}

TEST_CASE("prism.camera_initial_overlap_separates_using_native_penetration") {
  Fixture fixture;
  fixture.box({0, 1.5F, 0}, {.4F, .4F, .4F});
  auto physics = Script::PhysicsSource::create(fixture.scene).value();
  auto scripts = fixture.scripts(physics);
  update(scripts, 1);
  CHECK(separation(physics, fixture.position()) < .0001F);
}

TEST_CASE("prism.camera_impossible_corridor_reports_failure_without_publishing_a_pose") {
  Fixture fixture;
  fixture.box({-.2F, 1.5F, 0}, {.1F, 5, 5});
  fixture.box({.2F, 1.5F, 0}, {.1F, 5, 5});
  auto physics = Script::PhysicsSource::create(fixture.scene).value();
  auto scripts = fixture.scripts(physics);
  const auto original = fixture.position();
  auto result = scripts.update(1);
  REQUIRE(result);
  REQUIRE(result->failures.size() == 1);
  CHECK(result->failures[0].error.message.find("could not separate") != std::string::npos);
  CHECK_FALSE(result->realmFaulted);
  CHECK(fixture.position() == original);
}
