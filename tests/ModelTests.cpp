#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>

import Dot.Assets.Model;
import Dot.Assets.Mesh;
import Dot.Assets.Image;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Editor.Resources;
import Dot.Editor.ModelReimport;
import Dot.Editor.Document;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Math;
import Dot.Graphics.Renderer;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files = storage.files().value();

    Fixture() {
      std::filesystem::copy(DOT_MODEL_FIXTURES, files.root() / "source",
                            std::filesystem::copy_options::recursive);
    }

    auto source() const -> Core::FileSystem {
      return Core::FileSystem::open(files.root() / "source").value();
    }
  };

  auto wait(Editor::ModelReimport& review) -> Editor::ReimportStatus {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    auto state = review.status(0, 256).value();

    while ((state.state == Editor::ReimportState::Preparing
            || state.state == Editor::ReimportState::Validating)
           && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      state = review.status(0, 256).value();
    }

    REQUIRE(state.state != Editor::ReimportState::Preparing);
    REQUIRE(state.state != Editor::ReimportState::Validating);
    return state;
  }

  auto chooseAll(Editor::ModelReimport& review, Editor::ReimportChoice choice)
      -> Editor::ReimportStatus {
    auto state = review.status(0, 256).value();
    const auto files = state.changes;

    for (const auto& file : files) {
      REQUIRE(review.choose(file.path, file.added ? Editor::ReimportChoice::Source : choice,
                            state.revision));
      state = review.status(0, 256).value();
    }

    return state;
  }

}  // namespace

TEST_CASE("model.glb_and_external_gltf_preserve_frames_pbr_images_and_hierarchy") {
  Fixture fixture;
  for (const auto* source : {"surface.glb", "surface.gltf"}) {
    auto result = Assets::loadModel(fixture.source(), source, "assets/model");
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    const auto& model = *result;
    REQUIRE(model.primitives.size() == 1);
    CHECK(model.materials.size() == 2);
    CHECK(model.images.size() == 3);
    CHECK(model.nodes.size() == 2);
    CHECK(model.nodes[1].parent == 0);
    CHECK(model.nodes[1].transform.scale.x == -1);
    CHECK(model.nodes[0].transform.position.x == doctest::Approx(.2));
    CHECK(model.primitives[0].mesh.vertices.size() == 4);
    CHECK(model.primitives[0].mesh.vertices[0].tangent == Math::Vec4{1, 0, 0, -1});
    CHECK(model.primitives[0].mesh.vertices[0].color.w == .25F);
    CHECK(model.materials[0].baseColor.w == doctest::Approx(.2));
    CHECK(model.materials[0].occlusionStrength == doctest::Approx(.8));
    CHECK(model.materials[0].occlusionTexture->path == "assets/model/images/1.png");
    CHECK(model.materials[0].baseColorTexture->sampling.colorSpace == Graphics::ColorSpace::Srgb);
    CHECK(model.materials[0].normalTexture->sampling.colorSpace == Graphics::ColorSpace::Linear);
    CHECK(Assets::decodeImage(model.images[0].png)->pixels[3] == 64);
    CHECK(Assets::decodeImage(model.images[0].png)->pixels[0] == 188);
    auto staging = Core::StagedDirectory::create(fixture.files).value();
    REQUIRE(Assets::writeModel(staging.files().value(), model));
    CHECK(World::loadScene(staging.files().value(), "model.dotscene")->entities()->size() == 3);
    CHECK(Assets::loadMesh(staging.files().value(), "meshes/0.dotmesh")->data.vertices[0].tangent.w
          == -1);
  }
}

TEST_CASE("model.signed_normal_pixels_survive_import_without_clamping_or_replacement") {
  Fixture fixture;
  auto source = fixture.source();
  const auto document = Json::parse(source.readText("surface.gltf").value());
  const auto texture = document["materials"][0]["normalTexture"]["index"].get<std::size_t>();
  const auto image = document["textures"][texture]["source"].get<std::size_t>();
  const auto path = document["images"][image]["uri"].get<std::string>();
  const Graphics::Image expected{2, 1, {119, 214, 120, 255, 128, 128, 0, 255}};
  REQUIRE(source.writeAtomic(path, Assets::encodeImage(expected).value()));
  auto model = Assets::loadModel(source, "surface.gltf", "assets/model");
  REQUIRE_MESSAGE(model, (model ? "" : model.error().message));
  CHECK(Assets::decodeImage(model->images[image].png)->pixels == expected.pixels);
  CHECK(model->materials[0].normalTexture->sampling.colorSpace == Graphics::ColorSpace::Linear);
}

TEST_CASE("model.invalid_ranges_uris_cycles_extensions_and_capacities_fail") {
  Fixture fixture;
  const auto original = Json::parse(fixture.source().readText("surface.gltf").value());
  std::vector<Json> cases;
  auto bad = original;
  bad["accessors"][0]["count"] = 0;
  cases.push_back(bad);
  bad = original;
  bad["accessors"][0]["byteOffset"] = 18446744073709551615ULL;
  cases.push_back(bad);
  bad = original;
  bad["buffers"][0]["byteLength"] = 67108865;
  cases.push_back(bad);
  bad = original;
  bad["buffers"][0]["uri"] = "../surface.bin";
  cases.push_back(bad);
  bad = original;
  bad["images"][0]["uri"] = "https://example.com/image.png";
  cases.push_back(bad);
  bad = original;
  bad["images"][0]["uri"] = "%2e%2e/base.png";
  cases.push_back(bad);
  bad = original;
  bad["nodes"][1]["children"] = {0};
  cases.push_back(bad);
  bad = original;
  bad["materials"][0]["alphaMode"] = "BAD";
  cases.push_back(bad);
  bad = original;
  bad["extensionsRequired"] = {"UNKNOWN_required"};
  cases.push_back(bad);
  bad = original;
  bad["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"] = 4;
  cases.push_back(bad);
  bad = original;
  bad["nodes"][0]["scale"] = {0, 1, 1};
  cases.push_back(bad);
  bad = original;
  bad["materials"][0]["normalTexture"]["texCoord"] = 1;
  cases.push_back(bad);
  bad = original;
  bad["materials"][0]["normalTexture"] = Json::object();
  cases.push_back(bad);
  bad = original;
  bad["materials"][0]["pbrMetallicRoughness"]["metallicFactor"] = "bad";
  cases.push_back(bad);
  bad = original;
  bad["accessors"][0]["count"] = 4.5;
  cases.push_back(bad);
  for (const auto& value : cases) {
    REQUIRE(fixture.source().writeTextAtomic("bad.gltf", value.dump()));
    CHECK_FALSE(Assets::loadModel(fixture.source(), "bad.gltf", "assets/model"));
  }
  CHECK_FALSE(Assets::loadModel(fixture.source(), "surface.glb", "../model"));
  CHECK_FALSE(Assets::loadModel(fixture.source(), "surface.glb", "assets/model", 2));
  auto bytes = fixture.source().readBytes("surface.glb").value();
  bytes.pop_back();
  REQUIRE(fixture.source().writeAtomic("bad.glb", bytes));
  CHECK_FALSE(Assets::loadModel(fixture.source(), "bad.glb", "assets/model"));
}

TEST_CASE("model.transactional_bundle_publication_and_single_history_instantiation") {
  Fixture fixture;
  auto project = Project::Project::create(fixture.files, "project").value();
  auto resources = Editor::ResourceStore::open(project).value();
  auto report = resources.importModel(fixture.source().root() / "surface.glb", "assets/model");
  REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
  CHECK(report->scene == "assets/model/model.dotscene");
  const auto bytes = project.files()->readBytes("assets/model/meshes/0.dotmesh").value();
  CHECK_FALSE(resources.importModel(fixture.source().root() / "surface.glb", "assets/model"));
  CHECK(project.files()->readBytes("assets/model/meshes/0.dotmesh").value() == bytes);
  auto document = Editor::Document::open(project).value();
  const auto before = Json::parse(document.snapshot().value());
  const auto revision = document.stats()->revision;
  const auto operation
      = Json::array(
            {{{"op", "instantiate-scene"},
              {"path", "assets/model/model.dotscene"},
              {"parent", nullptr},
              {"transform",
               {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}}}})
            .dump();
  auto edited = document.apply(operation, revision);
  REQUIRE_MESSAGE(edited, (edited ? "" : edited.error().message));
  CHECK(edited->created.size() == 4);
  CHECK(document.scene().value()->entities()->size() == 6);
  REQUIRE(document.undo(document.stats()->revision));
  auto restored = Json::parse(document.snapshot().value());
  CHECK(restored["scene"]["nextEntity"] == restored["nextEntity"]);
  CHECK(document.scene().value()->nextEntityValue().value() == document.stats()->nextEntity);
  restored["scene"].erase("nextEntity");
  auto authored = before["scene"];
  authored.erase("nextEntity");
  CHECK(restored["scene"] == authored);
  REQUIRE(document.redo(document.stats()->revision));
  CHECK(document.scene().value()->entities()->size() == 6);
  CHECK_FALSE(document.apply(operation, revision));
  bool rejected{};
  std::thread worker([&] {
    rejected = !resources.importModel(fixture.source().root() / "surface.glb", "assets/worker");
  });
  worker.join();
  CHECK(rejected);
  REQUIRE(document.save(document.stats()->revision));
  CHECK(project.loadStartup()->entities()->size() == 6);
}

TEST_CASE("model.sparse_base64_and_missing_normals_generate_owned_flat_surface") {
  Fixture fixture;
  auto model = Assets::loadModel(fixture.source(), "sparse.gltf", "assets/model");
  REQUIRE_MESSAGE(model, (model ? "" : model.error().message));
  CHECK(model->primitives[0].mesh.vertices[1].position == Math::Vec3{1, 0, .5F});
  CHECK(model->primitives[0].mesh.vertices[0].normal == Math::Vec3{0, 0, 1});
  auto original = Json::parse(fixture.source().readText("sparse.gltf").value());
  original["accessors"][0]["sparse"]["count"] = 4;
  REQUIRE(fixture.source().writeTextAtomic("bad.gltf", original.dump()));
  CHECK_FALSE(Assets::loadModel(fixture.source(), "bad.gltf", "assets/model"));
  auto truncated = Json::parse(fixture.source().readText("sparse.gltf").value());
  truncated["buffers"][0]["uri"] = "data:application/octet-stream;base64,A===";
  REQUIRE(fixture.source().writeTextAtomic("bad.gltf", truncated.dump()));
  CHECK_FALSE(Assets::loadModel(fixture.source(), "bad.gltf", "assets/model"));
}

TEST_CASE("model.failed_import_preserves_destination_and_cleans_owned_staging") {
  Fixture fixture;
  auto project = Project::Project::create(fixture.files, "game").value();
  auto resources = Editor::ResourceStore::open(project).value();
  const auto baseline = resources.list()->size();
  REQUIRE(fixture.source().writeTextAtomic("bad.gltf", "{}"));
  CHECK_FALSE(resources.importModel(fixture.source().root() / "bad.gltf", "assets/bad"));
  CHECK(resources.list()->size() == baseline);
  CHECK_FALSE(std::filesystem::exists(project.files()->root() / "assets/bad"));
  for (const auto& entry :
       std::filesystem::directory_iterator(project.files()->root() / "assets")) {
    CHECK_FALSE(entry.path().filename().string().starts_with(".dot-stage"));
  }
  Editor::ResourceLimits limited;
  limited.entries = baseline + 5;
  auto small = Editor::ResourceStore::open(project, limited).value();
  CHECK_FALSE(small.importModel(fixture.source().root() / "surface.glb", "assets/full"));
  auto prepared = Assets::loadModel(fixture.source(), "surface.glb", "assets/model").value();
  prepared.primitives[0].material = 99;
  auto staging = Core::StagedDirectory::create(fixture.files).value();
  CHECK_FALSE(Assets::writeModel(staging.files().value(), prepared));
  CHECK(std::filesystem::is_empty(staging.files()->root()));
  std::filesystem::create_symlink("surface.glb", fixture.source().root() / "link.glb");
  CHECK_FALSE(resources.importModel(fixture.source().root() / "link.glb", "assets/link"));
}

TEST_CASE("model.required_quantization_preserves_normalized_integer_positions") {
  Fixture fixture;
  auto model = Assets::loadModel(fixture.source(), "quantized.gltf", "assets/model");
  REQUIRE_MESSAGE(model, (model ? "" : model.error().message));
  CHECK(model->primitives[0].mesh.vertices[1].position.x == 1);
  CHECK(model->primitives[0].mesh.vertices[0].position.z == doctest::Approx(.5000076));
  auto invalid = Json::parse(fixture.source().readText("quantized.gltf").value());
  invalid.erase("extensionsRequired");
  invalid.erase("extensionsUsed");
  REQUIRE(fixture.source().writeTextAtomic("bad.gltf", invalid.dump()));
  CHECK_FALSE(Assets::loadModel(fixture.source(), "bad.gltf", "assets/model"));
}

TEST_CASE("model.alpha_modes_preserve_factor_vertex_texture_and_cutoff") {
  Fixture fixture;
  auto source = Json::parse(fixture.source().readText("surface.gltf").value());

  for (const auto* mode : {"OPAQUE", "MASK", "BLEND"}) {
    source["materials"][0]["alphaMode"] = mode;
    source["materials"][0]["alphaCutoff"] = .3;
    REQUIRE(fixture.source().writeTextAtomic("alpha.gltf", source.dump()));
    auto model = Assets::loadModel(fixture.source(), "alpha.gltf", "assets/model");
    REQUIRE(model);
    CHECK(model->materials[0].alphaMode
          == (std::string(mode) == "OPAQUE" ? Graphics::AlphaMode::Opaque
              : std::string(mode) == "MASK" ? Graphics::AlphaMode::Mask
                                            : Graphics::AlphaMode::Blend));
    CHECK(model->materials[0].alphaCutoff == doctest::Approx(.3));
    CHECK(model->primitives[0].mesh.vertices[0].color.w == .25F);
    CHECK(Assets::decodeImage(model->images[0].png)->pixels[3] == 64);
  }
}

TEST_CASE("model.reimport_reviews_owned_choices_preserves_material_and_reverts_complete_bundle") {
  Fixture f;
  auto project = Project::Project::create(f.files, "project").value();
  auto store = Editor::ResourceStore::open(project).value();
  REQUIRE(store.importModel(f.source().root() / "surface.gltf", "assets/model"));
  const auto originalScene = project.files()->readText("assets/model/model.dotscene").value();
  const auto originalMesh = project.files()->readBytes("assets/model/meshes/0.dotmesh").value();
  auto material
      = Json::parse(project.files()->readText("assets/model/materials/0.dotmaterial").value());
  material["roughness"] = .91;
  const auto authored = material.dump(2);
  REQUIRE(project.files()->writeTextAtomic("assets/model/materials/0.dotmaterial", authored));
  REQUIRE(project.files()->writeTextAtomic("assets/model/notes.txt", "retain local file"));
  auto source = Json::parse(f.source().readText("surface.gltf").value());
  source["nodes"][0]["translation"] = {2., 0., 0.};
  REQUIRE(f.source().writeTextAtomic("surface.gltf", source.dump()));
  auto mesh = f.source().readBytes("surface.bin").value();
  float x{};
  std::memcpy(&x, mesh.data(), sizeof(x));
  x += .125F;
  std::memcpy(mesh.data(), &x, sizeof(x));
  REQUIRE(f.source().writeAtomic("surface.bin", mesh));
  auto review
      = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
            .value();
  auto state = wait(review);
  REQUIRE_MESSAGE(state.state == Editor::ReimportState::Review, state.error);
  CHECK_FALSE(review.validate(state.revision));
  CHECK_FALSE(review.status(0, 0));
  CHECK_FALSE(review.status(0, 257));
  CHECK_FALSE(review.choose("absent", Editor::ReimportChoice::Source, state.revision));
  CHECK(project.files()->readText("assets/model/model.dotscene").value() == originalScene);
  CHECK(project.files()->readBytes("assets/model/meshes/0.dotmesh").value() == originalMesh);
  auto preview = review.file("assets/model/materials/0.dotmaterial", state.revision);
  REQUIRE(preview);
  CHECK(preview->text);
  CHECK(preview->before == authored);
  CHECK(preview->after != authored);
  const auto stale = state.revision;
  state = chooseAll(review, Editor::ReimportChoice::Source);
  CHECK_FALSE(review.validate(stale));
  REQUIRE(review.choose("assets/model/materials/0.dotmaterial", Editor::ReimportChoice::Project,
                        state.revision));
  state = review.status().value();
  REQUIRE(review.validate(state.revision));
  state = wait(review);
  REQUIRE_MESSAGE(state.state == Editor::ReimportState::Ready, state.error);
  REQUIRE(review.apply(state.revision));
  state = review.status().value();
  CHECK(state.state == Editor::ReimportState::Applied);
  CHECK(project.files()->readText("assets/model/materials/0.dotmaterial").value() == authored);
  CHECK(project.files()->readText("assets/model/notes.txt").value() == "retain local file");
  CHECK(project.files()->readBytes("assets/model/meshes/0.dotmesh").value() != originalMesh);
  CHECK(project.files()->readText("assets/model/model.dotscene").value() != originalScene);
  CHECK(store.list());
  REQUIRE(review.revert(state.revision));
  CHECK(project.files()->readText("assets/model/model.dotscene").value() == originalScene);
  CHECK(project.files()->readBytes("assets/model/meshes/0.dotmesh").value() == originalMesh);
  CHECK(project.files()->readText("assets/model/materials/0.dotmaterial").value() == authored);
  CHECK(review.status()->state == Editor::ReimportState::Reverted);
  CHECK_FALSE(review.revert(review.status()->revision));
}

TEST_CASE("model.reimport_rejects_structural_changes_and_stale_project_bytes_without_publication") {
  Fixture f;
  auto project = Project::Project::create(f.files, "project").value();
  auto store = Editor::ResourceStore::open(project).value();
  REQUIRE(store.importModel(f.source().root() / "surface.gltf", "assets/model"));
  const auto original = f.source().readText("surface.gltf").value();
  auto source = Json::parse(original);
  source["nodes"][0]["name"] = "renamed";
  REQUIRE(f.source().writeTextAtomic("surface.gltf", source.dump()));

  {
    auto review
        = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
              .value();
    const auto state = wait(review);
    CHECK(state.state == Editor::ReimportState::Failed);
    CHECK(state.error.find("stable-layout") != std::string::npos);
    CHECK_FALSE(review.apply(state.revision));
  }

  REQUIRE(f.source().writeTextAtomic("surface.gltf", original));
  auto review
      = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
            .value();
  auto state = wait(review);
  REQUIRE(state.state == Editor::ReimportState::Review);
  REQUIRE(review.validate(state.revision));
  state = wait(review);
  REQUIRE_MESSAGE(state.state == Editor::ReimportState::Ready, state.error);
  const auto baseline = project.files()->readText("scripts/Mover.ts").value();
  REQUIRE(project.files()->writeTextAtomic("scripts/Mover.ts", baseline + "\n"));
  CHECK_FALSE(review.apply(state.revision));
  REQUIRE(project.files()->writeTextAtomic("scripts/Mover.ts", baseline));
  REQUIRE(review.apply(state.revision));
  state = review.status().value();
  REQUIRE(project.files()->writeTextAtomic("assets/new.txt", "external edit"));
  CHECK_FALSE(review.revert(state.revision));
  CHECK(project.files()->readText("assets/new.txt").value() == "external edit");
  CHECK(store.list());
}

TEST_CASE("model.reimport_validation_failure_keeps_project_and_reports_referenced_asset") {
  Fixture f;
  auto project = Project::Project::create(f.files, "project").value();
  auto store = Editor::ResourceStore::open(project).value();
  REQUIRE(store.importModel(f.source().root() / "surface.gltf", "assets/model"));
  auto material
      = Json::parse(project.files()->readText("assets/model/materials/0.dotmaterial").value());
  material["baseColorTexture"]["path"] = "assets/missing.png";
  const auto broken = material.dump();
  REQUIRE(project.files()->writeTextAtomic("assets/model/materials/0.dotmaterial", broken));
  auto review
      = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
            .value();
  auto state = wait(review);
  REQUIRE_MESSAGE(state.state == Editor::ReimportState::Review, state.error);
  state = chooseAll(review, Editor::ReimportChoice::Project);
  REQUIRE(review.validate(state.revision));
  state = wait(review);
  CHECK(state.state == Editor::ReimportState::Review);
  CHECK_FALSE(state.error.empty());
  CHECK_FALSE(review.apply(state.revision));
  CHECK(project.files()->readText("assets/model/materials/0.dotmaterial").value() == broken);
  state = chooseAll(review, Editor::ReimportChoice::Source);
  REQUIRE(review.validate(state.revision));
  state = wait(review);
  REQUIRE_MESSAGE(state.state == Editor::ReimportState::Ready, state.error);
  CHECK(review.apply(state.revision));
}

TEST_CASE("model.reimport_owner_move_invalid_boundaries_and_cancel_release_private_trees") {
  Fixture f;
  auto project = Project::Project::create(f.files, "project").value();
  auto store = Editor::ResourceStore::open(project).value();
  REQUIRE(store.importModel(f.source().root() / "surface.gltf", "assets/model"));
  const auto root = project.files()->root();
  const auto count = [&] {
    return std::distance(std::filesystem::directory_iterator(root),
                         std::filesystem::directory_iterator{});
  };
  const auto before = count();
  CHECK_FALSE(Editor::ModelReimport::begin(project, "", "../escape"));
  CHECK_FALSE(Editor::ModelReimport::begin(project, "", "assets/model", 1024));

  {
    auto original
        = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
              .value();
    auto moved = std::move(original);
    CHECK_FALSE(original.status());
    CHECK_FALSE(original.validate(1));
    bool rejected{};
    std::thread thread([&] { rejected = !moved.status(); });
    thread.join();
    CHECK(rejected);
    const auto state = wait(moved);
    CHECK(state.state == Editor::ReimportState::Review);
    CHECK_FALSE(moved.file("missing", state.revision));
  }

  CHECK(count() == before);

  {
    auto pending
        = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
              .value();
  }

  CHECK(count() == before);
  CHECK(store.list());
  std::filesystem::create_symlink(f.source().root() / "surface.gltf",
                                  f.source().root() / "link.gltf");
  auto invalid
      = Editor::ModelReimport::begin(project, f.source().root() / "link.gltf", "assets/model")
            .value();
  CHECK(wait(invalid).state == Editor::ReimportState::Failed);
}

TEST_CASE("model.reimport_added_files_require_source_and_accepting_keeps_published_data") {
  Fixture f;
  auto project = Project::Project::create(f.files, "project").value();
  auto store = Editor::ResourceStore::open(project).value();
  REQUIRE(store.importModel(f.source().root() / "surface.gltf", "assets/model"));
  auto source = Json::parse(f.source().readText("surface.gltf").value());
  source["materials"].push_back(source["materials"][0]);
  REQUIRE(f.source().writeTextAtomic("surface.gltf", source.dump()));
  const auto root = project.files()->root();
  const auto before = std::distance(std::filesystem::directory_iterator(root),
                                    std::filesystem::directory_iterator{});

  {
    auto review
        = Editor::ModelReimport::begin(project, f.source().root() / "surface.gltf", "assets/model")
              .value();
    auto state = wait(review);
    REQUIRE_MESSAGE(state.state == Editor::ReimportState::Review, state.error);
    const auto added
        = std::ranges::find_if(state.changes, [](const auto& change) { return change.added; });
    REQUIRE(added != state.changes.end());
    CHECK(added->choice == Editor::ReimportChoice::Source);
    CHECK_FALSE(review.choose(added->path, Editor::ReimportChoice::Project, state.revision));
    state = chooseAll(review, Editor::ReimportChoice::Source);
    REQUIRE(review.validate(state.revision));
    state = wait(review);
    REQUIRE_MESSAGE(state.state == Editor::ReimportState::Ready, state.error);
    REQUIRE(review.apply(state.revision));
  }

  CHECK(std::distance(std::filesystem::directory_iterator(root),
                      std::filesystem::directory_iterator{})
        == before);
  CHECK(project.files()->readText("assets/model/materials/2.dotmaterial"));
  CHECK(store.list());
}
