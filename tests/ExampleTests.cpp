#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
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

  auto title(Project::GameSession& session) -> std::string {
    return session.scene().value()->name().value();
  }

  class Driver {
  public:
    explicit Driver(Project::GameSession& session) : m_session(session) {
    }

    auto frame(std::initializer_list<Platform::Key> down = {}) -> void {
      m_input.beginFrame();
      constexpr std::array keys{Platform::Key::A, Platform::Key::D,     Platform::Key::W,
                                Platform::Key::S, Platform::Key::Enter, Platform::Key::P,
                                Platform::Key::R, Platform::Key::Space};

      for (auto key : keys) {
        const bool desired = std::ranges::find(down, key) != down.end();

        if (m_input.key(key).down != desired) {
          REQUIRE(m_input.consume({0, 0, Platform::KeyChanged{key, desired, false}}));
        }
      }

      good(m_session.publish(m_input));
      report(m_session.fixedUpdate());
      report(m_session.update(1.0 / 60));
    }

    auto tap(Platform::Key key) -> void {
      frame({key});
      frame();
    }

    auto drive(float x, float z) -> void {
      for (int count = 0; count < 400; ++count) {
        const auto position = entity(m_session, "Player").transform.position;

        if (std::hypot(position.x - x, position.z - z) < 0.15F
            || title(m_session).find("Complete!") != std::string::npos) {
          frame();
          return;
        }

        const auto horizontal = x - position.x;
        const auto vertical = z - position.z;

        if (std::abs(horizontal) > 0.08F && std::abs(vertical) > 0.08F) {
          frame({horizontal > 0 ? Platform::Key::D : Platform::Key::A,
                 vertical > 0 ? Platform::Key::S : Platform::Key::W});
        } else if (std::abs(horizontal) > 0.08F) {
          frame({horizontal > 0 ? Platform::Key::D : Platform::Key::A});
        } else {
          frame({vertical > 0 ? Platform::Key::S : Platform::Key::W});
        }
      }

      FAIL("Native player did not reach the requested route point");
    }

  private:
    Project::GameSession& m_session;
    Platform::InputState m_input;
  };

  struct Model {
    Fixture fixture;
    Script::CompiledProgram program;
    Script::Runtime runtime;
    Script::ModuleId module;

    Model()
        : program([&]() {
            REQUIRE(fixture.project.files()->writeTextAtomic("scripts/driver.ts", R"(
import {RelayRules, type Settings, type Checkpoint} from './rules';
export function run(request: {settings: Settings; targets: string[];
  ops: {op: string; name?: string; seconds?: number; state?: Checkpoint}[]}) {
  const rules = new RelayRules(request.settings, request.targets);
  const errors: string[] = [];
  for (const action of request.ops) {
    try {
      switch(action.op) {
        case 'start': rules.start(); break;
        case 'pause': rules.pause(); break;
        case 'reset': rules.reset(); break;
        case 'hit': rules.hit(); break;
        case 'tick': rules.tick(action.seconds!); break;
        case 'collect': rules.collect(action.name!); break;
        case 'restore': rules.restore(action.state!); break;
        case 'mutate-copy': {const copy=rules.state; copy.collected.push('foreign'); break;}
        default: throw new Error('Unknown test action');
      }
    } catch(error) {errors.push(String(error));}
  }
  return {state:rules.state, errors};
})"));
            auto compiled = fixture.compiler().compileProject(fixture.project.files().value());
            good(compiled);
            return std::move(*compiled);
          }()),
          runtime(Script::Runtime::create(program.files().value()).value()),
          module(runtime.loadModule("driver.js").value()) {
    }

    auto run(Json operations, Json settings = {{"speed", 5}, {"timeLimit", 2}, {"maxHealth", 3}})
        -> Json {
      auto args = Json::array(
                      {{{"settings", settings}, {"targets", {"one", "two"}}, {"ops", operations}}})
                      .dump();
      auto result = runtime.call(module, "run", args);
      good(result);
      REQUIRE(result->has_value());
      return Json::parse(**result);
    }
  };
}  // namespace

TEST_CASE("prism.rules_round_pause_win_loss_reset_and_owned_checkpoints") {
  Model model;
  auto ready = model.run(Json::array({{{"op", "tick"}, {"seconds", 1}}, {{"op", "hit"}}}));
  CHECK(ready["state"]["phase"] == "ready");
  CHECK(ready["state"]["health"] == 3);
  auto won = model.run(Json::array({{{"op", "start"}},
                                    {{"op", "collect"}, {"name", "one"}},
                                    {{"op", "collect"}, {"name", "one"}},
                                    {{"op", "pause"}},
                                    {{"op", "tick"}, {"seconds", 1}},
                                    {{"op", "pause"}},
                                    {{"op", "mutate-copy"}},
                                    {{"op", "collect"}, {"name", "two"}},
                                    {{"op", "hit"}}}));
  CHECK(won["state"]["phase"] == "won");
  CHECK(won["state"]["collected"] == Json::array({"one", "two"}));
  CHECK(won["state"]["elapsed"] == 0);
  CHECK(won["state"]["health"] == 3);
  auto lost = model.run(Json::array({{{"op", "start"}},
                                     {{"op", "hit"}},
                                     {{"op", "hit"}},
                                     {{"op", "tick"}, {"seconds", 1}},
                                     {{"op", "hit"}},
                                     {{"op", "tick"}, {"seconds", 1}}}));
  CHECK(lost["state"]["phase"] == "lost");
  CHECK(lost["state"]["health"] == 1);
  auto reset = model.run(
      Json::array({{{"op", "start"}}, {{"op", "collect"}, {"name", "one"}}, {{"op", "reset"}}}));
  CHECK(reset["state"] == ready["state"]);
}

TEST_CASE("prism.rules_invalid_inputs_and_restore_preserve_previous_state") {
  Model model;
  auto checkpoint = Json{{"phase", "paused"},
                         {"elapsed", 0.5},
                         {"health", 2},
                         {"immunity", 0.2},
                         {"collected", {"one"}}};
  auto restored = model.run(Json::array({{{"op", "restore"}, {"state", checkpoint}}}));
  CHECK(restored["state"] == checkpoint);
  auto invalid = checkpoint;
  invalid["collected"] = {"foreign"};
  auto failed = model.run(Json::array({{{"op", "restore"}, {"state", checkpoint}},
                                       {{"op", "restore"}, {"state", invalid}},
                                       {{"op", "tick"}, {"seconds", -1}},
                                       {{"op", "collect"}, {"name", "foreign"}}}));
  CHECK(failed["state"] == checkpoint);
  CHECK(failed["errors"].size() == 3);
  invalid = checkpoint;
  invalid["phase"] = "won";
  failed = model.run(Json::array({{{"op", "restore"}, {"state", invalid}}}));
  CHECK(failed["state"]["phase"] == "ready");
  CHECK(failed["errors"].size() == 1);

  for (const auto& settings :
       std::vector<Json>{{{"speed", 0}, {"timeLimit", 2}, {"maxHealth", 3}},
                         {{"speed", 5}, {"timeLimit", 301}, {"maxHealth", 3}},
                         {{"speed", 5}, {"timeLimit", 2}, {"maxHealth", 4}}}) {
    const auto args
        = Json::array(
              {{{"settings", settings}, {"targets", {"one", "two"}}, {"ops", Json::array()}}})
              .dump();
    CHECK_FALSE(model.runtime.call(model.module, "run", args));
  }

  CHECK(model.run(Json::array())["state"]["phase"] == "ready");
}

TEST_CASE("prism.native_physics_route_collects_eight_and_restarts") {
  Fixture fixture;
  auto session = fixture.session();
  const auto original = fixture.project.loadStartup()->name().value();
  report(session.start());
  Driver driver(session);
  driver.tap(Platform::Key::Enter);
  driver.drive(5.5F, 5.5F);
  driver.drive(5.5F, -5.5F);
  driver.drive(-5.5F, -5.5F);
  driver.drive(-5.5F, 5.5F);
  CHECK(title(session).find("Complete!") != std::string::npos);
  CHECK(title(session).find("8/8") != std::string::npos);

  for (int index = 1; index <= 8; ++index) {
    CHECK_FALSE(entity(session, "CrystalVisual" + std::to_string(index)).mesh->visible);
  }

  driver.tap(Platform::Key::R);
  CHECK(title(session).find("Enter to start") != std::string::npos);
  CHECK(title(session).find("0/8") != std::string::npos);
  CHECK(entity(session, "Player").transform.position.x == doctest::Approx(-5.5));
  CHECK(entity(session, "Player").transform.position.z == doctest::Approx(5.5));
  CHECK(fixture.project.loadStartup()->name().value() == original);

  for (int index = 1; index <= 8; ++index) {
    CHECK(entity(session, "CrystalVisual" + std::to_string(index)).mesh->visible);
  }

  driver.tap(Platform::Key::Enter);

  for (int frame = 0; frame < 180; ++frame) {
    driver.frame({Platform::Key::D});
  }

  const auto position = entity(session, "Player").transform.position.x;
  CHECK(position > 6.9F);
  CHECK(position < 7.1F);
  report(session.close());
}

TEST_CASE("prism.native_hazard_damage_pause_and_defeat_use_contacts") {
  Fixture fixture;
  auto session = fixture.session();
  report(session.start());
  Driver driver(session);
  driver.tap(Platform::Key::Enter);
  driver.drive(-1.5F, 0);
  CHECK(title(session).find("2 HP") != std::string::npos);
  driver.tap(Platform::Key::P);
  const auto paused = entity(session, "Player").transform.position;

  for (int frame = 0; frame < 30; ++frame) {
    driver.frame({Platform::Key::D});
  }

  CHECK(title(session).find("P to resume") != std::string::npos);
  CHECK(title(session).find("2 HP") != std::string::npos);
  CHECK(entity(session, "Player").transform.position.x == doctest::Approx(paused.x));
  driver.tap(Platform::Key::P);

  for (int frame = 0; frame < 150; ++frame) {
    driver.frame();
  }

  CHECK(title(session).find("Game over!") != std::string::npos);
  CHECK(title(session).find("0 HP") != std::string::npos);
  std::vector<float> pcm(4096);
  good(session.renderAudio(pcm));
  CHECK(std::ranges::any_of(pcm, [](float value) { return std::abs(value) > 0.001F; }));
  report(session.close());
}

TEST_CASE("prism.hot_reload_preserves_round_and_native_scene") {
  Fixture fixture;
  auto session = fixture.session(true);
  report(session.start());
  Driver driver(session);
  driver.tap(Platform::Key::Enter);
  driver.drive(-3, 5.5F);
  const auto before = title(session);
  const auto sceneId = session.scene().value()->id();
  auto files = fixture.project.files().value();
  auto source = files.readText("scripts/spin.ts").value();
  const auto position = source.find("seconds * 1.5");
  REQUIRE(position != std::string::npos);
  source.replace(position, std::string("seconds * 1.5").size(), "seconds * 2.0");
  good(files.writeTextAtomic("scripts/spin.ts", source));
  auto reloaded = session.update(0);
  report(reloaded);
  CHECK(reloaded->source.committed);
  REQUIRE(reloaded->source.reload.has_value());
  CHECK(reloaded->source.reload->migrated == 9);
  CHECK(session.scene().value()->id() == sceneId);
  CHECK(title(session) == before);
  CHECK_FALSE(entity(session, "CrystalVisual1").mesh->visible);
  report(session.close());
}
