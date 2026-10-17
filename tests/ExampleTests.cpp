#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

import Dot.Animation.Files;
import Dot.Animation.SkinFiles;
import Dot.Assets.Image;
import Dot.Assets.Material;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;
import Dot.Graphics.Image;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Project;
import Dot.Project.GameSession;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Platform.Input;
import Dot.Platform.Events;
import Dot.World.Scene;

TEST_CASE("example.source_projects_open_and_compile_without_build_generated_sdk") {
  auto compiler = Dot::Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();

  for (const auto* root : {DOT_EXAMPLE_SOURCE, DOT_LAB_SOURCE}) {
    auto project = Dot::Project::Project::open(root);
    REQUIRE_MESSAGE(project, (project ? "" : project.error().message));
    auto files = project->files().value();
    CHECK_FALSE(std::filesystem::exists(files.root() / "scripts/dot"));

    auto scene = project->loadStartup();
    REQUIRE_MESSAGE(scene, (scene ? "" : scene.error().message));
    auto program = compiler.compileProject(files);
    REQUIRE_MESSAGE(program, (program ? "" : program.error().message));
    CHECK_FALSE(program->modules()->empty());
  }
}

namespace {
  using Json = nlohmann::json;
  using namespace Dot;

  struct Fixture {
    Core::StagedDirectory storage;
    Project::Project project;

    Fixture()
        : storage(Core::StagedDirectory::create(
                      Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          project([&]() {
            std::filesystem::copy(DOT_EXAMPLE_PROJECT, storage.files()->root() / "project",
                                  std::filesystem::copy_options::recursive);
            auto opened = Project::Project::open(storage.files()->root() / "project");
            REQUIRE_MESSAGE(opened, (opened ? "" : opened.error().message));
            return std::move(*opened);
          }()) {
    }

    auto compiler() -> Script::TypeScriptCompiler {
      return Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    }

    auto session(bool live = false) -> Project::GameSession {
      auto description = project.description().value();
      description.hotReload = live;
      REQUIRE(project.save(description));
      Project::GameSessionConfig config;
      config.pollInterval = std::chrono::milliseconds{0};
      auto result = Project::GameSession::create(project, compiler(), config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  template <class T>
  auto good(const Core::Result<T>& result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  }

  auto report(const Core::Result<Script::ScriptReport>& result) -> void {
    good(result);
    CHECK_FALSE(result->realmFaulted);

    for (const auto& failure : result->failures) {
      CHECK_MESSAGE(false, failure.error.message);
    }
  }

  auto report(const Core::Result<Script::LiveScriptReport>& result) -> void {
    good(result);
    CHECK_FALSE(result->frame.realmFaulted);

    for (const auto& failure : result->frame.failures) {
      CHECK_MESSAGE(false, failure.error.message);
    }
  }

  auto entity(Project::GameSession& session, std::string_view name) -> World::EntityInfo {
    auto entities = session.scene().value()->entities().value();
    auto found = std::ranges::find(entities, name, &World::EntityInfo::name);
    REQUIRE(found != entities.end());
    return *found;
  }

  class Driver {
  public:
    explicit Driver(Project::GameSession& session) : m_session(session) {
    }

    auto publish(std::initializer_list<Platform::Key> down = {}, float dx = 0, float dy = 0,
                 bool captured = false) -> void {
      m_input.beginFrame();
      constexpr std::array keys{Platform::Key::A, Platform::Key::D,     Platform::Key::W,
                                Platform::Key::S, Platform::Key::Space, Platform::Key::LeftShift};

      for (auto key : keys) {
        const bool desired = std::ranges::find(down, key) != down.end();

        if (m_input.key(key).down != desired) {
          REQUIRE(m_input.consume({0, 0, Platform::KeyChanged{key, desired, false}}));
        }
      }

      REQUIRE(m_input.consume({0, 0, Platform::PointerMoved{0, 0, dx, dy}}));
      good(m_session.publish(m_input, captured));
    }

    auto frame(std::initializer_list<Platform::Key> down = {}, float dx = 0, float dy = 0,
               bool captured = false) -> void {
      publish(down, dx, dy, captured);
      report(m_session.fixedUpdate());
      report(m_session.update(1.0 / 60));
    }

  private:
    Project::GameSession& m_session;
    Platform::InputState m_input;
  };

  auto observation(Project::GameSession& session) -> Json {
    const auto entities = session.scene().value()->entities().value();
    const auto observer = std::ranges::find_if(entities, [](const auto& entity) {
      return entity.script && entity.script->className == "Observer";
    });
    REQUIRE(observer != entities.end());
    return Json::parse(observer->name);
  }

  auto observe(Fixture& fixture) -> void {
    REQUIRE(fixture.project.files()->writeTextAtomic("scripts/observer.ts", R"ts(
import {scene, ScriptComponent} from 'dot:engine';
export class Observer extends ScriptComponent {
  #model = scene.findEntities('Surveyor')[0]!;
  #player = scene.findEntities('Player')[0]!;
  onUpdate(): void {
    const character = this.#player.characterState;
    this.self.name = JSON.stringify({clip: this.#model.animationState.clip,
      ground: character.ground, vy: character.velocity.y});
  }
}
)ts"));
    auto scene = fixture.project.loadStartup().value();
    const auto id = scene.createEntity("Observer").value();
    REQUIRE(scene.setScript(id, World::ScriptComponent{"observer.ts", "Observer"}));
    REQUIRE(fixture.project.saveStartup(scene));
  }
}  // namespace

TEST_CASE("prism.character_moves_relative_to_acknowledged_camera_and_blocks_at_walls") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  CHECK(session.pointerCaptureRequested().value());
  Driver driver(session);

  for (int frame = 0; frame < 30; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto forward = entity(session, "Player").transform.position;
  CHECK(forward.x == doctest::Approx(-3));
  CHECK(forward.z < 0.8F);
  driver.frame({}, -523.5988F, 0, false);
  const auto unchanged = entity(session, "Camera").transform.rotation;
  CHECK(unchanged.y == doctest::Approx(0));
  driver.frame({}, -523.5988F, 0, true);
  CHECK(entity(session, "Camera").transform.rotation.y > 0.7F);

  for (int frame = 0; frame < 120; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto stopped = entity(session, "Player").transform.position;
  CHECK(stopped.x < -6.9F);
  CHECK(stopped.x > -7.1F);
  CHECK(stopped.y == doctest::Approx(0).epsilon(0.02));
  CHECK(fixture.project.loadStartup()->entityCount().value()
        == session.scene().value()->entityCount().value());
  report(session.close());
}

TEST_CASE("prism.diagonal_intent_has_the_same_speed_and_native_sprint_is_faster") {
  auto displacement = [](bool diagonal, bool sprint) {
    Fixture fixture;
    auto session = fixture.session();
    report(session.start());
    Driver driver(session);
    const auto origin = entity(session, "Player").transform.position;

    for (int frame = 0; frame < 25; ++frame) {
      if (sprint) {
        driver.frame({Platform::Key::D, Platform::Key::LeftShift});
      } else if (diagonal) {
        driver.frame({Platform::Key::D, Platform::Key::W});
      } else {
        driver.frame({Platform::Key::D});
      }
    }

    const auto final = entity(session, "Player").transform.position;
    report(session.close());
    return std::hypot(final.x - origin.x, final.z - origin.z);
  };
  const auto straight = displacement(false, false);
  CHECK(displacement(true, false) == doctest::Approx(straight).epsilon(0.02));
  CHECK(displacement(false, true) > straight * 1.7F);
}

TEST_CASE("prism.jump_edge_is_consumed_once_and_native_rig_plays_jump_air_land") {
  Fixture fixture;
  observe(fixture);
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);

  for (int frame = 0; frame < 10; ++frame) {
    driver.frame();
  }

  CHECK(observation(session)["ground"] == "grounded");
  CHECK(observation(session)["clip"] == 0);
  driver.publish({Platform::Key::Space});

  for (int step = 0; step < 6; ++step) {
    report(session.fixedUpdate());
  }

  report(session.update(0.1));
  CHECK(entity(session, "Player").transform.position.y > 0.4F);
  std::set<int> clips{observation(session)["clip"].get<int>()};
  float peak{};

  for (int frame = 0; frame < 150; ++frame) {
    driver.frame({Platform::Key::Space});
    peak = std::max(peak, entity(session, "Player").transform.position.y);
    clips.insert(observation(session)["clip"].get<int>());
  }

  CHECK(peak > 1.5F);
  CHECK(peak < 2.0F);
  CHECK(clips.contains(4));
  CHECK(clips.contains(5));
  CHECK(clips.contains(6));
  CHECK(observation(session)["ground"] == "grounded");
  CHECK(entity(session, "Player").transform.position.y == doctest::Approx(0).epsilon(0.02));
  report(session.close());
}

TEST_CASE("prism.jump_press_between_fixed_steps_is_buffered_without_repeating") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);
  driver.frame();
  driver.publish({Platform::Key::Space});
  report(session.update(1.0 / 120));
  driver.publish();
  report(session.fixedUpdate());
  report(session.update(1.0 / 60));
  CHECK(entity(session, "Player").transform.position.y > 0.05F);
  report(session.close());
}

TEST_CASE("prism.hot_reload_keeps_native_rig_character_motion_and_orbit") {
  Fixture fixture;
  auto session = fixture.session(true);
  report(session.start());
  Driver driver(session);
  driver.frame({}, -100, 25, true);

  for (int frame = 0; frame < 10; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto before = entity(session, "Player").transform.position;
  const auto rotation = entity(session, "Camera").transform.rotation;
  const auto scope = session.scene().value()->id();
  auto files = fixture.project.files().value();
  auto source = files.readText("scripts/camera.ts").value();
  REQUIRE(files.writeTextAtomic("scripts/camera.ts", source + "\n"));
  auto reload = session.update(0);
  report(reload);
  REQUIRE(reload->source.committed);
  REQUIRE(reload->source.reload);
  CHECK(reload->source.reload->migrated == 1);
  CHECK(session.scene().value()->id() == scope);
  CHECK(entity(session, "Player").transform.position == before);
  CHECK(entity(session, "Camera").transform.rotation == rotation);
  CHECK(session.pointerCaptureRequested().value());
  driver.frame({Platform::Key::W});
  CHECK(entity(session, "Player").transform.position.z < before.z);
  CHECK(entity(session, "Player").character.has_value());
  report(session.close());
}

TEST_CASE("prism.authored_steps_climb_with_native_support_and_tall_rise_requires_jump") {
  Fixture fixture;
  auto authored = fixture.project.loadStartup().value();
  const auto entities = authored.entities().value();
  const auto actor = std::ranges::find(entities, "Player", &World::EntityInfo::name);
  REQUIRE(actor != entities.end());
  auto pose = actor->transform;
  pose.position = {2, 0, -1.8F};
  REQUIRE(authored.setTransform(actor->id, pose));
  REQUIRE(fixture.project.saveStartup(authored));
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);

  for (int frame = 0; frame < 90; ++frame) {
    driver.frame({Platform::Key::W});
  }

  const auto blocked = entity(session, "Player").transform.position;
  CHECK(blocked.z < -4.9F);
  CHECK(blocked.z > -5.5F);
  CHECK(blocked.y == doctest::Approx(.54).epsilon(.08));
  driver.frame({Platform::Key::W, Platform::Key::Space});

  for (int frame = 0; frame < 8; ++frame) {
    driver.frame({Platform::Key::W});
  }

  for (int frame = 0; frame < 80; ++frame) {
    driver.frame();
  }

  const auto landed = entity(session, "Player").transform.position;
  CHECK(landed.y == doctest::Approx(1.2).epsilon(.05));
  report(session.close());
}

TEST_CASE("prism.authored_surfaces_have_real_normal_frames_and_local_texture_inputs") {
  Fixture fixture;
  auto scene = fixture.project.loadStartup().value();
  auto files = fixture.project.files().value();
  std::map<std::string, Graphics::MeshSurface> surfaces;
  std::map<std::string, Assets::MaterialDescription> materials;
  REQUIRE(scene.environment()->has_value());
  REQUIRE(scene.occlusion()->has_value());
  auto panorama = Assets::loadHdrImage(files, scene.environment()->value().asset);
  REQUIRE_MESSAGE(panorama, (panorama ? "" : panorama.error().message));
  unsigned shadowed{};

  for (const auto& entity : scene.entities().value()) {
    if (entity.light && entity.light->shadows) {
      ++shadowed;
    }

    if (!entity.mesh) {
      continue;
    }

    INFO(entity.name);
    REQUIRE(entity.material);
    REQUIRE(entity.mesh->kind == World::MeshKind::Asset);
    const auto& path = entity.mesh->asset;

    if (!surfaces.contains(path)) {
      auto mesh = Assets::loadMesh(files, path);
      REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
      surfaces.emplace(path, Graphics::meshSurface(mesh->data).value());
    }

    const auto& material = entity.material->asset;

    if (!materials.contains(material)) {
      auto loaded = Assets::loadMaterial(files, material);
      REQUIRE_MESSAGE(loaded, (loaded ? "" : loaded.error().message));
      materials.emplace(material, std::move(*loaded));
    }

    CHECK(surfaces.at(path).hasNormals);

    if (materials.at(material).normalTexture) {
      CHECK_MESSAGE(surfaces.at(path).hasNormalMapFrame, path);
    }
  }

  CHECK(shadowed == 4);
  CHECK(surfaces.size() >= 10);
  CHECK(materials.size() >= 8);
  unsigned textured{};

  for (const auto& [path, material] : materials) {
    if (!material.baseColorTexture) {
      continue;
    }

    INFO(path);
    ++textured;
    REQUIRE(material.metallicRoughnessTexture);
    REQUIRE(material.normalTexture);
    REQUIRE(material.occlusionTexture);
    CHECK(material.baseColorTexture->sampling.colorSpace == Graphics::ColorSpace::Srgb);
    CHECK(material.normalTexture->sampling.colorSpace == Graphics::ColorSpace::Linear);
    CHECK(material.metallicRoughnessTexture->sampling.colorSpace == Graphics::ColorSpace::Linear);

    for (const auto* source : {&*material.baseColorTexture, &*material.metallicRoughnessTexture,
                               &*material.normalTexture, &*material.occlusionTexture}) {
      auto image = Assets::loadImage(files, source->path);
      REQUIRE_MESSAGE(image, (image ? "" : image.error().message));
      const auto size = path.starts_with("assets/surveyor/") ? 2048U : 1024U;
      CHECK(image->width == size);
      CHECK(image->height == size);
    }
  }

  CHECK(textured >= 3);
}

TEST_CASE("prism.surveyor_retains_textured_armor_eight_slot_skinning_and_seven_roles") {
  auto project = Project::Project::open(DOT_EXAMPLE_SOURCE).value();
  auto files = project.files().value();
  auto scene = project.loadStartup().value();
  const auto models = scene.findEntities("Surveyor").value();
  REQUIRE(models.size() == 1);
  auto model = scene.entity(models[0]).value();
  REQUIRE(model.animation);
  CHECK(model.animation->enabled);
  REQUIRE(model.animation->clips.size() == 7);
  const std::array names{"Idle_Loop",  "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop",
                         "Jump_Start", "Jump_Loop", "Jump_Land"};

  for (std::size_t index = 0; index < names.size(); ++index) {
    auto clip = Animation::loadClip(files, model.animation->clips[index]);
    REQUIRE_MESSAGE(clip, (clip ? "" : clip.error().message));
    CHECK(clip->description().value()->name == names[index]);
    CHECK(clip->duration().value() > 0);
    auto sample = clip->sample(clip->duration().value() / 2);
    REQUIRE(sample);
    CHECK_FALSE(sample->empty());
  }

  unsigned skinned{};
  bool extendedWeights{};
  std::set<std::string> textures;

  for (const auto& entity : scene.entities().value()) {
    if (!entity.mesh || !entity.mesh->asset.starts_with("assets/surveyor/")) {
      continue;
    }

    ++skinned;
    REQUIRE(entity.skin);
    REQUIRE(entity.material);
    auto skin = Animation::loadSkin(files, entity.skin->asset);
    auto mesh = Assets::loadMesh(files, entity.mesh->asset);
    REQUIRE(skin);
    REQUIRE(mesh);
    const auto* data = skin->description().value();
    CHECK(data->inverseBindMatrices.size() == 63);
    CHECK(entity.skin->joints.size() == 63);
    CHECK(data->vertices.size() == mesh->data.vertices.size());
    extendedWeights = extendedWeights
                      || std::ranges::any_of(
                          data->vertices, [](const auto& vertex) { return vertex.weights[4] > 0; });
    auto material = Assets::loadMaterial(files, entity.material->asset).value();
    REQUIRE(material.baseColorTexture);
    REQUIRE(material.normalTexture);
    REQUIRE(material.metallicRoughnessTexture);
    REQUIRE(material.occlusionTexture);
    REQUIRE(material.emissiveTexture);
    CHECK(material.occlusionTexture->path == material.metallicRoughnessTexture->path);
    CHECK(material.emissive.x > 1);

    for (const auto* texture : {&*material.baseColorTexture, &*material.normalTexture,
                                &*material.metallicRoughnessTexture, &*material.emissiveTexture}) {
      textures.insert(texture->path);
    }
  }

  CHECK(skinned == 3);
  CHECK(extendedWeights);
  REQUIRE(textures.size() == 4);

  for (const auto& path : textures) {
    auto image = Assets::loadImage(files, path);
    REQUIRE(image);
    CHECK(image->width == 2048);
    CHECK(image->height == 2048);
  }
}
