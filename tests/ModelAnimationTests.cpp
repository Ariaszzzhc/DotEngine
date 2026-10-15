#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Animation.SceneAnimator;
import Dot.Assets.Model;
import Dot.Assets.ModelBundle;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.Resources;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Math;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files = storage.files().value();
    Project::Project project = Project::Project::create(files, "game").value();
    Editor::ResourceStore resources = Editor::ResourceStore::open(project).value();

    Fixture() {
      std::filesystem::copy(DOT_MODEL_FIXTURES, files.root() / "source",
                            std::filesystem::copy_options::recursive);
    }

    auto source() const -> Core::FileSystem {
      return Core::FileSystem::open(files.root() / "source").value();
    }

    auto root() const -> Json {
      return Json::parse(source().readText("animated.gltf").value());
    }

    auto load(const Json& value) const -> Core::Result<Assets::ModelAsset> {
      source().writeTextAtomic("variant.gltf", value.dump()).value();
      return Assets::loadModel(source(), "variant.gltf", "assets/model");
    }

    auto publish(bool interleave = false) -> void {
      auto input = source().root() / "animated.glb";

      if (interleave) {
        auto variant = root();
        variant["nodes"][2]["mesh"] = 0;
        REQUIRE(source().writeTextAtomic("interleaved.gltf", variant.dump()));
        input = source().root() / "interleaved.gltf";
      }

      const auto report = resources.importModel(input, "assets/model");
      REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
      CHECK(report->animations == 1);
      CHECK(report->model == "assets/model/model.dotmodel");
    }
  };
}  // namespace

TEST_CASE("model.animation_gltf_glb_remap_owned_trs_and_filter_other_scenes") {
  for (const auto* name : {"animated.gltf", "animated.glb"}) {
    Fixture fixture;
    auto model = Assets::loadModel(fixture.source(), name, "assets/model");
    REQUIRE_MESSAGE(model, (model ? "" : model.error().message));
    REQUIRE(model->animations.size() == 1);
    const auto& clip = model->animations[0];
    CHECK(clip.description().value()->name == "机械展开");
    CHECK(clip.duration().value() == 2);
    const auto samples = clip.sample(1).value();
    REQUIRE(samples.size() == 3);
    CHECK(samples[0].node == 0);
    CHECK(samples[0].value.x == doctest::Approx(.85));
    CHECK(samples[1].node == 1);
    CHECK(samples[1].value.z == doctest::Approx(std::sin(.125 * 3.141592653589793)));
    CHECK(samples[2].value.x == -.5F);
    CHECK(samples[2].value.y == 2);
    CHECK(model->nodes[1].transform.scale.x == -1);
    CHECK(model->nodes[1].transform.scale.y == 1.5F);
    REQUIRE(model->warnings.size() >= 2);
    CHECK(model->warnings.back().find("outside selected scene") != std::string::npos);
    REQUIRE(fixture.source().writeTextAtomic(name, "source removed"));
    CHECK(clip.sample(1)->at(0).value == samples[0].value);
  }

  Fixture fixture;
  auto other = Assets::loadModel(fixture.source(), "animated.gltf", "assets/other", 1);
  REQUIRE(other);
  REQUIRE(other->animations.size() == 1);
  CHECK(other->animations[0].description().value()->name == "Animation 1");
  CHECK(other->animations[0].sample(1)->at(0).node == 0);
  CHECK(other->animations[0].sample(1)->at(0).value.x == 1);
}

TEST_CASE("model.animation_rejects_invalid_sampler_channel_shapes_and_unsupported_semantics") {
  Fixture fixture;
  const auto root = fixture.root();
  const auto input = root["animations"][0]["samplers"][0]["input"].get<std::size_t>();
  const auto output = root["animations"][0]["samplers"][0]["output"].get<std::size_t>();
  std::vector<Json> variants;
  auto bad = root;
  bad["animations"][0]["channels"].push_back(bad["animations"][0]["channels"][0]);
  variants.push_back(bad);
  bad = root;
  bad["animations"][0]["channels"][0]["target"]["node"] = 2.5;
  variants.push_back(bad);
  bad = root;
  bad["animations"][0]["channels"][0]["target"]["path"] = "weights";
  variants.push_back(bad);
  bad = root;
  bad["animations"][0]["channels"][0]["target"].erase("node");
  variants.push_back(bad);
  bad = root;
  bad["animations"][0]["samplers"][0]["interpolation"] = "bad";
  variants.push_back(bad);
  bad = root;
  bad["animations"][0]["samplers"][0]["input"] = -1;
  variants.push_back(bad);
  bad = root;
  bad["accessors"][input].erase("min");
  variants.push_back(bad);
  bad = root;
  bad["accessors"][input]["max"] = {3};
  variants.push_back(bad);
  bad = root;
  bad["accessors"][input]["min"] = Json::array();
  variants.push_back(bad);
  bad = root;
  bad["accessors"][input]["min"] = {0, 1};
  variants.push_back(bad);
  bad = root;
  bad["accessors"][input]["normalized"] = true;
  variants.push_back(bad);
  bad = root;
  bad["accessors"][input]["type"] = "VEC2";
  variants.push_back(bad);
  bad = root;
  bad["accessors"][output]["count"] = 2;
  variants.push_back(bad);
  bad = root;
  bad["accessors"][output]["componentType"] = 5123;
  variants.push_back(bad);
  bad = root;
  bad["nodes"][2].erase("translation");
  bad["nodes"][2]["matrix"] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  variants.push_back(bad);
  bad = root;
  bad["animations"][0]["channels"] = Json::array();
  variants.push_back(bad);
  bad = root;
  bad["animations"] = Json::array();

  for (int i = 0; i < 1025; ++i) {
    bad["animations"].push_back(root["animations"][0]);
  }

  variants.push_back(std::move(bad));

  for (const auto& variant : variants) {
    CAPTURE(variant);
    CHECK_FALSE(fixture.load(variant));
  }

  auto excluded = root;
  excluded["animations"][1]["channels"].push_back(excluded["animations"][1]["channels"][0]);
  CHECK_FALSE(fixture.load(excluded));
}

TEST_CASE("model.animation_rejects_binary_nonfinite_unordered_and_singular_keys") {
  Fixture fixture;
  auto root = fixture.root();
  const auto glb = fixture.source().readBytes("animated.glb").value();
  std::uint32_t jsonBytes{};

  for (unsigned i = 0; i < 4; ++i) {
    jsonBytes |= std::to_integer<std::uint32_t>(glb[12 + i]) << (i * 8);
  }

  const auto first = glb.begin() + 28 + jsonBytes;
  const std::vector<std::byte> binary(first, first + root["buffers"][0]["byteLength"].get<int>());
  root["buffers"][0]["uri"] = "animation.bin";
  const auto edit = [&](std::size_t accessor, std::size_t component, float value) {
    auto bytes = binary;
    const auto view = root["accessors"][accessor]["bufferView"].get<std::size_t>();
    const auto offset = root["bufferViews"][view]["byteOffset"].get<std::size_t>() + component * 4;
    const auto bits = std::bit_cast<std::uint32_t>(value);

    for (unsigned i = 0; i < 4; ++i) {
      bytes[offset + i] = static_cast<std::byte>(bits >> (i * 8));
    }

    REQUIRE(fixture.source().writeAtomic("animation.bin", bytes));
    CHECK_FALSE(fixture.load(root));
  };
  const auto& samplers = root["animations"][0]["samplers"];
  edit(samplers[0]["input"], 1, 0);
  edit(samplers[0]["output"], 3, std::numeric_limits<float>::infinity());
  edit(samplers[1]["output"], 3, 0);
  edit(samplers[2]["output"], 0, 0);
  const auto& other = root["animations"][1]["samplers"][0];
  edit(other["output"], 0, std::numeric_limits<float>::quiet_NaN());
}

TEST_CASE("model.animation_bundle_reopens_live_bindings_moves_and_plays_without_source") {
  Fixture fixture;
  fixture.publish(true);
  auto native
      = Assets::loadModelBundle(fixture.project.files().value(), "assets/model/model.dotmodel");
  REQUIRE_MESSAGE(native, (native ? "" : native.error().message));
  REQUIRE(native->animations.size() == 1);
  REQUIRE(native->bindings.size() == 2);
  CHECK(native->bindings[0].entity.value == 1);
  CHECK(native->bindings[1].entity.value == 3);
  CHECK(native->scene.entityCount().value() == 4);
  auto moved = std::move(*native);
  std::filesystem::remove_all(fixture.source().root());
  auto animator
      = Animation::SceneAnimator::bind(moved.scene, moved.animations[0], moved.bindings).value();
  REQUIRE(animator.seek(moved.scene, 1));
  CHECK(moved.scene.transform(moved.bindings[0].entity)->position.x == doctest::Approx(.85));
  CHECK(moved.scene.transform(moved.bindings[1].entity)->scale.x == -.5F);
  REQUIRE(animator.stop(moved.scene));
  CHECK(moved.scene.transform(moved.bindings[0].entity)->position.x == doctest::Approx(.2));
  CHECK(moved.scene.transform(moved.bindings[1].entity)->scale.x == -1);
  CHECK(Assets::loadModelBundle(fixture.project.files().value(), "assets/model/model.dotmodel"));
  const auto entries = fixture.resources.list().value();

  for (const auto* suffix : {"model.dotmodel", "animations/0.dotanimation"}) {
    const auto found = std::ranges::find(entries, "assets/model/" + std::string(suffix),
                                         &Editor::ResourceEntry::path);
    REQUIRE(found != entries.end());
    CHECK(found->text);
  }
}

TEST_CASE("model.animation_bundle_rejects_malformed_foreign_missing_and_linked_resources") {
  Fixture fixture;
  fixture.publish();
  const auto files = fixture.project.files().value();
  const auto descriptor = files.readText("assets/model/model.dotmodel").value();
  const auto root = Json::parse(descriptor);
  const auto check = [&](const Json& value) {
    REQUIRE(files.writeTextAtomic("assets/model/model.dotmodel", value.dump()));
    CHECK_FALSE(Assets::loadModelBundle(files, "assets/model/model.dotmodel"));
  };
  auto bad = root;
  bad["extra"] = true;
  check(bad);
  bad = root;
  bad["scene"] = "assets/elsewhere/model.dotscene";
  check(bad);
  bad = root;
  bad["nodes"][0] = 1;
  check(bad);
  bad = root;
  bad["nodes"][0] = "01";
  check(bad);
  bad = root;
  bad["nodes"][1] = "1";
  check(bad);
  bad = root;
  bad["nodes"][1] = "99";
  check(bad);
  bad = root;
  bad["animations"][0] = "../other.dotanimation";
  check(bad);
  REQUIRE(files.writeTextAtomic("assets/model/model.dotmodel", descriptor));
  CHECK_FALSE(Assets::loadModelBundle(files, "../model.dotmodel"));
  CHECK_FALSE(Assets::loadModelBundle(files, "assets/model/model.dotscene"));
  const auto clip = Animation::loadClip(files, "assets/model/animations/0.dotanimation").value();
  auto description = *clip.description().value();
  description.tracks[0].node = 99;
  REQUIRE(Animation::saveClip(files, "assets/model/animations/0.dotanimation",
                              Animation::Clip::create(description).value()));
  CHECK_FALSE(Assets::loadModelBundle(files, "assets/model/model.dotmodel"));
  REQUIRE(Animation::saveClip(files, "assets/model/animations/0.dotanimation", clip));
  std::filesystem::rename(files.root() / "assets/model/animations/0.dotanimation",
                          files.root() / "assets/model/animations/original.dotanimation");
  std::filesystem::create_symlink("original.dotanimation",
                                  files.root() / "assets/model/animations/0.dotanimation");
  CHECK_FALSE(Assets::loadModelBundle(files, "assets/model/model.dotmodel"));
  std::filesystem::remove(files.root() / "assets/model/animations/0.dotanimation");
  CHECK_FALSE(Assets::loadModelBundle(files, "assets/model/model.dotmodel"));
}

TEST_CASE("model.animation_failed_publication_preserves_project_and_staging") {
  Fixture fixture;
  auto bad = fixture.root();
  bad["animations"][0]["channels"][0]["target"]["node"] = 99;
  REQUIRE(fixture.source().writeTextAtomic("bad.gltf", bad.dump()));
  const auto baseline = fixture.resources.list()->size();
  CHECK_FALSE(fixture.resources.importModel(fixture.source().root() / "bad.gltf", "assets/bad"));
  CHECK(fixture.resources.list()->size() == baseline);
  CHECK_FALSE(std::filesystem::exists(fixture.project.files()->root() / "assets/bad"));
  auto model = Assets::loadModel(fixture.source(), "animated.gltf", "assets/model").value();
  auto description = *model.animations[0].description().value();
  description.tracks[0].node = 99;
  model.animations[0] = Animation::Clip::create(description).value();
  auto staging = Core::StagedDirectory::create(fixture.files).value();
  CHECK_FALSE(Assets::writeModel(staging.files().value(), model));
  CHECK(std::filesystem::is_empty(staging.files()->root()));
  Editor::ResourceLimits limits;
  limits.entries = baseline + 13;
  auto limited = Editor::ResourceStore::open(fixture.project, limits).value();
  CHECK_FALSE(limited.importModel(fixture.source().root() / "animated.glb", "assets/limited"));
  CHECK_FALSE(std::filesystem::exists(fixture.project.files()->root() / "assets/limited"));
  limits.entries = baseline + 14;
  auto exact = Editor::ResourceStore::open(fixture.project, limits).value();
  REQUIRE(exact.importModel(fixture.source().root() / "animated.glb", "assets/exact"));
  CHECK(exact.list()->size() == limits.entries);
  fixture.publish();
  const auto before = fixture.project.files()->readText("assets/model/model.dotmodel").value();
  CHECK_FALSE(
      fixture.resources.importModel(fixture.source().root() / "animated.gltf", "assets/model"));
  CHECK(fixture.project.files()->readText("assets/model/model.dotmodel").value() == before);
}
