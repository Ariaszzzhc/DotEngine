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
    REQUIRE_FALSE(frame.realmFaulted);

    for (const auto& failure : frame.failures) {
      FAIL_CHECK(failure.error.message);
    }

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

  for (const auto* name :
       {"input", "components", "physics", "audio", "scene", "Entity", "ScriptComponent"}) {
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
  const std::set<std::string> namespaces{"input", "components", "physics", "audio", "scene"};

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
  report(session.close());
  snapshot = data(*scene);
  CHECK(snapshot["starts"] == 1);
  CHECK(snapshot["fixed"] == 1);
  CHECK(snapshot["updates"] == 1);
  CHECK(snapshot["stops"] == 1);
  CHECK(snapshot["errors"] == 5);
  complete(fixture.project, *scene);
  CHECK(Script::compilerGenerations() == generations);
  CHECK(Script::Runtime::backendMemory().bytes == vm);
  CHECK(Audio::Mixer::backendMemory().bytes == mixer);
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
  CHECK(reloaded->source.reload->migrated == 1);
  auto after = data(*session.scene().value());
  CHECK(after["starts"] == before["starts"]);
  CHECK(after["fixed"] == before["fixed"]);
  CHECK(after["updates"] == before["updates"].get<int>() + 1);
  report(session.close());
  complete(fixture.project, *session.scene().value());
}
