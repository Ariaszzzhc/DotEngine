#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

import Dot.Script.NavigationSource;
import Dot.Script.PhysicsSource;
import Dot.Navigation.Geometry;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Core.Result;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    std::shared_ptr<World::Scene> scene
        = std::make_shared<World::Scene>(World::Scene::create().value());
    World::EntityId floor = scene->createEntity("Floor").value();
    World::EntityId actor = scene->createEntity("Actor").value();
    World::EntityId wall = scene->createEntity("Obstacle").value();
    Navigation::Surface surface;
    std::optional<Script::PhysicsSource> physics;

    Fixture() {
      World::RigidBody box;
      box.motion = Physics::Motion::Static;
      box.shape = Physics::Shape::Box;
      REQUIRE(scene->setRigidBody(floor, box));
      REQUIRE(scene->setTransform(floor, {{0, -.25F, 0}, {}, {12, .5F, 12}}));
      REQUIRE(scene->setRigidBody(wall, box));
      REQUIRE(scene->setTransform(wall, {{0, 1.5F, 0}, {}, {1.5F, 3, 1.5F}}));
      REQUIRE(scene->setCharacterController(actor, World::CharacterController{}));
      REQUIRE(scene->setTransform(actor, {{-4, 0, 0}}));
      REQUIRE(scene->setNavigation("assets/room.dotnav"));
      const auto geometry = Navigation::sceneGeometry(*scene, {}).value();
      surface = Navigation::Surface::build(geometry, {}).value();
      physics.emplace(Script::PhysicsSource::create(scene).value());
    }

    auto source() -> Script::NavigationSource {
      auto result = Script::NavigationSource::create(scene, *physics, surface);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }

    auto args(World::EntityId id) -> Json {
      return Json::array(
          {{{"scene", std::to_string(id.scene)}, {"value", std::to_string(id.value)}},
           {{"x", 4}, {"y", 0}, {"z", 0}},
           {{"x", .5}, {"y", .5}, {"z", .5}},
           256});
    }
  };

  auto call(const Script::HostBindings& bindings, std::string_view method, const Json& args)
      -> Core::Result<std::optional<std::string>> {
    const auto found = std::ranges::find(bindings, method, &Script::HostFunction::name);
    REQUIRE(found != bindings.end());
    return found->callback(args.dump());
  }
}  // namespace

TEST_CASE("navigation_host.actual_character_route_is_read_only_owned_and_profile_checked") {
  Fixture f;
  auto source = f.source();
  auto bindings = source.bindings().value();
  REQUIRE(bindings.size() == 6);
  CHECK(std::ranges::all_of(bindings, [](const auto& value) {
    return value.access
           == ((value.name == "navigation.move" || value.name == "navigation.stop")
                   ? Script::HostAccess::ReadWrite
                   : Script::HostAccess::ReadOnly);
  }));
  const auto before = World::encodeScene(*f.scene).value();
  auto value = call(bindings, "navigation.route", f.args(f.actor));
  REQUIRE_MESSAGE(value, (value ? "" : value.error().message));
  const auto route = Json::parse(**value);
  CHECK(route["start"]["x"] == -4);
  CHECK(route["points"].size() >= 3);
  CHECK(World::encodeScene(*f.scene).value() == before);
  REQUIRE(f.scene->setTransform(f.actor, {{-3, 0, 2}}));
  CHECK_FALSE(call(bindings, "navigation.route", f.args(f.actor)));
  REQUIRE(f.physics->synchronize());
  auto next = call(bindings, "navigation.route", f.args(f.actor));
  REQUIRE(next);
  CHECK(Json::parse(**next)["start"]["z"] == 2);
  CHECK(route["start"]["z"] == 0);
  auto profile = *f.scene->entity(f.actor)->character;
  profile.radius = .6F;
  REQUIRE(f.scene->setCharacterController(f.actor, profile));
  REQUIRE(f.physics->synchronize());
  CHECK_FALSE(call(bindings, "navigation.route", f.args(f.actor)));
  const auto stats = call(bindings, "navigation.stats", Json::array());
  REQUIRE(stats);
  CHECK(Json::parse(**stats)["agentRadius"].get<float>() == doctest::Approx(.35F));
  REQUIRE(f.scene->setCharacterController(f.actor, World::CharacterController{}));
  REQUIRE(f.physics->synchronize());
  CHECK_FALSE(call(bindings, "navigation.route", f.args(f.floor)));
  auto foreign = f.actor;
  ++foreign.scene;
  CHECK_FALSE(call(bindings, "navigation.route", f.args(foreign)));
  auto bad = f.args(f.actor);
  bad[0]["extra"] = 1;
  CHECK_FALSE(call(bindings, "navigation.route", bad));
  bad = f.args(f.actor);
  bad[3] = 2.5;
  CHECK_FALSE(call(bindings, "navigation.route", bad));
  bad = f.args(f.actor);
  bad[1]["y"] = 100;
  CHECK_FALSE(call(bindings, "navigation.route", bad));
  CHECK_FALSE(call(bindings, "navigation.stats", Json::array({1})));
  REQUIRE(call(bindings, "navigation.route", f.args(f.actor)));
}

TEST_CASE("navigation_host.stale_static_geometry_binding_thread_and_scene_are_rejected") {
  Fixture f;
  auto source = f.source();
  auto bindings = source.bindings().value();
  const auto original = f.scene->entity(f.wall)->rigidBody.value();
  auto changed = original;
  changed.friction = .8F;
  REQUIRE(f.scene->setRigidBody(f.wall, changed));
  REQUIRE(call(bindings, "navigation.route", f.args(f.actor)));
  REQUIRE(f.scene->setTransform(f.wall, {{0, 1.5F, 1}}));
  CHECK_FALSE(call(bindings, "navigation.route", f.args(f.actor)));
  CHECK_FALSE(call(bindings, "navigation.stats", Json::array()));
  CHECK_FALSE(Script::NavigationSource::create(f.scene, *f.physics, f.surface));
  REQUIRE(f.scene->setTransform(f.wall, {{0, 1.5F, 0}, {}, {1.5F, 3, 1.5F}}));
  REQUIRE(call(bindings, "navigation.stats", Json::array()));
  REQUIRE(f.scene->setNavigation("assets/other.dotnav"));
  CHECK_FALSE(source.bindings());
  CHECK_FALSE(call(bindings, "navigation.stats", Json::array()));
  REQUIRE(f.scene->setNavigation("assets/room.dotnav"));
  const auto wrongThread = std::async(std::launch::async, [&] { return source.bindings(); }).get();
  CHECK_FALSE(wrongThread);
  *f.scene = World::Scene::create().value();
  CHECK_FALSE(call(bindings, "navigation.stats", Json::array()));
}

TEST_CASE("navigation_host.absence_and_invalid_prepared_ownership_never_fake_a_route") {
  auto scene = std::make_shared<World::Scene>(World::Scene::create().value());
  auto physics = Script::PhysicsSource::create(scene).value();
  auto source = Script::NavigationSource::create(scene, physics).value();
  auto bindings = source.bindings().value();
  auto stats = call(bindings, "navigation.stats", Json::array());
  REQUIRE(stats);
  CHECK(**stats == "null");
  auto absent = call(bindings, "navigation.route", Json::array({{}, {}, {}, 256}));
  REQUIRE_FALSE(absent);
  CHECK(absent.error().code == Core::ErrorCode::Unavailable);
  CHECK_FALSE(Script::NavigationSource::create(nullptr, physics));
  CHECK_FALSE(Script::NavigationSource::create(scene, physics, Navigation::Surface{}));
  REQUIRE(scene->setNavigation("assets/room.dotnav"));
  CHECK_FALSE(Script::NavigationSource::create(scene, physics));
  CHECK_FALSE(Script::NavigationSource::create(scene, physics, Navigation::Surface{}));
  CHECK_FALSE(scene->setNavigation("assets/../room.dotnav"));
  CHECK_FALSE(scene->setNavigation("room.dotnav"));
  CHECK_FALSE(scene->setNavigation("assets/room.DOTNAV"));
}

TEST_CASE("navigation_host.native_scaled_profile_keeps_step_height_in_world_metres") {
  Fixture f;
  REQUIRE(f.scene->setTransform(f.actor, {{-4, 0, 0}, {}, {2, 2, 2}}));
  REQUIRE(f.physics->synchronize());
  Navigation::BuildSettings settings;
  settings.agentHeight = 4;
  settings.agentRadius = .75F;
  settings.maxClimb = .6F;
  auto geometry = Navigation::sceneGeometry(*f.scene, settings).value();
  auto surface = Navigation::Surface::build(geometry, settings).value();
  auto source = Script::NavigationSource::create(f.scene, *f.physics, surface).value();
  auto invalid = call(source.bindings().value(), "navigation.route", f.args(f.actor));
  REQUIRE_FALSE(invalid);
  CHECK(invalid.error().code == Core::ErrorCode::InvalidArgument);
  settings.maxClimb = .35F;
  surface = Navigation::Surface::build(geometry, settings).value();
  source = Script::NavigationSource::create(f.scene, *f.physics, surface).value();
  REQUIRE(call(source.bindings().value(), "navigation.route", f.args(f.actor)));
  REQUIRE(call(f.physics->bindings().value(), "physics.setCharacterYaw",
               Json::array({f.args(f.actor)[0], 90})));
  auto reader = f.physics->characterReader().value();
  const auto actual = reader(f.actor);
  REQUIRE(actual);
  CHECK(actual->radius == doctest::Approx(.7F));
  CHECK(actual->yawDegrees == doctest::Approx(90));
  CHECK(actual->stepHeight == doctest::Approx(.35F));
  auto retained = std::move(*f.physics);
  CHECK_FALSE(f.physics->characterReader());
  REQUIRE(reader(f.actor));
}

TEST_CASE("navigation_host.movement_uses_actual_physics_and_rejects_partial_batches") {
  Fixture f;
  REQUIRE(f.scene->setTransform(f.actor, {{-4, 0, -3}}));
  REQUIRE(f.physics->synchronize());
  auto source = f.source();
  const auto id = f.args(f.actor)[0];
  auto control = f.physics->characterControl().value();
  const auto bindingsBefore = source.bindings().value();
  CHECK_FALSE(call(
      bindingsBefore, "navigation.move",
      Json::array(
          {id, {{"x", 4}, {"y", 0}, {"z", -3}}, 1.2, {{"goal", {{"x", 0}, {"y", 0}, {"z", 0}}}}})));
  const auto before = World::encodeScene(*f.scene).value();
  auto requested
      = source.perform(Json{{"command", "move"},
                            {"entity", id},
                            {"value", {{"goal", {{"x", 4}, {"y", 0}, {"z", -3}}}, {"speed", 1.2}}}}
                           .dump());
  REQUIRE_MESSAGE(requested, (requested ? "" : requested.error().message));
  CHECK_FALSE(source.advance(0));
  REQUIRE(source.advance(1));
  CHECK(World::encodeScene(*f.scene).value() == before);
  auto observations = control.snapshot().value();
  REQUIRE(observations.size() == 1);
  CHECK(observations[0].config.position.x == -4);
  auto invalid
      = source.perform(Json{{"command", "move"},
                            {"entity", id},
                            {"value", {{"goal", {{"x", 40}, {"y", 0}, {"z", -3}}}, {"speed", 1.2}}}}
                           .dump());
  CHECK_FALSE(invalid);
  CHECK(Json::parse(*source.perform(
            Json{{"command", "state"}, {"entity", id}}.dump()))["agent"]["goal"]["x"]
        == 4);
  const std::array bad{Script::CharacterIntent{f.actor, {{-1, 0, 0}}, {}},
                       Script::CharacterIntent{{f.actor.scene + 1, f.actor.value}, {}, {}}};
  CHECK_FALSE(control.apply(bad));
  REQUIRE(f.physics->advance(1));
  CHECK(control.snapshot()->front().measuredVelocity.x > 0);

  for (int frame = 0; frame < 600; ++frame) {
    REQUIRE(source.advance(1.0 / 60));
    REQUIRE(f.physics->advance(1.0 / 60));
  }

  const auto state
      = Json::parse(*source.perform(Json{{"command", "state"}, {"entity", id}}.dump()))["agent"];
  CHECK(state["phase"] == "arrived");
  CHECK(std::abs(control.snapshot()->front().config.position.x - 4) <= .12F);
  REQUIRE(source.perform(Json{{"command", "stop"}, {"entity", id}}.dump()));
  CHECK(Json::parse(*source.perform(Json{{"command", "state"}, {"entity", id}}.dump()))["agent"]
            .is_null());
  REQUIRE(source.perform(Json{{"command", "stop"}, {"entity", id}}.dump()));
  auto bindings = source.bindings().value();
  source.retire();
  CHECK_FALSE(call(bindings, "navigation.agents", Json::array()));
  CHECK_FALSE(source.advance(1.0 / 60));
}

TEST_CASE("navigation_host.reciprocal_characters_pass_using_measured_simultaneous_snapshots") {
  Fixture f;
  REQUIRE(f.scene->setTransform(f.actor, {{-3, 0, -3}}));
  const auto peer = f.scene->createEntity("Peer").value();
  REQUIRE(f.scene->setCharacterController(peer, World::CharacterController{}));
  REQUIRE(f.scene->setTransform(peer, {{3, 0, -3}}));
  REQUIRE(f.physics->synchronize());
  auto source = f.source();

  for (const auto actor : {f.actor, peer}) {
    REQUIRE(source.perform(Json{
        {"command", "move"},
        {"entity", f.args(actor)[0]},
        {"value",
         {{"goal", {{"x", actor == f.actor ? 3 : -3}, {"y", 0}, {"z", -3}}},
          {"speed", 1.2}}}}.dump()));
  }

  float clearance = 10;
  bool detoured{};

  for (int frame = 0; frame < 600; ++frame) {
    REQUIRE(source.advance(1.0 / 60));
    REQUIRE(f.physics->advance(1.0 / 60));
    const auto actual = f.physics->characterControl()->snapshot().value();
    const auto a = actual[0].config.position, b = actual[1].config.position;
    clearance = std::min(clearance, std::hypot(a.x - b.x, a.z - b.z));
    detoured = detoured || std::abs(a.z + 3) > .2F || std::abs(b.z + 3) > .2F;
  }

  CHECK(clearance > .68F);
  CHECK(detoured);

  for (const auto actor : {f.actor, peer}) {
    const auto state = Json::parse(
        *source.perform(Json{{"command", "state"}, {"entity", f.args(actor)[0]}}.dump()))["agent"];
    INFO(state.dump());
    CHECK(state["phase"] == "arrived");
  }
}

TEST_CASE("navigation_host.occupied_goal_stale_geometry_and_native_profile_fail_honestly") {
  Fixture f;
  REQUIRE(f.scene->setTransform(f.actor, {{-3, 0, -3}}));
  const auto peer = f.scene->createEntity("Standing peer").value();
  REQUIRE(f.scene->setCharacterController(peer, World::CharacterController{}));
  REQUIRE(f.scene->setTransform(peer, {{0, 0, -3}}));
  REQUIRE(f.physics->synchronize());
  auto source = f.source();
  const Json request{{"command", "move"},
                     {"entity", f.args(f.actor)[0]},
                     {"value", {{"goal", {{"x", 0}, {"y", 0}, {"z", -3}}}, {"speed", 1.2}}}};
  REQUIRE(source.perform(request.dump()));

  for (int frame = 0; frame < 360; ++frame) {
    REQUIRE(source.advance(1.0 / 60));
    REQUIRE(f.physics->advance(1.0 / 60));
  }

  const auto query = Json{{"command", "state"}, {"entity", f.args(f.actor)[0]}}.dump();
  auto state = Json::parse(*source.perform(query))["agent"];
  CHECK(state["phase"] == "moving");
  CHECK(state["distance"].get<float>() > .65F);
  REQUIRE(f.scene->setTransform(peer, {{0, 0, -5}}));
  REQUIRE(f.physics->synchronize());

  for (int frame = 0; frame < 180; ++frame) {
    REQUIRE(source.advance(1.0 / 60));
    REQUIRE(f.physics->advance(1.0 / 60));
  }

  CHECK(Json::parse(*source.perform(query))["agent"]["phase"] == "arrived");
  REQUIRE(f.scene->setTransform(f.wall, {{0, 1.5F, 1}}));
  CHECK(Json::parse(*source.perform(query))["agent"]["phase"] == "failed");
  REQUIRE(f.physics->synchronize());
  REQUIRE(source.advance(1.0 / 60));
  CHECK(Json::parse(*source.perform(query))["agent"]["error"]["code"] == "InvalidState");
  REQUIRE(f.scene->setTransform(f.wall, {{0, 1.5F, 0}, {}, {1.5F, 3, 1.5F}}));
  REQUIRE(f.physics->synchronize());
  REQUIRE(source.perform(request.dump()));
  auto profile = *f.scene->entity(f.actor)->character;
  profile.radius = .6F;
  REQUIRE(f.scene->setCharacterController(f.actor, profile));
  REQUIRE(f.physics->synchronize());
  REQUIRE(source.advance(1.0 / 60));
  CHECK(Json::parse(*source.perform(query))["agent"]["error"]["code"] == "InvalidArgument");
  auto bad = request;
  bad["value"]["typo"] = 1;
  CHECK_FALSE(source.perform(bad.dump()));
  bad = request;
  bad["entity"]["scene"] = "999999";
  CHECK_FALSE(source.perform(bad.dump()));
  CHECK_FALSE(source.perform(R"({"command":"agents","extra":0})"));
  CHECK_FALSE(source.perform(R"({"command":"unknown"})"));
  CHECK_FALSE(source.perform("[]"));
  CHECK_FALSE(source.perform("{}"));
}
