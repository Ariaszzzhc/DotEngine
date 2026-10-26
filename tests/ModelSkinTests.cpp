#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

#include "ModelSkinFixture.hpp"

import Dot.Animation.SceneSkin;
import Dot.Animation.SkinFiles;
import Dot.Assets.Model;
import Dot.Assets.ModelBundle;
import Dot.Assets.Mesh;
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
  using DotTest::ModelSkin::accessor;
  using DotTest::ModelSkin::Json;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files = storage.files().value();
    Project::Project project = Project::Project::create(files, "game").value();

    Fixture() {
      std::filesystem::copy(DOT_MODEL_FIXTURES, files.root() / "source",
                            std::filesystem::copy_options::recursive);
    }

    auto source() const -> Core::FileSystem {
      return Core::FileSystem::open(files.root() / "source").value();
    }

    auto root() const -> Json {
      return Json::parse(source().readText("skinned.gltf").value());
    }

    auto load(const Json& json) const -> Core::Result<Assets::ModelAsset> {
      source().writeTextAtomic("variant.gltf", json.dump()).value();
      return Assets::loadModel(source(), "variant.gltf", "assets/model");
    }

    auto imported() const -> Assets::ModelAsset {
      return Assets::loadModel(source(), "skinned.gltf", "assets/model").value();
    }
  };

  auto matrices(bool reverse = false, std::size_t count = 8) -> std::vector<float> {
    std::vector<float> result;

    for (std::size_t i = 0; i < count; ++i) {
      Math::Mat4 matrix;
      matrix.data[13] = (reverse ? i == 7 : i == 0) ? 1.F : 0.F;
      result.insert(result.end(), matrix.data.begin(), matrix.data.end());
    }

    return result;
  }

  auto hasWarning(const Assets::ModelAsset& model, std::string_view text) -> bool {
    return std::ranges::any_of(
        model.warnings, [&](const auto& value) { return value.find(text) != std::string::npos; });
  }
}  // namespace

TEST_CASE("model.skin_gltf_glb_own_eight_weights_inverse_binds_and_nontraversal_joint_order") {
  for (const auto* name : {"skinned.gltf", "skinned.glb"}) {
    Fixture f;
    auto loaded = Assets::loadModel(f.source(), name, "assets/model");
    REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
    REQUIRE(loaded->skins.size() == 1);
    CHECK(loaded->nodes.size() == 10);
    CHECK(loaded->nodes[1].parts[0].skin == 0);
    CHECK(loaded->nodes[1].transform.position == Math::Vec3{7, 3, 1});
    CHECK(loaded->skins[0].joints == std::vector<std::uint32_t>{2, 3, 4, 5, 6, 7, 8, 9});
    const auto data = loaded->skins[0].data.description().value();
    REQUIRE(data->vertices.size() == 18);
    CHECK(data->inverseBindMatrices[0].data[13] == 1);
    CHECK(data->inverseBindMatrices[1] == Math::Mat4{});
    CHECK(data->vertices[8].weights[0] == doctest::Approx(.5));
    CHECK(data->vertices[8].weights[1] == doctest::Approx(.2));

    for (std::size_t i = 2; i < 8; ++i) {
      CHECK(data->vertices[8].weights[i] == doctest::Approx(.05));
      CHECK(data->vertices[8].joints[i] == i);
    }

    REQUIRE(loaded->animations.size() == 1);
    CHECK(loaded->animations[0].description().value()->tracks[0].node == 3);
    REQUIRE(f.source().writeTextAtomic(name, "source removed"));
    CHECK(loaded->skins[0].data.description().value() == data);
    CHECK(loaded->animations[0].sample(1));
  }
}

TEST_CASE("model.shared_indexed_accessors_compact_material_skin_vertices") {
  Fixture fixture;
  auto source = fixture.imported();
  const auto sourceSkin = source.skins[0].data.description().value();
  auto root = fixture.root();
  auto& primitives = root["meshes"][0]["primitives"];
  auto second = primitives[0];
  primitives[0]["indices"] = accessor(root, std::vector<std::uint16_t>{0, 1, 2}, 5123, "SCALAR", 1);
  second["indices"] = accessor(root, std::vector<std::uint16_t>{14, 15, 16}, 5123, "SCALAR", 1);
  root["materials"].push_back(root["materials"][0]);
  second["material"] = 1;
  primitives.push_back(std::move(second));
  auto loaded = fixture.load(root);
  REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
  REQUIRE(loaded->primitives.size() == 2);
  REQUIRE(loaded->skins.size() == 2);
  REQUIRE(loaded->nodes[1].parts.size() == 2);
  CHECK(loaded->primitives[0].material == 0);
  CHECK(loaded->primitives[1].material == 1);

  for (std::size_t part = 0; part < 2; ++part) {
    const auto& mesh = loaded->primitives[part].mesh;
    const auto& skin = loaded->skins[part].data.description().value()->vertices;
    REQUIRE(mesh.vertices.size() == 3);
    REQUIRE(skin.size() == 3);
    CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2});

    for (std::size_t vertex = 0; vertex < 3; ++vertex) {
      const auto original = part == 0 ? vertex : vertex + 14;
      CHECK(mesh.vertices[vertex].position
            == source.primitives[0].mesh.vertices[original].position);
      CHECK(skin[vertex].joints == sourceSkin->vertices[original].joints);
      CHECK(skin[vertex].weights == sourceSkin->vertices[original].weights);
    }
  }

  auto staging = Core::StagedDirectory::create(fixture.files).value();
  REQUIRE(Assets::writeModel(staging.files().value(), *loaded));
  CHECK(Assets::loadMesh(staging.files().value(), "meshes/0.dotmesh")->data.vertices.size() == 3);
  CHECK(Assets::loadMesh(staging.files().value(), "meshes/1.dotmesh")->data.vertices.size() == 3);
  auto savedSkin = Animation::loadSkin(staging.files().value(), "skins/0.dotskin");
  REQUIRE(savedSkin);
  CHECK(savedSkin->description().value()->vertices.size() == 3);

  auto flat = root;

  for (auto& primitive : flat["meshes"][0]["primitives"]) {
    primitive["attributes"].erase("NORMAL");
    primitive["attributes"].erase("TANGENT");
  }

  auto expanded = fixture.load(flat);
  REQUIRE_MESSAGE(expanded, (expanded ? "" : expanded.error().message));

  for (std::size_t part = 0; part < 2; ++part) {
    CHECK(expanded->primitives[part].mesh.vertices.size() == 3);
    CHECK(expanded->skins[part].data.description().value()->vertices.size() == 3);
  }

  root["meshes"][0]["primitives"][1]["indices"]
      = accessor(root, std::vector<std::uint16_t>{14, 18, 16}, 5123, "SCALAR", 1);
  CHECK_FALSE(fixture.load(root));
}

TEST_CASE("model.skin_four_weights_quantized_byte_short_and_mixed_sets_keep_normalization") {
  Fixture f;
  const auto model = f.imported();
  const auto data = model.skins[0].data.description().value();

  for (const int component : {5121, 5123, 0}) {
    auto root = f.root();
    std::array<std::vector<std::uint8_t>, 2> bytes;
    std::array<std::vector<std::uint16_t>, 2> shorts;

    for (const auto& influence : data->vertices) {
      std::array<unsigned, 8> raw;
      unsigned sum{};

      for (std::size_t i = 0; i < 8; ++i) {
        raw[i] = static_cast<unsigned>(std::floor(influence.weights[i] * 255.F));
        sum += raw[i];
      }

      raw[0] += 255 - sum;

      for (std::size_t i = 0; i < 8; ++i) {
        bytes[i / 4].push_back(static_cast<std::uint8_t>(raw[i]));
        shorts[i / 4].push_back(static_cast<std::uint16_t>(raw[i] * 257));
      }
    }

    auto& attributes = root["meshes"][0]["primitives"][0]["attributes"];
    attributes["WEIGHTS_0"] = component == 5123 ? accessor(root, shorts[0], 5123, "VEC4", 4, true)
                                                : accessor(root, bytes[0], 5121, "VEC4", 4, true);
    attributes["WEIGHTS_1"] = component == 5121 ? accessor(root, bytes[1], 5121, "VEC4", 4, true)
                                                : accessor(root, shorts[1], 5123, "VEC4", 4, true);
    const auto loaded = f.load(root);
    REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
    const auto actual = loaded->skins[0].data.description().value();

    for (const auto& influence : actual->vertices) {
      CHECK(std::accumulate(influence.weights.begin(), influence.weights.end(), 0.0)
            == doctest::Approx(1));
    }

    bytes[0][0] = 254;
    attributes["WEIGHTS_0"] = accessor(root, bytes[0], 5121, "VEC4", 4, true);
    CHECK_FALSE(f.load(root));
  }

  auto root = f.root();
  auto& attributes = root["meshes"][0]["primitives"][0]["attributes"];
  attributes.erase("JOINTS_1");
  attributes.erase("WEIGHTS_1");
  std::vector<float> values;

  for (std::size_t i = 0; i < 18; ++i) {
    const auto t = static_cast<float>(i / 2) / 8;
    values.insert(values.end(), {1 - t, t, 0, 0});
  }

  attributes["WEIGHTS_0"] = accessor(root, values, 5126, "VEC4", 4);
  REQUIRE(f.load(root));
  values[0] = .8F;
  attributes["WEIGHTS_0"] = accessor(root, values, 5126, "VEC4", 4);
  const auto renormalized = f.load(root).value();
  CHECK(hasWarning(renormalized, "Non-unit float"));
  CHECK(renormalized.skins[0].data.description().value()->vertices[0].weights[0] == 1);
}

TEST_CASE("model.skin_missing_and_extra_inverse_binds_follow_identity_and_joint_count_rules") {
  Fixture f;
  auto root = f.root();
  root["skins"][0].erase("inverseBindMatrices");
  auto model = f.load(root).value();

  for (const auto& matrix : model.skins[0].data.description().value()->inverseBindMatrices) {
    CHECK(matrix == Math::Mat4{});
  }

  root = f.root();
  root["skins"][0]["inverseBindMatrices"] = accessor(root, matrices(false, 9), 5126, "MAT4", 16);
  REQUIRE(f.load(root));
  CHECK(f.load(root)->skins[0].data.description().value()->inverseBindMatrices.size() == 8);

  for (const auto count : {7U, 1025U}) {
    root = f.root();
    root["skins"][0]["inverseBindMatrices"]
        = accessor(root, matrices(false, count), 5126, "MAT4", 16);
    CHECK_FALSE(f.load(root));
  }

  for (const auto [slot, value] :
       std::array{std::pair{0U, 0.F}, std::pair{3U, 1.F},
                  std::pair{0U, std::numeric_limits<float>::quiet_NaN()}}) {
    root = f.root();
    auto values = matrices();
    values[slot] = value;
    root["skins"][0]["inverseBindMatrices"] = accessor(root, values, 5126, "MAT4", 16);
    CHECK_FALSE(f.load(root));
  }
}

TEST_CASE("model.skin_strict_attribute_shapes_joint_hierarchies_and_zero_weight_indices_fail") {
  Fixture f;
  const auto root = f.root();
  const auto& original = root["meshes"][0]["primitives"][0]["attributes"];
  std::vector<Json> cases;

  for (const auto* key : {"JOINTS_0", "WEIGHTS_0", "JOINTS_1", "WEIGHTS_1"}) {
    auto bad = root;
    bad["meshes"][0]["primitives"][0]["attributes"].erase(key);
    cases.push_back(std::move(bad));
  }

  for (const auto* key : {"JOINTS_0", "WEIGHTS_0"}) {
    for (const auto* property : {"count", "type", "componentType", "normalized"}) {
      auto bad = root;
      auto& value = bad["accessors"][original[key].get<std::size_t>()][property];
      value = property == std::string_view("count")           ? Json(17)
              : property == std::string_view("type")          ? Json("VEC3")
              : property == std::string_view("componentType") ? Json(5122)
                                                              : Json(true);
      cases.push_back(std::move(bad));
    }
  }

  auto bad = root;
  bad["meshes"][0]["primitives"][0]["attributes"]["JOINTS_2"] = original["JOINTS_1"];
  cases.push_back(bad);
  bad = root;
  bad["skins"][0]["joints"][1] = 3;
  cases.push_back(bad);
  bad = root;
  bad["skins"][0]["joints"][1] = 99;
  cases.push_back(bad);
  bad = root;
  bad["skins"][0]["joints"][1] = 0.5;
  cases.push_back(bad);
  bad = root;
  bad["skins"][0]["skeleton"] = 0;
  cases.push_back(bad);
  bad = root;
  bad["nodes"][3]["children"] = {4, 5, 6, 7, 8, 9};
  bad["scenes"][0]["nodes"].push_back(0);
  cases.push_back(bad);
  bad = root;
  bad["nodes"][2]["children"] = {1};
  cases.push_back(bad);

  for (const auto& value : cases) {
    CHECK_FALSE(f.load(value));
  }

  for (const auto invalid : {8U, 65535U}) {
    bad = root;
    std::vector<std::uint16_t> indices(18 * 4);

    for (std::size_t i = 0; i < indices.size(); ++i) {
      indices[i] = static_cast<std::uint16_t>(i % 4);
    }

    indices[3] = static_cast<std::uint16_t>(invalid);
    bad["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"]
        = accessor(bad, indices, 5123, "VEC4", 4);
    CHECK_FALSE(f.load(bad));
  }

  for (const auto invalid : {0.F, -1.F, std::numeric_limits<float>::infinity()}) {
    bad = root;
    std::vector<float> values(18 * 4);
    values[0] = invalid;
    bad["meshes"][0]["primitives"][0]["attributes"].erase("JOINTS_1");
    bad["meshes"][0]["primitives"][0]["attributes"].erase("WEIGHTS_1");
    bad["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"]
        = accessor(bad, values, 5126, "VEC4", 4);
    CHECK_FALSE(f.load(bad));
  }
}

TEST_CASE("model.skin_flat_normal_expansion_remaps_each_triangle_corner_influence") {
  Fixture f;
  const auto original = f.imported();
  auto root = f.root();
  auto& attributes = root["meshes"][0]["primitives"][0]["attributes"];
  attributes.erase("NORMAL");
  attributes.erase("TANGENT");
  auto expanded = f.load(root).value();
  const auto& mesh = original.primitives[0].mesh;
  const auto originalData = original.skins[0].data.description().value();
  const auto actualData = expanded.skins[0].data.description().value();
  CHECK(expanded.primitives[0].mesh.indices.empty());
  REQUIRE(actualData->vertices.size() == mesh.indices.size());

  for (std::size_t i = 0; i < mesh.indices.size(); ++i) {
    const auto source = mesh.indices[i];
    CHECK(actualData->vertices[i].joints == originalData->vertices[source].joints);
    CHECK(actualData->vertices[i].weights == originalData->vertices[source].weights);
    CHECK(expanded.primitives[0].mesh.vertices[i].position == mesh.vertices[source].position);
  }
}

TEST_CASE(
    "model.skin_shared_mesh_instances_different_rigs_and_multiple_parts_publish_exact_assets") {
  Fixture f;
  auto root = f.root();
  root["meshes"][0]["primitives"].push_back(root["meshes"][0]["primitives"][0]);
  auto other = root["skins"][0];
  std::reverse(other["joints"].begin(), other["joints"].end());
  other["inverseBindMatrices"] = accessor(root, matrices(true), 5126, "MAT4", 16);
  root["skins"].push_back(other);
  root["nodes"].push_back({{"name", "Shared rig"}, {"mesh", 0}, {"skin", 0}});
  root["nodes"].push_back({{"name", "Reordered rig"}, {"mesh", 0}, {"skin", 1}});
  root["nodes"][2]["children"].push_back(10);
  root["nodes"][2]["children"].push_back(11);
  const auto model = f.load(root).value();
  CHECK(model.skins.size() == 4);
  CHECK(model.nodes[1].parts[0].skin == model.nodes[10].parts[0].skin);
  CHECK(model.nodes[1].parts[0].skin != model.nodes[11].parts[0].skin);
  auto store = Editor::ResourceStore::open(f.project).value();
  const auto before = store.list()->size();
  auto report = store.importModel(f.source().root() / "variant.gltf", "assets/model");
  REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
  CHECK(report->skins == 4);
  CHECK(report->skinnedMeshes == 6);
  CHECK(store.list()->size() == before + 17);
  auto loaded = Assets::loadModelBundle(f.project.files().value(), report->model);
  REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
  unsigned owners{};

  for (const auto& entity : loaded->scene.entities().value()) {
    if (!entity.skin) {
      continue;
    }

    ++owners;
    auto data = Animation::loadSkin(f.project.files().value(), entity.skin->asset).value();
    auto bound = Animation::SceneSkin::bind(loaded->scene, entity.id, data).value();
    CHECK(bound.palette(loaded->scene));
  }

  CHECK(owners == 6);
  Editor::ResourceLimits limits;
  limits.entries = before + 16;
  auto smallProject = Project::Project::create(f.files, "small").value();
  auto shortStore = Editor::ResourceStore::open(smallProject, limits).value();
  CHECK_FALSE(shortStore.importModel(f.source().root() / "variant.gltf", "assets/rejected"));
  CHECK_FALSE(std::filesystem::exists(smallProject.files()->root() / "assets/rejected"));
  limits.entries = before + 17;
  auto exactStore = Editor::ResourceStore::open(smallProject, limits).value();
  REQUIRE(exactStore.importModel(f.source().root() / "variant.gltf", "assets/exact"));
}

TEST_CASE(
    "model.skin_prepared_validation_and_bundle_foreign_or_missing_assets_fail_without_publish") {
  Fixture f;
  const auto model = f.imported();
  auto stage = Core::StagedDirectory::create(f.files).value();
  const auto invalid = [&](const Assets::ModelAsset& value) {
    CHECK_FALSE(Assets::writeModel(stage.files().value(), value));
    CHECK(std::filesystem::is_empty(stage.files()->root()));
  };
  auto bad = model;
  bad.skins[0].joints[1] = bad.skins[0].joints[0];
  invalid(bad);
  bad = model;
  bad.skins[0].joints[0] = 99;
  invalid(bad);
  bad = model;
  bad.nodes[1].parts[0].skin = 99;
  invalid(bad);
  bad = model;
  bad.nodes[1].parts[0].skin.reset();
  invalid(bad);
  bad = model;
  bad.skins[0].data = Animation::Skin{};
  invalid(bad);
  auto store = Editor::ResourceStore::open(f.project).value();
  const auto report = store.importModel(f.source().root() / "skinned.glb", "assets/model").value();
  REQUIRE(Assets::loadModelBundle(f.project.files().value(), report.model));
  auto scene = World::loadScene(f.project.files().value(), report.scene).value();
  const auto entities = scene.entities().value();
  auto owner
      = *std::ranges::find_if(entities, [](const auto& entity) { return entity.skin.has_value(); });
  owner.skin->asset = "assets/foreign.dotskin";
  REQUIRE(scene.setSkin(owner.id, owner.skin));
  REQUIRE(World::saveScene(f.project.files().value(), report.scene, scene));
  CHECK_FALSE(Assets::loadModelBundle(f.project.files().value(), report.model));
  owner.skin->asset = "assets/model/skins/0.dotskin";
  REQUIRE(scene.setSkin(owner.id, owner.skin));
  REQUIRE(World::saveScene(f.project.files().value(), report.scene, scene));
  REQUIRE(f.project.files()->writeTextAtomic(owner.skin->asset, "broken"));
  CHECK_FALSE(Assets::loadModelBundle(f.project.files().value(), report.model));
}

TEST_CASE("model.skin_sparse_and_strided_joint_attributes_preserve_accessor_values") {
  Fixture f;
  auto root = f.root();
  const auto joint
      = root["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"].get<std::size_t>();
  const auto indices = accessor(root, std::vector<std::uint8_t>{0}, 5121, "SCALAR", 1);
  const auto values = accessor(root, std::vector<std::uint8_t>{0, 1, 2, 7}, 5121, "VEC4", 4);
  root["accessors"][joint]["sparse"]
      = {{"count", 1},
         {"indices",
          {{"bufferView", root["accessors"][indices]["bufferView"]}, {"componentType", 5121}}},
         {"values", {{"bufferView", root["accessors"][values]["bufferView"]}}}};
  auto sparse = f.load(root);
  REQUIRE_MESSAGE(sparse, (sparse ? "" : sparse.error().message));
  CHECK(sparse->skins[0].data.description().value()->vertices[0].joints[3] == 7);
  root = f.root();
  std::vector<std::uint8_t> interleaved(18 * 16, 255);

  for (std::size_t vertex = 0; vertex < 18; ++vertex) {
    for (std::size_t slot = 0; slot < 4; ++slot) {
      interleaved[vertex * 16 + slot] = static_cast<std::uint8_t>(slot);
    }
  }

  const auto strided = accessor(root, interleaved, 5121, "VEC4", 4);
  root["accessors"][strided]["count"] = 18;
  root["bufferViews"][root["accessors"][strided]["bufferView"].get<std::size_t>()]["byteStride"]
      = 16;
  root["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = strided;
  const auto loaded = f.load(root).value();
  CHECK(loaded.skins[0].data.description().value()->vertices[8].joints
        == std::array<std::uint32_t, 8>{0, 1, 2, 3, 4, 5, 6, 7});
}

TEST_CASE("model.skin_exact_owner_capacity_reuses_one_asset_and_rejects_overflow") {
  Fixture f;
  auto root = f.root();

  for (unsigned i = 1; i < 128; ++i) {
    const auto index = root["nodes"].size();
    root["nodes"].push_back({{"mesh", 0}, {"skin", 0}});
    root["nodes"][2]["children"].push_back(index);
  }

  const auto model = f.load(root).value();
  CHECK(model.skins.size() == 1);
  auto stage = Core::StagedDirectory::create(f.files).value();
  REQUIRE(Assets::writeModel(stage.files().value(), model));
  const auto index = root["nodes"].size();
  root["nodes"].push_back({{"mesh", 0}, {"skin", 0}});
  root["nodes"][2]["children"].push_back(index);
  CHECK_FALSE(f.load(root));
  root = f.root();
  const auto skin = root["skins"][0];

  for (unsigned i = 1; i <= 1024; ++i) {
    root["skins"].push_back(skin);
  }

  CHECK_FALSE(f.load(root));
}
