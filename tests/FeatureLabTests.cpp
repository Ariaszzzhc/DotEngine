#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

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
import Dot.World.SceneFiles;
import Dot.Audio.Mixer;
import Dot.Assets.Mesh;
import Dot.Graphics.Geometry;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory storage;
    Project::Project project;

    Fixture()
        : storage(Core::StagedDirectory::create(
                      Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                      .value()),
          project([&]() {
            std::filesystem::copy(DOT_FEATURE_LAB, storage.files()->root() / "project",
                                  std::filesystem::copy_options::recursive);
            auto opened = Project::Project::open(storage.files()->root() / "project");
            REQUIRE_MESSAGE(opened, (opened ? "" : opened.error().message));
            return std::move(*opened);
          }()) {
    }

    auto session(bool live = false) -> Project::GameSession {
      auto description = project.description().value();
      description.hotReload = live;
      REQUIRE(project.save(description));
      auto compiler = Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
      Project::GameSessionConfig config;
      config.pollInterval = std::chrono::milliseconds{0};
      auto result = Project::GameSession::create(project, compiler, config);
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  template <class T>
  auto report(const Core::Result<T>& result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    const auto& frame = [&]() -> const Script::ScriptReport& {
      if constexpr (std::is_same_v<T, Script::ScriptReport>) {
        return *result;
      } else {
        return result->frame;
      }
    }();
    for (const auto& failure : frame.failures) {
      FAIL_CHECK(failure.error.message);
    }

    REQUIRE_FALSE(frame.realmFaulted);
    REQUIRE(frame.failures.empty());
  }

  auto data(const World::Scene& scene) -> Json {
    auto entities = scene.entities().value();
    auto found = std::ranges::find(entities, "Report", &World::EntityInfo::name);
    REQUIRE(found != entities.end());
    REQUIRE(found->script.has_value());
    return Json::parse(found->script->properties);
  }

  auto declared(const Project::Project& project) -> std::set<std::string> {
    auto manifest = Json::parse(project.files()->readText("coverage.json").value());
    REQUIRE(manifest["version"] == 1);
    auto list = manifest["sdk"].get<std::vector<std::string>>();
    REQUIRE(std::ranges::is_sorted(list));
    std::set<std::string> result(list.begin(), list.end());
    REQUIRE(result.size() == list.size());
    return result;
  }

  auto complete(const Project::Project& project, const World::Scene& scene) -> void {
    auto snapshot = data(scene);
    auto completed = snapshot["checked"].get<std::vector<std::string>>();
    std::set<std::string> actual(completed.begin(), completed.end());

    for (const auto& member : declared(project)) {
      INFO(member);
      CHECK(actual.contains(member));
    }
  }
}  // namespace

TEST_CASE("feature_lab.inventory_matches_the_current_runtime_sdk") {
  Fixture fixture;
  const auto sdk
      = Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY)->readText("sdk/engine.ts").value();
  std::set<std::string> expected{"keys",
                                 "mouseButtons",
                                 "invoke",
                                 "HostError",
                                 "Entity.id",
                                 "ScriptComponent.self",
                                 "ScriptComponent.properties"};
  const std::regex member(R"(^  (?:(get|set) )?([A-Za-z][A-Za-z0-9_]*)\()", std::regex::multiline);

  for (const auto* name : {"input", "components", "physics", "audio", "animation", "scene",
                           "Entity", "ScriptComponent"}) {
    const bool isClass
        = std::string_view(name) == "Entity" || std::string_view(name) == "ScriptComponent";
    const auto prefix = std::string(isClass ? "export class " : "export const ") + name;
    const auto begin = sdk.find(prefix);
    REQUIRE(begin != std::string::npos);
    const auto end = sdk.find(isClass ? "\n}\n" : "\n});", begin);
    REQUIRE(end != std::string::npos);
    const auto block = sdk.substr(begin, end - begin);

    for (auto match = std::sregex_iterator(block.begin(), block.end(), member);
         match != std::sregex_iterator{}; ++match) {
      auto key = std::string(name) + "." + (*match)[2].str();

      if ((*match)[1].matched) {
        key += "." + (*match)[1].str();
      }

      expected.insert(std::move(key));
    }
  }

  const std::regex field(R"(^  readonly ([A-Za-z][A-Za-z0-9_]*)[:!])", std::regex::multiline);

  for (const auto* name : {"Entity", "ScriptComponent"}) {
    const auto begin = sdk.find(std::string("export class ") + name);
    const auto end = sdk.find("\n}\n", begin);
    const auto block = sdk.substr(begin, end - begin);

    for (auto match = std::sregex_iterator(block.begin(), block.end(), field);
         match != std::sregex_iterator{}; ++match) {
      expected.insert(std::string(name) + "." + (*match)[1].str());
    }
  }

  const std::regex constant(R"(^export const ([A-Za-z][A-Za-z0-9_]*))", std::regex::multiline);
  const std::set<std::string> namespaces{"input", "components", "physics",
                                         "audio", "animation",  "scene"};

  for (auto match = std::sregex_iterator(sdk.begin(), sdk.end(), constant);
       match != std::sregex_iterator{}; ++match) {
    if (!namespaces.contains((*match)[1].str())) {
      expected.insert((*match)[1].str());
    }
  }

  const std::regex function(R"(^export function ([A-Za-z][A-Za-z0-9_]*))", std::regex::multiline);

  for (auto match = std::sregex_iterator(sdk.begin(), sdk.end(), function);
       match != std::sregex_iterator{}; ++match) {
    expected.insert((*match)[1].str());
  }

  const std::regex className(R"(^export class ([A-Za-z][A-Za-z0-9_]*))", std::regex::multiline);
  std::set<std::string> classes;

  for (auto match = std::sregex_iterator(sdk.begin(), sdk.end(), className);
       match != std::sregex_iterator{}; ++match) {
    classes.insert((*match)[1].str());
  }

  CHECK(classes == std::set<std::string>{"Entity", "ScriptComponent"});
  CHECK(declared(fixture.project) == expected);
}

TEST_CASE("feature_lab_mouse_look_uses_native_ack_and_rotates_once_per_variable_frame") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  auto scene = session.scene().value();
  const auto camera = scene->entities()->front().id;
  const auto initial = scene->entity(camera)->transform.rotation;
  Platform::InputState input;
  REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::M, true, false}}));
  REQUIRE(input.consume({0, 0, Platform::PointerMoved{0, 0, 30, 20}}));
  REQUIRE(session.publish(input));
  report(session.update(0));
  CHECK(session.pointerCaptureRequested().value());
  CHECK(scene->entity(camera)->transform.rotation == initial);
  input.beginFrame();
  REQUIRE(input.consume({0, 0, Platform::PointerMoved{0, 0, 30, 20}}));
  REQUIRE(session.publish(input, true));
  report(session.fixedUpdate());
  report(session.fixedUpdate());
  CHECK(scene->entity(camera)->transform.rotation == initial);
  report(session.update(0));
  auto rotated = scene->entity(camera)->transform.rotation;
  CHECK(rotated.y < 0);
  CHECK(rotated.x < 0);
  CHECK(rotated.w == doctest::Approx(std::cos(0.045) * std::cos(0.03)));
  REQUIRE(session.publish(input));
  report(session.update(0));
  CHECK(scene->entity(camera)->transform.rotation == rotated);
  REQUIRE(session.publish(input, true));
  report(session.update(0));
  rotated = scene->entity(camera)->transform.rotation;
  CHECK(rotated.w == doctest::Approx(std::cos(0.09) * std::cos(0.06)));
  report(session.close());
}

TEST_CASE("feature_lab.full_sdk_executes_against_real_scene_input_physics_audio") {
  Fixture fixture;
  const auto generations = Script::compilerGenerations();
  const auto vm = Script::Runtime::backendMemory().bytes;
  const auto mixer = Audio::Mixer::backendMemory().bytes;
  auto session = fixture.session();
  auto scene = session.scene().value();
  report(session.start());
  Platform::InputState input;
  REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::D, true, false}}));
  REQUIRE(input.consume({0, 0, Platform::ButtonChanged{Platform::MouseButton::Left, true}}));
  REQUIRE(input.consume({0, 0, Platform::PointerMoved{25, 30, 4, 5}}));
  REQUIRE(input.consume({0, 0, Platform::Wheel{1, -2}}));
  REQUIRE(input.consume({0, 0, Platform::Text{"实验"}}));
  REQUIRE(session.publish(input));
  report(session.fixedUpdate());
  report(session.update(1.0 / 60));
  auto snapshot = data(*scene);
  CHECK(snapshot["input"]["keys"]["D"]["pressed"] == true);
  CHECK(snapshot["input"]["buttons"]["Left"]["down"] == true);
  CHECK(snapshot["input"]["pointer"]["x"] == 25);
  CHECK(snapshot["input"]["pointerDelta"]["y"] == 5);
  CHECK(snapshot["input"]["wheel"]["y"] == -2);
  CHECK(snapshot["input"]["text"] == "实验");
  REQUIRE_FALSE(snapshot["contacts"]["events"].empty());
  CHECK(snapshot["contacts"]["events"][0]["sensor"] == true);
  std::vector<float> samples(2048);
  REQUIRE(session.renderAudio(samples));
  CHECK(std::ranges::any_of(samples, [](float value) { return std::abs(value) > 0.0001F; }));
  report(session.update(1.0 / 60));
  report(session.close());
  snapshot = data(*scene);
  CHECK(snapshot["starts"] == 1);
  CHECK(snapshot["fixed"] == 1);
  CHECK(snapshot["updates"] == 2);
  CHECK(snapshot["stops"] == 1);
  CHECK(snapshot["errors"] == 6);
  complete(fixture.project, *scene);
  CHECK(Script::compilerGenerations() == generations);
  CHECK(Script::Runtime::backendMemory().bytes == vm);
  CHECK(Audio::Mixer::backendMemory().bytes == mixer);
}

TEST_CASE("feature_lab.idle_character_stays_supported_beside_moving_body_showcase") {
  Fixture fixture;
  auto session = fixture.session();
  const auto native = session.scene().value();
  const auto entities = native->entities().value();
  const auto found = std::ranges::find(entities, "Character controller", &World::EntityInfo::name);
  REQUIRE(found != entities.end());
  const auto actor = found->id;
  const auto before = native->transform(actor).value();
  report(session.start());

  for (int frame = 0; frame < 240; ++frame) {
    report(session.fixedUpdate());
    report(session.update(1.0 / 60));
  }

  const auto after = native->transform(actor).value();
  CHECK(after.position.x == doctest::Approx(before.position.x).epsilon(.01));
  CHECK(after.position.y == doctest::Approx(before.position.y).epsilon(.04));
  CHECK(after.position.z == doctest::Approx(before.position.z).epsilon(.01));
  report(session.close());
}

TEST_CASE("feature_lab.character_input_jump_and_hot_reload_keep_native_simulation") {
  Fixture fixture;
  auto session = fixture.session(true);
  const auto native = session.scene().value();
  const auto entities = native->entities().value();
  const auto found = std::ranges::find(entities, "Character controller", &World::EntityInfo::name);
  REQUIRE(found != entities.end());
  const auto actor = found->id;
  CHECK(actor.value > 9007199254740992ULL);
  report(session.start());

  for (int frame = 0; frame < 2; ++frame) {
    report(session.fixedUpdate());
    report(session.update(1.0 / 60));
  }

  const auto initial = native->transform(actor).value();
  Platform::InputState input;
  REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::Right, true, false}}));
  REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::J, true, false}}));
  REQUIRE(session.publish(input));
  report(session.fixedUpdate());
  report(session.update(1.0 / 60));
  CHECK(native->transform(actor)->position.x > initial.position.x);
  CHECK(native->transform(actor)->position.y > initial.position.y + .05F);
  input.beginFrame();
  REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::J, false, false}}));
  REQUIRE(session.publish(input));

  for (int frame = 0; frame < 24; ++frame) {
    report(session.fixedUpdate());
    report(session.update(1.0 / 60));
  }

  auto source = fixture.project.files()->readText("scripts/character.ts").value();
  const auto speed = source.find("3 / length");
  REQUIRE(speed != std::string::npos);
  source.replace(speed, 10, "4 / length");
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/character.ts", source));
  const auto before = native->transform(actor).value();
  auto reloaded = session.update(0);
  report(reloaded);
  CHECK(reloaded->source.committed);
  CHECK(native->transform(actor)->position == before.position);
  report(session.fixedUpdate());
  report(session.update(1.0 / 60));
  CHECK(native->transform(actor)->position.x - before.position.x
        == doctest::Approx(4.0 / 60).epsilon(.01));
  input.beginFrame();
  REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::Right, false, false}}));
  REQUIRE(session.publish(input));

  for (int frame = 0; frame < 90; ++frame) {
    report(session.fixedUpdate());
    report(session.update(1.0 / 60));
  }

  CHECK(native->transform(actor)->position.y == doctest::Approx(-1.5).epsilon(.04));
  CHECK(data(*native)["physics"]["characters"] == 1);
  report(session.close());
}

TEST_CASE("feature_lab.hot_reload_preserves_verified_checks_and_hook_counts") {
  Fixture fixture;
  auto session = fixture.session(true);
  report(session.start());
  report(session.fixedUpdate());
  report(session.update(0));
  auto before = data(*session.scene().value());
  auto source = fixture.project.files()->readText("scripts/audit.ts").value();
  source += "\nexport const revision = 2;\n";
  REQUIRE(fixture.project.files()->writeTextAtomic("scripts/audit.ts", source));
  auto reloaded = session.update(0);
  report(reloaded);
  CHECK(reloaded->source.committed);
  REQUIRE(reloaded->source.reload.has_value());
  CHECK(reloaded->source.reload->migrated == 2);
  auto after = data(*session.scene().value());
  CHECK(after["starts"] == before["starts"]);
  CHECK(after["fixed"] == before["fixed"]);
  CHECK(after["updates"] == before["updates"].get<int>() + 1);
  report(session.close());
  complete(fixture.project, *session.scene().value());
}

TEST_CASE("feature_lab.authored_normal_map_asset_preserves_baked_tangent_basis") {
  Fixture fixture;
  const auto scene = fixture.project.loadStartup().value();
  const auto entities = scene.entities().value();
  const auto entity = std::ranges::find(entities, "Quad", &World::EntityInfo::name);
  REQUIRE(entity != entities.end());
  REQUIRE(entity->mesh);
  CHECK(entity->mesh->kind == World::MeshKind::Asset);
  auto mesh = Assets::loadMesh(fixture.project.files().value(), entity->mesh->asset);
  REQUIRE(mesh);
  CHECK(Graphics::meshSurface(mesh->data)->hasTangents);

  for (const auto& vertex : mesh->data.vertices) {
    CHECK(vertex.tangent == Math::Vec4{1, 0, 0, -1});
  }
}

TEST_CASE("feature_lab.continuous_slider_reset_and_discrete_comparison_use_native_motion") {
  for (const auto mode : {"continuous", "discrete"}) {
    Fixture fixture;
    auto authored = fixture.project.loadStartup().value();
    auto json = Json::parse(World::encodeScene(authored).value());
    auto found = std::ranges::find_if(
        json["entities"], [](const Json& entity) { return entity["name"] == "CCD slider"; });
    REQUIRE(found != json["entities"].end());
    (*found)["rigidBody"]["collisionDetection"] = mode;
    authored = World::decodeScene(json.dump()).value();
    REQUIRE(fixture.project.saveStartup(authored));
    auto session = fixture.session(true);
    report(session.start());
    auto native = session.scene().value();
    const auto entities = native->entities().value();
    const auto slider = std::ranges::find(entities, "CCD slider", &World::EntityInfo::name);
    REQUIRE(slider != entities.end());
    Platform::InputState input;
    REQUIRE(session.publish(input));

    for (int tick = 0; tick < 8; ++tick) {
      report(session.fixedUpdate());
    }

    report(session.update(1.0 / 60));
    const auto contact = native->transform(slider->id)->position;

    if (std::string_view(mode) == "continuous") {
      CHECK(contact.x < -2.09F);
      CHECK(contact.x > -2.13F);
    } else {
      CHECK(contact.x > -.01F);
    }

    input.beginFrame();
    REQUIRE(input.consume({0, 0, Platform::KeyChanged{Platform::Key::T, true, false}}));
    REQUIRE(session.publish(input));
    report(session.update(0));
    CHECK(native->transform(slider->id)->position.x == -4);

    for (int tick = 0; tick < 8; ++tick) {
      report(session.fixedUpdate());
    }

    report(session.update(0));
    const auto resetContact = native->transform(slider->id)->position;

    if (std::string_view(mode) == "continuous") {
      CHECK(resetContact.x < -2.09F);
      CHECK(resetContact.x > -2.13F);
    } else {
      CHECK(resetContact.x == doctest::Approx(0).epsilon(.01));
    }

    auto source = fixture.project.files()->readText("scripts/collision.ts").value();
    REQUIRE(fixture.project.files()->writeTextAtomic("scripts/collision.ts", source + "\n"));
    auto reloaded = session.update(0);
    report(reloaded);
    REQUIRE(reloaded->source.committed);
    CHECK(native->transform(slider->id)->position == resetContact);
    report(session.close());
  }
}
