#include <doctest/doctest.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Math;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace {
  using namespace Dot;

  auto storage() -> Core::StagedDirectory {
    return Core::StagedDirectory::create(
               Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
        .value();
  }

  auto skin() -> Animation::Skin {
    return Animation::Skin::create(
               {std::vector<Math::Mat4>(1), std::vector<Animation::SkinInfluence>(4)})
        .value();
  }
}  // namespace

TEST_CASE("skin.files_atomic_unicode_roundtrip_rejects_links_missing_and_special_paths") {
  auto staging = storage();
  auto files = staging.files().value();
  const std::filesystem::path path{u8"assets/人物/骨架.dotskin"};
  REQUIRE(files.createDirectories(path.parent_path()));
  REQUIRE(Animation::saveSkin(files, path, skin()));
  const auto before = files.readBytes(path).value();
  auto loaded = Animation::loadSkin(files, path).value();
  CHECK(Animation::encodeSkin(loaded).value() == before);
  CHECK_FALSE(Animation::saveSkin(files, path, Animation::Skin{}));
  CHECK(files.readBytes(path).value() == before);
  CHECK_FALSE(Animation::saveSkin(files, "absent/rig.dotskin", skin()));
  CHECK_FALSE(Animation::saveSkin(files, "../rig.dotskin", skin()));
  CHECK_FALSE(Animation::saveSkin(files, "rig.json", skin()));
  CHECK_FALSE(Animation::loadSkin(files, "missing.dotskin"));
  REQUIRE(files.createDirectories("directory.dotskin"));
  CHECK_FALSE(Animation::loadSkin(files, "directory.dotskin"));
  CHECK_FALSE(Animation::saveSkin(files, "directory.dotskin", skin()));
  std::error_code error;
  std::filesystem::create_symlink(files.root() / path, files.root() / "link.dotskin", error);
  REQUIRE_FALSE(error);
  CHECK_FALSE(Animation::loadSkin(files, "link.dotskin"));
  CHECK_FALSE(Animation::saveSkin(files, "link.dotskin", skin()));
  std::filesystem::create_directory_symlink(files.root() / path.parent_path(),
                                            files.root() / "linked", error);
  REQUIRE_FALSE(error);
  CHECK_FALSE(Animation::saveSkin(files, "linked/new.dotskin", skin()));
  REQUIRE(files.writeTextAtomic("corrupt.dotskin", "bad"));
  const auto corrupt = Animation::loadSkin(files, "corrupt.dotskin");
  REQUIRE_FALSE(corrupt);
  CHECK(corrupt.error().message.find("corrupt.dotskin") != std::string::npos);
  REQUIRE(std::filesystem::remove(files.root() / path));
  CHECK(Animation::encodeSkin(loaded).value() == before);
}

TEST_CASE("skin.scene_files_preserve_order_rescope_and_reject_dangling_or_unmeshed_bindings") {
  auto scene = World::Scene::create().value();
  const auto mesh = scene.createEntity("Mesh").value();
  const auto joint = scene.createEntity("Joint").value();
  REQUIRE(scene.setMeshRenderer(mesh, World::MeshRenderer{}));
  REQUIRE(scene.setSkin(mesh, World::SkinBinding{"assets/rig.dotskin", {joint.value}}));
  const auto encoded = World::encodeScene(scene).value();
  auto clone = World::decodeScene(encoded).value();
  const World::EntityId cloned{clone.id(), mesh.value};
  CHECK(clone.id() != scene.id());
  CHECK(clone.entity(cloned)->skin == scene.entity(mesh)->skin);
  CHECK(World::encodeScene(clone).value() == encoded);
  auto binding = Animation::SceneSkin::bind(clone, cloned, skin()).value();
  CHECK(binding.palette(clone));
  CHECK_FALSE(binding.palette(scene));
  const auto json = nlohmann::json::parse(encoded);
  auto bad = json;
  bad["entities"][0]["skin"]["joints"][0] = "999";
  CHECK_FALSE(World::decodeScene(bad.dump()));
  bad = json;
  bad["entities"][0]["mesh"] = nullptr;
  CHECK_FALSE(World::decodeScene(bad.dump()));
  CHECK(World::encodeScene(scene).value() == encoded);
  auto staging = storage();
  REQUIRE(World::saveScene(staging.files().value(), "rig.dotscene", scene));
  CHECK(World::loadScene(staging.files().value(), "rig.dotscene"));
}
