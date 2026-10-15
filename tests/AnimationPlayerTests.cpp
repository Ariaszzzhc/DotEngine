#include <doctest/doctest.h>

#include <limits>
#include <nlohmann/json.hpp>
#include <thread>
#include <utility>

import Dot.Animation.Source;
import Dot.World.Scene;
import Dot.World.SceneFiles;

TEST_CASE("animation.player_authoring_strict_codec_and_boundaries_without_devices") {
  using namespace Dot::Animation;
  using Json = nlohmann::json;
  PlayerProperties player{{"assets/move.dotanimation"}, {{0, 1}}};
  REQUIRE(validatePlayerProperties(player));
  const auto encoded = encodePlayerProperties(player).value();
  CHECK(decodePlayerProperties(encoded).value() == player);
  auto bad = player;
  bad.speed = std::numeric_limits<double>::quiet_NaN();
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.clips.push_back(bad.clips[0]);
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.targets.push_back({1, 1});
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.targets.push_back({0, 2});
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.targets[0].entity = 0;
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.clips[0] = "assets/../outside.dotanimation";
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.clip = 1;
  CHECK_FALSE(validatePlayerProperties(bad));

  for (const auto* field : {"extra", "clip", "speed", "enabled", "targets"}) {
    auto json = Json::parse(encoded);
    json[field] = "bad";
    CHECK_FALSE(decodePlayerProperties(json.dump()));
  }

  auto json = Json::parse(encoded);
  json["targets"][0]["entity"] = "01";
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  CHECK_FALSE(decodePlayerProperties("{\"clips\":[],\"clips\":[]}"));
}

TEST_CASE("animation.player_scene_ownership_clone_deletion_and_transactional_validation") {
  using namespace Dot;
  auto scene = World::Scene::create().value();
  const auto owner = scene.createEntity("Player").value();
  const auto target = scene.createEntity("Target").value();
  World::AnimationPlayer player{{"assets/move.dotanimation"}, {{0, target.value}}};
  REQUIRE(scene.setAnimationPlayer(owner, player));
  auto copy = scene.animationPlayers().value();
  REQUIRE(copy.size() == 1);
  copy[0].properties.speed = 2;
  CHECK(scene.entity(owner)->animation == player);
  auto bad = player;
  bad.targets[0].entity = 999;
  CHECK_FALSE(scene.setAnimationPlayer(owner, bad));
  CHECK(scene.entity(owner)->animation == player);
  CHECK_FALSE(scene.destroyEntity(target));
  CHECK(scene.entityCount().value() == 2);
  auto cloned = World::decodeScene(World::encodeScene(scene).value()).value();
  CHECK(cloned.id() != scene.id());
  CHECK(cloned.entity(cloned.findEntity(owner.value).value())->animation == player);
  CHECK_FALSE(cloned.setAnimationPlayer(owner, player));
  bool rejected{};
  std::thread worker([&] { rejected = !scene.animationPlayers(); });
  worker.join();
  CHECK(rejected);
  REQUIRE(scene.setAnimationPlayer(owner, {}));
  REQUIRE(scene.destroyEntity(target));
  CHECK(scene.animationPlayers()->empty());
  auto moved = std::move(scene);
  CHECK_FALSE(scene.animationPlayers());
  CHECK(moved.entityCount().value() == 1);
}
