#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <utility>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.StagedDirectory;
import Dot.Script.AnimationSession;
import Dot.Script.Compiler;
import Dot.Script.SceneScripts;
import Dot.World.Scene;

namespace {
  using namespace Dot;
  using Json = nlohmann::json;

  struct Fixture {
    Core::StagedDirectory directory
        = Core::StagedDirectory::create(
              Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
              .value();
    Core::FileSystem files = directory.files().value();
    std::shared_ptr<World::Scene> scene
        = std::make_shared<World::Scene>(World::Scene::create().value());
    World::EntityId actor = scene->createEntity("Actor").value();
    World::AnimationPlayer player{{"assets/move.dotanimation", "assets/scale.dotanimation"},
                                  {{0, actor.value}},
                                  0,
                                  1,
                                  true,
                                  false,
                                  true};

    Fixture() {
      files.createDirectories("scripts").value();
      files.createDirectories("assets").value();
      scene->setTransform(actor, {{7, 0, 0}}).value();
      scene->setAnimationPlayer(actor, player).value();
      scene->setScript(actor, World::ScriptComponent{"actor.ts", "Actor"}).value();
      auto move = Animation::Clip::create({"Move",
                                           {{0,
                                             Animation::Path::Translation,
                                             Animation::Interpolation::Linear,
                                             {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}}}})
                      .value();
      auto scale = Animation::Clip::create({"Scale",
                                            {{0,
                                              Animation::Path::Scale,
                                              Animation::Interpolation::Linear,
                                              {{0, {1, 1, 1, 0}}, {2, {3, 3, 3, 0}}}}}})
                       .value();
      Animation::saveClip(files, "assets/move.dotanimation", move).value();
      Animation::saveClip(files, "assets/scale.dotanimation", scale).value();
    }

    auto compile(std::string_view source) -> Core::Result<Script::CompiledProgram> {
      files.writeTextAtomic("scripts/actor.ts", source).value();
      return Script::TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER)->compileProject(files);
    }

    auto scripts(const Script::CompiledProgram& program, const Script::AnimationSession& animation)
        -> Script::SceneScripts {
      Script::SceneScriptsConfig config;
      config.services = animation.bindings().value();
      return Script::SceneScripts::create(scene, program, config).value();
    }
  };

  auto good(Core::Result<Script::ScriptReport> result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE_FALSE(result->realmFaulted);
    REQUIRE(result->failures.empty());
  }
}  // namespace

TEST_CASE("animation_sdk_real_typescript_controls_native_pose_clock_library_and_snapshots") {
  Fixture fixture;
  auto compiled = fixture.compile(R"ts(
import {animation, ScriptComponent} from 'dot:engine';

export class Actor extends ScriptComponent {
  onStart(): void {
    const state = this.self.animationState;
    state.speed = 8;

    if (animation.state(this.self.id).speed !== 1 || state.asset !== 'assets/move.dotanimation') {
      throw Error('animation snapshot ownership');
    }

    this.self.playAnimation(0, true);
  }

  onUpdate(): void {
    if (this.self.transform.position.x !== 1 || this.self.animationState.timeSeconds !== 0.5) {
      throw Error('native animation advance');
    }

    animation.pause(this.self.id);
    animation.seek(this.self.id, 1);
    this.self.playAnimation(1, true);
    this.self.configureAnimation(-1, false);
    animation.play(this.self.id, null, true);
    this.self.pauseAnimation();
    this.self.seekAnimation(1);

    if (Math.abs(this.self.transform.position.x - 7) > 1e-6 || this.self.transform.scale.x !== 2 ||
        this.self.animationState.clip !== 1 || this.self.animationState.playing) {
      throw Error('atomic native clip switch');
    }

    animation.configure(this.self.id, 1, false);
    this.self.playAnimation();
  }

  onStop(): void {
    animation.stop(this.self.id);
    this.self.stopAnimation();
  }
}
)ts");
  REQUIRE_MESSAGE(compiled, (compiled ? "" : compiled.error().message));
  auto session = Script::AnimationSession::create(fixture.scene, fixture.files).value();
  auto scripts = fixture.scripts(*compiled, session);
  good(scripts.start());
  REQUIRE(session.advance(.5));
  good(scripts.update(.1));
  REQUIRE(session.advance(.5));
  CHECK(fixture.scene->transform(fixture.actor)->scale.x == 2.5F);
  good(scripts.stop());
  CHECK(fixture.scene->transform(fixture.actor)->scale.x == 1);
  CHECK(fixture.scene->transform(fixture.actor)->position.x == 7);
  CHECK(fixture.scene->entity(fixture.actor)->animation == fixture.player);
}

TEST_CASE("animation_sdk_prepare_and_reload_reads_preserve_clock_and_deny_all_writes") {
  Fixture fixture;
  auto compiled = fixture.compile(R"ts(
import {animation, ScriptComponent, type ScriptContext, type HostError} from 'dot:engine';

function inspect(self: ScriptComponent['self']): void {
  const time = self.animationState.timeSeconds;

  for (const action of [() => self.playAnimation(), () => self.pauseAnimation(),
                       () => self.stopAnimation(), () => self.seekAnimation(1),
                       () => self.configureAnimation(1, false)]) {
    let denied = false;

    try {
      action();
    } catch (error) {
      denied = (error as HostError).code === 'InvalidState';
    }

    if (!denied || self.animationState.timeSeconds !== time) {
      throw Error('animation preparation must be observational');
    }
  }
}

export class Actor extends ScriptComponent {
  constructor(context: ScriptContext) {
    super(context);
    inspect(this.self);
  }

  onStart(): void { this.self.playAnimation(); }

  onSaveState(): {time: number} {
    inspect(this.self);
    return {time: this.self.animationState.timeSeconds};
  }

  onLoadState(state: {time: number}): void {
    inspect(this.self);

    if (state.time !== animation.state(this.self.id).timeSeconds) {
      throw Error('reload replaced native clock');
    }
  }
}
)ts");
  REQUIRE_MESSAGE(compiled, (compiled ? "" : compiled.error().message));
  auto session = Script::AnimationSession::create(fixture.scene, fixture.files).value();
  auto scripts = fixture.scripts(*compiled, session);
  good(scripts.start());
  REQUIRE(session.advance(.5));
  auto moved = std::move(session);
  auto reloaded = scripts.reload(*compiled);
  REQUIRE(reloaded);
  CHECK(reloaded->migrated == 1);
  REQUIRE(moved.advance(.5));
  CHECK(fixture.scene->transform(fixture.actor)->position.x == 2);
  good(scripts.stop());
}

TEST_CASE("animation_sdk_invalid_controls_foreign_missing_and_disabled_owners_are_catchable") {
  Fixture fixture;
  auto compiled = fixture.compile(R"ts(
import {animation, scene, ScriptComponent, type HostError} from 'dot:engine';

export class Actor extends ScriptComponent {
  onStart(): void {
    const missing = scene.create('No player');
    const actions: [() => void, string][] = [
      [() => this.self.playAnimation(2), 'InvalidArgument'],
      [() => this.self.seekAnimation(-1), 'InvalidArgument'],
      [() => this.self.seekAnimation(3), 'InvalidArgument'],
      [() => this.self.configureAnimation(17, true), 'InvalidArgument'],
      [() => missing.playAnimation(), 'NotFound'],
      [() => animation.pause({scene: '99999', value: this.self.id.value}), 'InvalidArgument']
    ];

    for (const [action, code] of actions) {
      let caught = false;

      try { action(); } catch (error) { caught = (error as HostError).code === code; }

      if (!caught || this.self.animationState.timeSeconds !== 0 || this.self.animationState.playing) {
        throw Error('invalid animation controls must preserve state');
      }
    }

    missing.destroy();
  }
}
)ts");
  REQUIRE_MESSAGE(compiled, (compiled ? "" : compiled.error().message));
  auto session = Script::AnimationSession::create(fixture.scene, fixture.files).value();
  auto scripts = fixture.scripts(*compiled, session);
  good(scripts.start());
  CHECK(fixture.scene->transform(fixture.actor)->position.x == 7);
  good(scripts.stop());
  fixture.player.enabled = false;
  REQUIRE(fixture.scene->setAnimationPlayer(fixture.actor, fixture.player));
  auto bindings = session.bindings().value();
  const auto args = Json::array({{{"scene", std::to_string(fixture.actor.scene)},
                                  {"value", std::to_string(fixture.actor.value)}}})
                        .dump();
  CHECK_FALSE(bindings.front().callback(args));
}

TEST_CASE("animation_sdk_host_arguments_lifetime_moves_threads_and_replaced_scope_are_checked") {
  Fixture fixture;
  std::weak_ptr<World::Scene> weak = fixture.scene;
  auto session = Script::AnimationSession::create(fixture.scene, fixture.files).value();
  auto bindings = session.bindings().value();
  auto state = bindings.front().callback;
  const auto args = Json::array({{{"scene", std::to_string(fixture.actor.scene)},
                                  {"value", std::to_string(fixture.actor.value)}}})
                        .dump();

  for (const auto* bad : {"", "null", "{}", "[]", "[1]", "[{\"scene\":\"01\",\"value\":\"1\"}]",
                          "[{\"scene\":\"1\",\"scene\":\"1\",\"value\":\"1\"}]"}) {
    CHECK_FALSE(state(bad));
  }

  CHECK_FALSE(state(std::string(4097, 'x')));
  const auto id = Json::parse(args)[0];

  for (const auto& index : {Json(-1), Json(1.5), Json(1024), Json("1")}) {
    CHECK_FALSE(bindings[1].callback(Json::array({id, index, false}).dump()));
  }

  CHECK_FALSE(bindings[1].callback(Json::array({id, nullptr, "true"}).dump()));
  CHECK_FALSE(bindings[4].callback(Json::array({id, "1"}).dump()));
  CHECK_FALSE(bindings[5].callback(Json::array({id, 1, "true"}).dump()));

  bool rejected{};
  std::thread worker(
      [&] { rejected = !state(args) && !session.advance(0) && !session.bindings(); });
  worker.join();
  CHECK(rejected);
  auto moved = std::move(session);
  CHECK_FALSE(session.advance(0));
  CHECK_FALSE(session.bindings());
  fixture.scene.reset();
  CHECK_FALSE(weak.expired());
  REQUIRE(state(args));
  auto scene = weak.lock();
  auto original = std::move(*scene);
  CHECK_FALSE(state(args));
  *scene = std::move(original);
  REQUIRE(state(args));
  scene.reset();
  bindings.clear();
  {
    auto retired = std::move(moved);
  }
  CHECK_FALSE(weak.expired());
  REQUIRE(state(args));
  state = {};
  CHECK(weak.expired());
  CHECK_FALSE(Script::AnimationSession::create({}, fixture.files));
}

TEST_CASE("animation_sdk_typecheck_rejects_wrong_ids_controls_and_script_scheduling") {
  Fixture fixture;

  for (const auto* source :
       {"import {animation} from 'dot:engine';animation.play({scene:1,value:2});",
        "import {animation} from 'dot:engine';animation.seek({scene:'1',value:'1'},'1');",
        "import {animation} from 'dot:engine';animation.advance(1);",
        "import {animation} from 'dot:engine';animation.play({scene:'1',value:'1'},'Move');",
        "import {scene} from 'dot:engine';scene.create().configureAnimation(1,'true');"}) {
    auto result = fixture.compile(source);
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}
