#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Editor.NavigationBake;
import Dot.Editor.Resources;
import Dot.Editor.Document;
import Dot.Navigation.Files;
import Dot.Navigation.Geometry;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Project;
import Dot.Project.ScenePreparation;
import Dot.Physics.Body;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Project::Project project
        = Project::Project::create(storage.files().value(), "navigation").value();
    Core::FileSystem files = project.files().value();
    World::Scene scene = World::Scene::create("Walkable room").value();
    World::EntityId floor = scene.createEntity("Floor").value();

    Fixture() {
      REQUIRE(files.createDirectories("assets"));
      World::RigidBody body;
      body.motion = Physics::Motion::Static;
      body.shape = Physics::Shape::Box;
      REQUIRE(scene.setRigidBody(floor, body));
      REQUIRE(scene.setTransform(floor, {{0, -.25F, 0}, {}, {12, .5F, 12}}));
    }

    auto meshFloor() -> void {
      REQUIRE(Assets::saveMesh(files, "assets/floor.dotmesh", Graphics::cube().value()));
      auto body = *scene.entity(floor)->rigidBody;
      body.shape = Physics::Shape::Mesh;
      body.meshAsset = "assets/floor.dotmesh";
      REQUIRE(scene.setRigidBody(floor, body));
    }
  };

  auto await(Editor::NavigationBake& job) -> Core::Result<Editor::NavigationCreation> {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{15};

    while (std::chrono::steady_clock::now() < deadline) {
      auto ready = job.ready();
      REQUIRE(ready);

      if (*ready) {
        return job.take();
      }

      std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    return Core::fail(Core::ErrorCode::Unavailable, "Navigation test wait exceeded 15 seconds");
  }
}  // namespace

TEST_CASE("navigation_bake.worker_freezes_scene_and_binary_publication_is_exclusive") {
  Fixture f;
  const auto identity
      = Navigation::geometryIdentity(Navigation::sceneGeometry(f.scene, {}).value()).value();
  auto job = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
  REQUIRE(f.scene.setTransform(f.floor, {{0, -.25F, 0}, {}, {10, .5F, 10}}));
  auto result = await(job);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->surface.description().value()->source == identity);
  REQUIRE(result->surface.route({{-4, 0, 0}, {4, 0, 0}}));
  CHECK_FALSE(job.take());
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/room.dotnav"));
  auto store = Editor::ResourceStore::open(f.project).value();
  REQUIRE(store.createBinary("assets/room.dotnav", result->bytes));
  CHECK_FALSE(store.createBinary("assets/room.dotnav", result->bytes));
  CHECK_FALSE(store.createBinary("assets/room.dotui", result->bytes));
  CHECK_FALSE(store.createBinary("scripts/room.dotnav", result->bytes));
  CHECK_FALSE(store.createBinary("assets/Room.dotnav", result->bytes));
  CHECK(f.files.readBytes("assets/room.dotnav").value() == result->bytes);
  REQUIRE(Navigation::loadSurface(f.files, "assets/room.dotnav"));
  CHECK_FALSE(Navigation::loadSurface(f.files, "assets/Room.dotnav"));
  CHECK_FALSE(Navigation::loadSurface(f.files, "../room.dotnav"));
  REQUIRE(f.files.writeAtomic("assets/bad.dotnav", std::span(result->bytes).first(100)));
  CHECK_FALSE(Navigation::loadSurface(f.files, "assets/bad.dotnav"));
  std::filesystem::create_symlink(f.files.root() / "assets/room.dotnav",
                                  f.files.root() / "assets/link.dotnav");
  CHECK_FALSE(Navigation::loadSurface(f.files, "assets/link.dotnav"));
  CHECK_FALSE(store.createBinary("assets/link.dotnav", result->bytes));
}

TEST_CASE("navigation_bake.mesh_proof_matches_exact_decoded_bytes_and_rejects_changed_sources") {
  Fixture f;
  f.meshFloor();
  auto job = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
  auto result = await(job);
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  REQUIRE(result->inputs.size() == 1);
  CHECK(result->inputs[0].bytes == f.files.readBytes("assets/floor.dotmesh").value());
  REQUIRE(Editor::validateNavigationInputs(f.files, *result));
  const auto original = result->inputs[0].bytes;
  auto mesh = Graphics::cube().value();
  mesh.vertices[0].color = {.3F, .4F, .5F, 1};
  REQUIRE(Assets::saveMesh(f.files, "assets/floor.dotmesh", mesh));
  /** Even non-collision byte edits conflict with an in-flight authoring publication.
   * 即使非碰撞属性的字节修改也应阻止进行中的编辑发布。 */
  CHECK_FALSE(Editor::validateNavigationInputs(f.files, *result));
  REQUIRE(f.files.writeAtomic("assets/floor.dotmesh", original));
  REQUIRE(Editor::validateNavigationInputs(f.files, *result));
  CHECK_FALSE(Editor::validateNavigationInputs(f.storage.files().value(), *result));
  std::filesystem::remove(f.files.root() / "assets/floor.dotmesh");
  CHECK_FALSE(Editor::validateNavigationInputs(f.files, *result));
  auto failed = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
  CHECK_FALSE(await(failed));
  REQUIRE(f.files.writeAtomic("assets/floor.dotmesh", original));
  auto recovery = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
  REQUIRE(await(recovery));
}

TEST_CASE("navigation_bake.cancellation_moves_thread_ownership_and_repeated_initialization") {
  Fixture f;

  for (int count = 0; count < 3; ++count) {
    auto job = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
    auto moved = std::move(job);
    CHECK_FALSE(job.ready());
    CHECK_FALSE(job.take());
    CHECK_FALSE(std::async(std::launch::async, [&] { return moved.ready(); }).get());
    moved.requestStop();
    const auto result = await(moved);
    CHECK_FALSE(moved.take());
    /** A completed build may win cancellation; publication is a separate owner operation.
     * 已完成构建可先于取消返回，发布仍须独立的拥有线程操作。 */
    CHECK((result.has_value() || result.error().code == Core::ErrorCode::Unavailable));
  }

  auto job = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
  REQUIRE(await(job));
  CHECK_FALSE(std::filesystem::exists(f.files.root() / "assets/room.dotnav"));
  auto settings = Navigation::BuildSettings{};
  settings.agentRadius = 0;
  CHECK_FALSE(Editor::NavigationBake::begin(f.files, f.scene, settings));
}

TEST_CASE("navigation_bake.scene_binding_history_preparation_and_content_keep_their_scopes") {
  Fixture f;
  auto job = Editor::NavigationBake::begin(f.files, f.scene, {}).value();
  const auto result = await(job);
  REQUIRE(result);
  auto store = Editor::ResourceStore::open(f.project).value();
  REQUIRE(store.createBinary("assets/room.dotnav", result->bytes));
  const auto path = f.project.description()->startScene;
  REQUIRE(World::saveScene(f.files, path, f.scene));
  auto document = Editor::Document::open(f.project).value();
  REQUIRE(document.apply(R"([{"op":"navigation","value":"assets/room.dotnav"}])",
                         document.stats()->revision));
  REQUIRE(document.scene().value()->navigation().value());
  REQUIRE(document.undo(document.stats()->revision));
  CHECK_FALSE(document.scene().value()->navigation().value());
  REQUIRE(document.redo(document.stats()->revision));
  REQUIRE(document.save(document.stats()->revision));
  auto proof = Project::PreparedScene::create(f.files, path, {}, {});
  REQUIRE_MESSAGE(proof, (proof ? "" : proof.error().message));
  const auto owned = proof->navigation().value();
  REQUIRE(owned);
  CHECK(owned->description().value()->source == result->surface.description().value()->source);
  auto moved = std::move(*proof);
  CHECK_FALSE(proof->navigation());
  std::filesystem::remove(f.files.root() / "assets/room.dotnav");
  REQUIRE(moved.navigation().value()->route({{-4, 0, 0}, {4, 0, 0}}));
  CHECK_FALSE(Project::PreparedScene::create(f.files, path, {}, {}));
  auto content = Project::PreparedScene::create(f.files, path, {}, {},
                                                Project::ScenePreparationKind::Content);
  REQUIRE(content);
  CHECK_FALSE(content->navigation().value());
  CHECK_FALSE(content->scene()->navigation().value());
  REQUIRE(store.createBinary("assets/room.dotnav", result->bytes));
  REQUIRE(f.scene.setNavigation("assets/room.dotnav"));
  REQUIRE(f.scene.setTransform(f.floor, {{0, -.25F, 0}, {}, {10, .5F, 10}}));
  REQUIRE(World::saveScene(f.files, path, f.scene));
  CHECK_FALSE(Project::PreparedScene::create(f.files, path, {}, {}));
}
