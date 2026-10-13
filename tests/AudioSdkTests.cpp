#include <algorithm>
#include <array>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "SceneAudioFixture.hpp"

import Dot.Script.Compiler;
import Dot.Script.SceneScripts;
import Dot.Script.AudioSession;
import Dot.World.Scene;
import Dot.Core.Result;

namespace {
  using namespace Dot::Script;

  struct Project {
    DotTest::Audio::Directory directory;

    Project() {
      auto files = directory.files();
      REQUIRE(files.createDirectories("scripts"));

      std::vector<float> samples(4096, 0.25F);
      std::fill(samples.begin() + 2048, samples.end(), 0.5F);
      DotTest::Audio::writeWave(files, "tone.wav", samples);
    }

    auto write(std::string_view source) const -> void {
      REQUIRE(directory.files().writeTextAtomic("scripts/actor.ts", source));
    }

    auto compile() const -> CompiledProgram {
      auto compiler = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
      auto result = compiler.compileProject(directory.files());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Audio SDK");
    REQUIRE(result);
    auto native = std::make_shared<Dot::World::Scene>(std::move(*result));
    auto actor = native->createEntity("Actor").value();
    Dot::World::AudioSource source;
    source.asset = "tone.wav";
    source.playOnStart = false;
    REQUIRE(native->setAudioSource(actor, source));
    REQUIRE(native->setScript(actor, Dot::World::ScriptComponent{"actor.ts", "Actor"}));
    return native;
  }

  auto good(Dot::Core::Result<ScriptReport> result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->failures.empty());
    REQUIRE_FALSE(result->realmFaulted);
  }

  auto service(const std::shared_ptr<Dot::World::Scene>& scene, const CompiledProgram& program,
               const AudioSession& session) -> SceneScripts {
    SceneScriptsConfig config;
    config.services = session.bindings().value();
    auto result = SceneScripts::create(scene, program, config);
    REQUIRE(result);
    return std::move(*result);
  }
}  // namespace

TEST_CASE("audio_sdk_real_typescript_seek_gain_play_pause_stop_and_owned_state_control_pcm") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, audio} from 'dot:engine';

export class Actor extends ScriptComponent {
  #ticks = 0;

  onStart(): void {
    const state = this.self.audioVoice;
    state.settings.volume = 0;

    if (this.self.audioVoice.settings.volume !== 1 || state.clip.frames !== '4096') {
      throw Error('voice snapshot ownership');
    }

    this.self.seekAudio('2048');
    audio.setVolume(0.5);
    this.self.playAudio();
  }

  onUpdate(): void {
    if (++this.#ticks === 1) {
      this.self.pauseAudio();
    } else {
      this.self.stopAudio();

      if (this.self.audioVoice.sourceFrame !== '0' || this.self.audioVoice.playing) {
        throw Error('stop did not rewind');
      }

      this.self.playAudio();
    }
  }

  onStop(): void {
    this.self.stopAudio();
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto audio = AudioSession::create(native, project.directory.files()).value();
  auto scripts = service(native, program, audio);
  good(scripts.start());
  std::array<float, 32> pcm;
  REQUIRE(audio.render(pcm));
  CHECK(std::ranges::all_of(pcm, [](float value) { return value == doctest::Approx(0.25); }));
  good(scripts.update(0.1));
  REQUIRE(audio.render(pcm));
  CHECK(std::ranges::all_of(pcm, [](float value) { return value == 0; }));
  good(scripts.update(0.1));
  REQUIRE(audio.render(pcm));
  CHECK(std::ranges::all_of(pcm, [](float value) { return value == doctest::Approx(0.125); }));
  good(scripts.stop());
}

TEST_CASE("audio_sdk_preparation_and_reload_allow_reads_deny_controls_and_keep_paused_cursor") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, audio} from 'dot:engine';
import type {Entity, ScriptContext, HostError} from 'dot:engine';

function inspect(self: Entity): void {
  self.audioVoice;
  audio.stats();
  audio.listener();
  const attempts = [() => self.playAudio(), () => self.pauseAudio(), () => self.stopAudio(),
    () => self.seekAudio('0'), () => audio.setVolume(0)];

  for (const attempt of attempts) {
    let denied = false;

    try {
      attempt();
    } catch (error) {
      denied = (error as HostError).code === 'InvalidState';
    }

    if (!denied) {
      throw Error('audio write during preparation');
    }
  }
}

export class Actor extends ScriptComponent {
  constructor(context: ScriptContext) {
    super(context);
    inspect(this.self);
  }

  onStart(): void {
    this.self.seekAudio('123');
  }

  onSaveState(): {cursor: string} {
    inspect(this.self);
    return {cursor: this.self.audioVoice.sourceFrame};
  }

  onLoadState(state: {cursor: string}): void {
    inspect(this.self);

    if (state.cursor !== this.self.audioVoice.sourceFrame) {
      throw Error('native voice was replaced');
    }
  }

  onUpdate(): void {
    this.self.name = this.self.audioVoice.sourceFrame + ':' + audio.stats().generation;
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto actor = native->entities()->front().id;
  auto audio = AudioSession::create(native, project.directory.files()).value();
  auto scripts = service(native, program, audio);
  good(scripts.start());
  auto moved = std::move(audio);
  auto reloaded = scripts.reload(program);
  REQUIRE(reloaded);
  CHECK(reloaded->migrated == 1);
  good(scripts.update(0.1));
  CHECK(native->entity(actor)->name == "123:1");
  REQUIRE(moved.reset());
  good(scripts.update(0.1));
  CHECK(native->entity(actor)->name == "0:2");
  good(scripts.stop());
}

TEST_CASE("audio_sdk_invalid_controls_and_missing_voices_are_catchable_without_mutation") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, scene, audio} from 'dot:engine';
import type {HostError} from 'dot:engine';

export class Actor extends ScriptComponent {
  onStart(): void {
    let invalid = false;

    try {
      this.self.seekAudio('4096');
    } catch (error) {
      invalid = (error as HostError).code === 'InvalidArgument';
    }

    let absent = false;
    const missing = scene.create('No Voice');

    try {
      missing.playAudio();
    } catch (error) {
      absent = (error as HostError).code === 'NotFound';
    }

    missing.destroy();

    if (!invalid || !absent || this.self.audioVoice.sourceFrame !== '0' || audio.stats().playing !== 0) {
      throw Error('bad audio control contract');
    }

    this.self.playAudio();
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto audio = AudioSession::create(native, project.directory.files()).value();
  auto scripts = service(native, program, audio);
  good(scripts.start());
  std::array<float, 32> pcm;
  REQUIRE(audio.render(pcm));
  CHECK(std::ranges::all_of(pcm, [](float value) { return value == doctest::Approx(0.25); }));
  CHECK_FALSE(scripts.stats()->runtime.faulted);
  good(scripts.stop());
}

TEST_CASE("audio_sdk_source_setting_edits_apply_on_native_sync_and_keep_manual_pause") {
  Project project;
  project.write(R"ts(
import {ScriptComponent, audio} from 'dot:engine';

export class Actor extends ScriptComponent {
  onStart(): void {
    this.self.seekAudio('100');
    const source = this.self.audioSource!;
    source.voice.volume = 0.5;
    source.voice.pitch = 2;
    source.voice.looping = true;
    this.self.audioSource = source;
  }

  onUpdate(): void {
    const state = this.self.audioVoice;

    if (state.settings.volume !== 0.5 || state.settings.pitch !== 2 || !state.settings.looping ||
        state.playing || state.sourceFrame !== '100' || audio.stats().assets !== 1) {
      throw Error('synchronized settings/cursor/manual state');
    }

    this.self.playAudio();
  }
}
)ts");
  auto program = project.compile();
  auto native = scene();
  auto audio = AudioSession::create(native, project.directory.files()).value();
  auto scripts = service(native, program, audio);
  good(scripts.start());
  REQUIRE(audio.synchronize());
  good(scripts.update(0.1));
  std::array<float, 64> pcm;
  REQUIRE(audio.render(pcm));
  CHECK(std::ranges::any_of(pcm, [](float value) { return value > 0; }));
  good(scripts.stop());
}

TEST_CASE("audio_sdk_typecheck_rejects_numeric_frames_wrong_ids_and_native_scheduling_calls") {
  Project project;

  for (const auto* source :
       {"import {audio} from 'dot:engine';audio.seek({scene:'1',value:'1'},0);",
        "import {audio} from 'dot:engine';audio.play({scene:1,value:2});",
        "import {audio} from 'dot:engine';audio.render(256);",
        "import {scene} from 'dot:engine';scene.create().seekAudio(1);",
        "import {audio} from 'dot:engine';audio.setVolume('0.5');"}) {
    project.write(source);
    auto compiler = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER).value();
    auto result = compiler.compileProject(project.directory.files());
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}
