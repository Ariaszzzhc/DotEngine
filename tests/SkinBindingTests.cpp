#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <span>
#include <thread>
#include <utility>
#include <vector>

import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Animation.SkinSource;
import Dot.Math;
import Dot.World.Scene;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto skin() -> Skin {
    return Skin::create({std::vector<Math::Mat4>(2), std::vector<SkinInfluence>(4)}).value();
  }

  struct Fixture {
    World::Scene scene{World::Scene::create().value()};
    World::EntityId mesh{scene.createEntity("Mesh").value()};
    World::EntityId root{scene.createEntity("Root").value()};
    World::EntityId tip{scene.createEntity("Tip").value()};
    World::SkinBinding properties{"assets/character.dotskin", {root.value, tip.value}};

    Fixture() {
      scene.setMeshRenderer(mesh, World::MeshRenderer{}).value();
      scene.setParent(tip, root).value();
      scene.setTransform(root, {{10, 0, 0}}).value();
      scene.setTransform(tip, {{0, 2, 0}}).value();
      scene.setTransform(mesh, {{3, 0, 0}}).value();
      scene.setSkin(mesh, properties).value();
    }
  };
}  // namespace

TEST_CASE("skin.codec_exact_little_endian_layout_owns_decoded_inputs") {
  auto bytes = encodeSkin(skin()).value();
  CHECK(bytes.size() == 16 + 2 * 64 + 4 * 64);
  const std::array header{0x44, 0x4f, 0x54, 0x53, 0x4b, 0x49, 0x4e, 0, 2, 0, 0, 0, 4, 0, 0, 0};

  for (std::size_t i = 0; i < header.size(); ++i) {
    CHECK(std::to_integer<int>(bytes[i]) == header[i]);
  }

  CHECK(bytes[16] == std::byte{0});
  CHECK(bytes[18] == std::byte{0x80});
  CHECK(bytes[19] == std::byte{0x3f});
  auto decoded = decodeSkin(bytes).value();
  CHECK(encodeSkin(decoded).value() == bytes);
  bytes.assign(bytes.size(), std::byte{0});
  CHECK(decoded.description().value()->inverseBindMatrices[0] == Math::Mat4{});
  CHECK_FALSE(encodeSkin(Skin{}));
}

TEST_CASE("skin.codec_rejects_every_truncation_trailing_counts_and_invalid_payload") {
  const auto bytes = encodeSkin(skin()).value();

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    CHECK_FALSE(decodeSkin(std::span(bytes).first(i)));
  }

  const auto word = [](auto& target, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
      target[offset + i] = static_cast<std::byte>((value >> (i * 8)) & 255U);
    }
  };

  for (const auto offset : {8U, 12U}) {
    for (const auto value : {0U, 1025U, std::numeric_limits<std::uint32_t>::max()}) {
      auto bad = bytes;
      word(bad, offset, value);
      CHECK_FALSE(decodeSkin(bad));
    }
  }

  for (const auto [offset, value] :
       std::array{std::pair{0U, 0U}, std::pair{16U, 0U}, std::pair{16U, 0x7fc00000U},
                  std::pair{144U, 2U}, std::pair{172U, 2U}, std::pair{176U, 0U},
                  std::pair{176U, 0x7f800000U}, std::pair{176U, 0xbf800000U}}) {
    auto bad = bytes;
    word(bad, offset, value);
    CHECK_FALSE(decodeSkin(bad));
  }

  auto trailing = bytes;
  trailing.push_back(std::byte{0});
  CHECK_FALSE(decodeSkin(trailing));
}

TEST_CASE("skin.source_strict_paths_canonical_ids_types_and_exact_joint_capacity") {
  using Json = nlohmann::json;
  SkinProperties properties{"assets/人物/骨架.dotskin", {3, 7}};
  const auto encoded = encodeSkinProperties(properties).value();
  CHECK(decodeSkinProperties(encoded).value() == properties);
  const auto json = Json::parse(encoded);

  for (const Json& value :
       {Json(3), Json(3.0), Json("03"), Json("0"), Json("-1"), Json("18446744073709551615")}) {
    auto bad = json;
    bad["joints"][0] = value;
    CHECK_FALSE(decodeSkinProperties(bad.dump()));
  }

  for (const auto* path : {"assets/../rig.dotskin", "assets/rig.json", "rig.dotskin",
                           "assets\\rig.dotskin", "/assets/rig.dotskin", "assets/NUL.dotskin"}) {
    auto bad = properties;
    bad.asset = path;
    CHECK_FALSE(encodeSkinProperties(bad));
  }

  for (const auto* source : {"", "{}", "[]", "null", "{\"asset\":1,\"asset\":2}", "\xc0\x80"}) {
    CHECK_FALSE(decodeSkinProperties(source));
  }

  auto bad = json;
  bad["extra"] = 1;
  CHECK_FALSE(decodeSkinProperties(bad.dump()));
  bad = json;
  bad["enabled"] = 1;
  CHECK_FALSE(decodeSkinProperties(bad.dump()));
  properties.joints = {3, 3};
  CHECK_FALSE(validateSkinProperties(properties));
  properties.joints.clear();
  CHECK_FALSE(validateSkinProperties(properties));

  for (std::uint64_t i = 1; i <= 1024; ++i) {
    properties.joints.push_back(i);
  }

  REQUIRE(decodeSkinProperties(encodeSkinProperties(properties).value()));
  properties.joints.push_back(1025);
  CHECK_FALSE(validateSkinProperties(properties));
}

TEST_CASE("skin.world_binding_references_mesh_deletion_and_mutations_are_guarded") {
  Fixture f;
  auto invalid = f.properties;
  invalid.joints[1] = 999;
  CHECK_FALSE(f.scene.setSkin(f.mesh, invalid));
  CHECK(f.scene.entity(f.mesh)->skin.value() == f.properties);
  CHECK_FALSE(f.scene.setSkin(f.tip, f.properties));
  CHECK_FALSE(f.scene.setMeshRenderer(f.mesh, std::nullopt));
  CHECK_FALSE(f.scene.destroyEntity(f.root));
  CHECK(f.scene.entity(f.tip));
  REQUIRE(f.scene.setSkin(f.mesh, std::nullopt));
  REQUIRE(f.scene.destroyEntity(f.root));
  REQUIRE(f.scene.setMeshRenderer(f.mesh, std::nullopt));
  CHECK_FALSE(f.scene.entity(f.root));
  CHECK_FALSE(f.scene.entity(f.tip));
}

TEST_CASE("skin.world_exact_binding_limit_replacement_subtree_and_owner_thread") {
  Fixture f;

  for (int i = 1; i < 128; ++i) {
    const auto entity = f.scene.createEntity().value();
    REQUIRE(f.scene.setMeshRenderer(entity, World::MeshRenderer{}));
    REQUIRE(f.scene.setSkin(entity, f.properties));
  }

  REQUIRE(f.scene.setSkin(f.mesh, f.properties));
  const auto excess = f.scene.createEntity().value();
  REQUIRE(f.scene.setMeshRenderer(excess, World::MeshRenderer{}));
  CHECK_FALSE(f.scene.setSkin(excess, f.properties));
  CHECK_FALSE(f.scene.entity(excess)->skin);
  bool rejected{};
  std::thread worker([&] { rejected = !f.scene.setSkin(f.mesh, std::nullopt); });
  worker.join();
  CHECK(rejected);
  auto subtree = World::Scene::create().value();
  const auto owner = subtree.createEntity().value();
  const auto joint = subtree.createEntity().value();
  REQUIRE(subtree.setMeshRenderer(owner, World::MeshRenderer{}));
  REQUIRE(subtree.setParent(joint, owner));
  REQUIRE(subtree.setSkin(owner, World::SkinBinding{"assets/rig.dotskin", {joint.value}}));
  REQUIRE(subtree.destroyEntity(owner));
  CHECK(subtree.entities()->empty());
}

TEST_CASE("skin.scene_palette_uses_current_hierarchy_and_owns_data_across_scene_moves") {
  Fixture f;
  auto original = skin();
  auto binding = SceneSkin::bind(f.scene, f.mesh, original).value();
  original = Skin{};
  const auto before = binding.palette(f.scene).value();
  CHECK(before[0].data[12] == 7);
  CHECK(before[1].data[13] == 2);
  REQUIRE(f.scene.setTransform(f.root, {{20, 0, 0}}));
  CHECK(binding.palette(f.scene)->at(1).data[12] == 17);
  CHECK(before[1].data[12] == 7);
  auto movedScene = std::move(f.scene);
  CHECK_FALSE(binding.palette(f.scene));
  CHECK(binding.palette(movedScene));
  auto moved = std::move(binding);
  CHECK_FALSE(binding.skin());
  CHECK_FALSE(binding.palette(movedScene));
  CHECK(moved.skin());
  CHECK(moved.palette(movedScene));
  bool rejected{};
  std::thread worker([&] { rejected = !moved.skin() && !moved.palette(movedScene); });
  worker.join();
  CHECK(rejected);
}

TEST_CASE("skin.scene_rejects_foreign_disabled_changed_deleted_and_mismatched_bindings") {
  Fixture f;
  CHECK_FALSE(SceneSkin::bind(f.scene, f.mesh, Skin{}));
  CHECK_FALSE(SceneSkin::bind(f.scene, f.root, skin()));
  auto binding = SceneSkin::bind(f.scene, f.mesh, skin()).value();
  Fixture foreign;
  CHECK_FALSE(binding.palette(foreign.scene));
  f.properties.enabled = false;
  REQUIRE(f.scene.setSkin(f.mesh, f.properties));
  CHECK_FALSE(binding.palette(f.scene));
  CHECK_FALSE(SceneSkin::bind(f.scene, f.mesh, skin()));
  f.properties.enabled = true;
  std::swap(f.properties.joints[0], f.properties.joints[1]);
  REQUIRE(f.scene.setSkin(f.mesh, f.properties));
  CHECK_FALSE(binding.palette(f.scene));
  f.properties.joints.pop_back();
  REQUIRE(f.scene.setSkin(f.mesh, f.properties));
  CHECK_FALSE(SceneSkin::bind(f.scene, f.mesh, skin()));
  REQUIRE(f.scene.destroyEntity(f.mesh));
  CHECK_FALSE(binding.palette(f.scene));
}
