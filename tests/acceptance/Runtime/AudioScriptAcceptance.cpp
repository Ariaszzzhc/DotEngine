#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.World.Scene;
import Dot.Audio.Mixer;
import Dot.Audio.Output;
import Dot.Audio.Playback;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Script.AudioSession;

namespace Dot::Runtime {
  namespace {
    auto wave(const Core::FileSystem& files) -> Core::Result<void> {
      constexpr std::size_t frames = 48000;
      std::vector<std::byte> bytes(44 + frames * 4);
      const auto put = [&](std::size_t offset, std::uint64_t value, std::size_t count) {
        for (std::size_t byte = 0; byte < count; ++byte) {
          bytes[offset + byte] = static_cast<std::byte>((value >> (8 * byte)) & 255);
        }
      };
      const auto text = [&](std::size_t offset, std::string_view value) {
        for (auto character : value) {
          bytes[offset++] = static_cast<std::byte>(character);
        }
      };
      text(0, "RIFF");
      put(4, bytes.size() - 8, 4);
      text(8, "WAVEfmt ");
      put(16, 16, 4);
      put(20, 3, 2);
      put(22, 1, 2);
      put(24, 48000, 4);
      put(28, 48000 * 4, 4);
      put(32, 4, 2);
      put(34, 32, 2);
      text(36, "data");
      put(40, frames * 4, 4);

      for (std::size_t frame = 0; frame < frames; ++frame) {
        put(44 + frame * 4, std::bit_cast<std::uint32_t>(0.5F), 4);
      }

      return files.writeAtomic("script-audio-acceptance/tone.wav", bytes);
    }
  }  // namespace

  auto testScriptAudio(const std::filesystem::path& executable,
                       const std::filesystem::path& sdkRoot, const std::filesystem::path& root,
                       bool native) -> int {
    auto files = Core::FileSystem::open(root);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Script Audio");

    if (!files || !sdk || !compiler || !created
        || !files->createDirectories("script-audio-acceptance/dot") || !wave(*files)) {
      std::println(stderr, "Script audio acceptance setup failed");
      return 1;
    }

    for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
      auto source = sdk->readText(path);

      if (!source
          || !files->writeTextAtomic(std::filesystem::path{"script-audio-acceptance"} / path,
                                     *source)) {
        std::println(stderr, "Script audio SDK copy failed");
        return 1;
      }
    }

    auto writeActor = [&](int version) -> Core::Result<void> {
      std::string source = R"ts(
import {ScriptComponent, audio} from './dot/engine.js';
import type {HostError} from './dot/engine.js';

export class Actor extends ScriptComponent {
  #ticks = 0;
  #cursor = '0';

  onStart(): void {
    let rejected = false;

    try {
      this.self.seekAudio('48000');
    } catch (error) {
      rejected = (error as HostError).code === 'InvalidArgument';
    }

    if (!rejected || this.self.audioVoice.clip.frames !== '48000') {
      throw Error('clip/invalid seek contract');
    }

    this.self.seekAudio('1000');
    audio.setVolume(0.5);
    this.self.playAudio();
  }

  onUpdate(): void {
    if (++this.#ticks === 1) {
      this.self.pauseAudio();
      this.#cursor = this.self.audioVoice.sourceFrame;
      this.self.name = 'paused';
    } else {
      this.self.stopAudio();

      if (this.self.audioVoice.sourceFrame !== '0' || this.self.audioVoice.playing) {
        throw Error('atomic stop/rewind');
      }

      this.self.seekAudio('1000');
      this.self.playAudio();
      const version = 1;
      this.self.name = 'v' + version + ':' + this.#ticks;
    }
  }

  onSaveState(): {ticks: number; cursor: string} {
    return {ticks: this.#ticks, cursor: this.#cursor};
  }

  onLoadState(state: {ticks: number; cursor: string}): void {
    if (this.self.audioVoice.sourceFrame !== state.cursor || this.self.audioVoice.playing ||
        audio.stats().generation !== '1' || audio.stats().volume !== 0.5) {
      throw Error('audio backend/cursor replaced during reload');
    }

    this.#ticks = state.ticks;
    this.#cursor = state.cursor;
  }

  onStop(): void {
    if (this.#ticks !== 2) {
      throw Error('private audio state lost');
    }

    this.self.stopAudio();
    this.self.name += ':stopped';
  }
}
)ts";
      source.replace(source.find("const version = 1;"), std::string("const version = 1;").size(),
                     "const version = " + std::to_string(version) + ";");
      return files->writeTextAtomic("script-audio-acceptance/actor.ts", source);
    };
    auto scene = std::make_shared<World::Scene>(std::move(*created));
    auto actor = scene->createEntity("Audio Actor");
    World::AudioSource description;
    description.asset = "script-audio-acceptance/tone.wav";
    description.playOnStart = false;
    description.voice.looping = true;

    if (!actor || !scene->setAudioSource(*actor, description)
        || !scene->setScript(*actor, World::ScriptComponent{"actor.ts", "Actor"})
        || !writeActor(1)) {
      std::println(stderr, "Script audio authoring failed");
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();
    const auto mixer = Audio::Mixer::backendMemory();
    const auto output = Audio::Output::backendMemory();

    {
      Audio::PlaybackConfig playback;
      playback.output.volume = 0;
      auto session = native ? Script::AudioSession::createPlayback(scene, *files, playback)
                            : Script::AudioSession::create(scene, *files);
      auto program = compiler->compileProject(*files, "script-audio-acceptance");

      if (!session || !program) {
        std::println(stderr, "{}", session ? program.error().message : session.error().message);
        return 1;
      }

      auto bindings = session->bindings();

      if (!bindings) {
        std::println(stderr, "{}", bindings.error().message);
        return 1;
      }

      auto stats = [&]() -> Core::Result<nlohmann::json> {
        auto callback = std::ranges::find(*bindings, "audio.stats", &Script::HostFunction::name);
        auto result = callback->callback("[]");

        if (!result) {
          return std::unexpected(result.error());
        }

        return nlohmann::json::parse(**result);
      };
      Script::SceneScriptsConfig options;
      options.services = *bindings;
      auto scripts = Script::SceneScripts::create(scene, *program, options);

      if (!scripts) {
        std::println(stderr, "{}", scripts.error().message);
        return 1;
      }

      auto check = [](Core::Result<Script::ScriptReport> report) -> Core::Result<void> {
        if (!report) {
          return std::unexpected(report.error());
        }

        if (!report->failures.empty()) {
          return std::unexpected(report->failures.front().error);
        }

        if (report->realmFaulted) {
          return Core::fail(Core::ErrorCode::InvalidState, "Audio script realm faulted");
        }

        return {};
      };
      auto started = check(scripts->start());

      if (!started) {
        std::println(stderr, "{}", started.error().message);
        return 1;
      }

      std::array<float, 32> pcm;

      if (native) {
        auto initial = stats();

        if (!initial || initial->at("mode") != "playback" || initial->at("playback").is_null()) {
          std::println(stderr, "Script native audio initialization snapshot failed");
          return 1;
        }

        const auto before
            = std::stoull((*initial)["playback"]["output"]["consumedFrames"].get<std::string>());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        bool delivered = false;

        while (std::chrono::steady_clock::now() < deadline) {
          auto current = stats();

          if (!current || current->at("faulted") == true) {
            std::println(stderr, "Script native audio delivery faulted");
            return 1;
          }

          const auto consumed
              = std::stoull((*current)["playback"]["output"]["consumedFrames"].get<std::string>());

          if (consumed > before + 1024) {
            delivered = true;
            break;
          }

          std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }

        if (!delivered) {
          std::println(stderr, "Script native audio did not deliver endpoint PCM");
          return 1;
        }

        pcm.fill(0.75F);

        if (session->render(pcm) || pcm.front() != 0.75F) {
          std::println(stderr, "Native script audio accepted offline rendering");
          return 1;
        }
      } else {
        auto rendered = session->render(pcm);

        if (!rendered || !std::ranges::all_of(pcm, [](float value) {
              return std::abs(value - 0.25F) < 0.0001F;
            })) {
          std::println(stderr, "Script seek/play/gain real PCM verification failed");
          return 1;
        }
      }

      auto paused = check(scripts->update(0.1));

      if (!paused || scene->entity(*actor)->name != "paused") {
        std::println(stderr, "Script audio pause failed");
        return 1;
      }

      if (!native && (!session->render(pcm) || !std::ranges::all_of(pcm, [](float value) {
            return value == 0;
          }))) {
        std::println(stderr, "Script pause real PCM verification failed");
        return 1;
      }

      auto changed = writeActor(2);
      auto next = changed ? compiler->compileProject(*files, "script-audio-acceptance")
                          : Core::Result<Script::CompiledProgram>{std::unexpected(changed.error())};

      if (!next) {
        std::println(stderr, "{}", next.error().message);
        return 1;
      }

      auto reload = scripts->reload(*next);

      if (!reload || reload->migrated != 1 || !reload->retired.failures.empty()) {
        std::println(stderr, "Script audio cursor/private state migration failed");
        return 1;
      }

      auto resumed = check(scripts->update(0.1));

      if (!resumed || scene->entity(*actor)->name != "v2:2") {
        std::println(stderr, "Reloaded script audio stop/seek/play failed");
        return 1;
      }

      auto stopped = check(scripts->stop());

      if (!stopped || scene->entity(*actor)->name != "v2:2:stopped"
          || scripts->stats()->runtime.instances != 0) {
        std::println(stderr, "Script audio explicit stop failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || after.bytes != memory.bytes
        || after.allocations != memory.allocations || Core::processResources().handles != 0
        || Core::processResources().children != 0
        || Audio::Mixer::backendMemory().bytes != mixer.bytes
        || Audio::Mixer::backendMemory().allocations != mixer.allocations
        || Audio::Output::backendMemory().bytes != output.bytes
        || Audio::Output::backendMemory().allocations != output.allocations) {
      std::println(stderr, "Script audio backend/compiler/process/VM cleanup failed");
      return 1;
    }

    std::println(
        "Script audio acceptance passed: real play/pause/stop/seek/gain, "
        "same voice/cursor through hot reload, cleanup{}",
        native ? ", muted actual endpoint delivery" : ", deterministic PCM");
    return 0;
  }
}  // namespace Dot::Runtime
