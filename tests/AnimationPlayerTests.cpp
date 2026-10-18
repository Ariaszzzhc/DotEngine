#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.Source;
import Dot.World.Scene;
import Dot.World.SceneFiles;

TEST_CASE("animation.scene_layer_mask_budget_rejects_replacement_without_changing_properties") {
  using namespace Dot::Animation;
  auto scene = Dot::World::Scene::create().value();
  std::vector<Dot::World::EntityId> owners;
  std::vector<PlayerProperties> players;

  for (const auto count : {500U, 500U, 250U}) {
    owners.push_back(scene.createEntity().value());
    PlayerProperties player{{"assets/motion.dotanimation"}, {}};
    std::vector<LayerChannel> mask;

    for (std::uint32_t node = 0; node < count; ++node) {
      auto entity = scene.createEntity().value();
      player.targets.push_back({node, entity.value});

      for (const auto path : {Path::Translation, Path::Rotation, Path::Scale}) {
        mask.push_back({node, path});
      }
    }

    for (int index = 0; index < 8; ++index) {
      player.layers.push_back({"Layer" + std::to_string(index), 0, mask});
    }

    REQUIRE(scene.setAnimationPlayer(owners.back(), player));
    players.push_back(std::move(player));
  }

  auto extra = scene.createEntity().value();
  auto candidate = players.back();
  candidate.targets.push_back({250, extra.value});
  candidate.layers.front().mask.push_back({250, Path::Rotation});
  CHECK_FALSE(scene.setAnimationPlayer(owners.back(), candidate));
  CHECK(scene.entity(owners.back())->animation.value() == players.back());
}

TEST_CASE("animation.layer_authoring_round_trip_and_strict_masks_preserve_source") {
  using namespace Dot::Animation;
  using Json = nlohmann::json;
  PlayerProperties player{{"assets/base.dotanimation", "assets/action.dotanimation"},
                          {{0, 1}, {1, 2}}};
  player.layers
      = {{"UpperBody", 1, {{0, Path::Rotation, .5}, {1, Path::Translation}}, .75, -1, false, true}};
  const auto encoded = encodePlayerProperties(player).value();
  CHECK(decodePlayerProperties(encoded).value() == player);
  auto bad = player;
  bad.layers.push_back(bad.layers.front());
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.layers[0].clip = 2;
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.layers[0].mask[0].node = 7;
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.layers[0].mask.push_back(bad.layers[0].mask.front());
  CHECK_FALSE(validatePlayerProperties(bad));
  bad = player;
  bad.layers[0].mask[0].weight = 2;
  CHECK_FALSE(validatePlayerProperties(bad));
  auto json = Json::parse(encoded);
  json.erase("layers");
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  json = Json::parse(encoded);
  json["layers"][0]["clip"] = true;
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  json = Json::parse(encoded);
  json["layers"][0]["mask"][0]["extra"] = 0;
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  auto additive = player;
  additive.layers[0].mode = LayerMode::Additive;
  additive.layers[0].referenceTime = .75;
  const auto relative = encodePlayerProperties(additive).value();
  CHECK(decodePlayerProperties(relative).value() == additive);

  for (const auto* field : {"mode", "referenceTime"}) {
    json = Json::parse(relative);
    json["layers"][0].erase(field);
    CHECK_FALSE(decodePlayerProperties(json.dump()));
  }

  json = Json::parse(relative);
  json["layers"][0]["mode"] = "unknown";
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  json["layers"][0]["mode"] = "override";
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  json = Json::parse(relative);
  json["layers"][0]["referenceTime"] = true;
  CHECK_FALSE(decodePlayerProperties(json.dump()));
  additive.layers[0].referenceTime = -1;
  CHECK_FALSE(validatePlayerProperties(additive));
  additive.layers[0].referenceTime = 0;
  additive.layers[0].mode = static_cast<LayerMode>(99);
  CHECK_FALSE(validatePlayerProperties(additive));
  CHECK(encodePlayerProperties(player).value() == encoded);
}

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

TEST_CASE(
    "animation.controller_reference_is_required_nullable_and_portable_in_current_scene_format") {
  using namespace Dot::Animation;
  using Json = nlohmann::json;
  PlayerProperties properties{{"assets/move.dotanimation"}, {{0, 1}}};
  auto encoded = Json::parse(encodePlayerProperties(properties).value());
  CHECK(encoded["controller"].is_null());
  encoded.erase("controller");
  CHECK_FALSE(decodePlayerProperties(encoded.dump()));
  properties.controller = "assets/人物.dotcontroller";
  CHECK(decodePlayerProperties(encodePlayerProperties(properties).value()).value() == properties);

  for (const auto& path : {"../bad.dotcontroller", "scripts/bad.dotcontroller", "assets/wrong.json",
                           "assets/../graph.dotcontroller", "assets/alias\\bad.dotcontroller",
                           "assets/CON.dotcontroller"}) {
    properties.controller = path;
    CHECK_FALSE(validatePlayerProperties(properties));
  }

  encoded["controller"] = false;
  CHECK_FALSE(decodePlayerProperties(encoded.dump()));
}
