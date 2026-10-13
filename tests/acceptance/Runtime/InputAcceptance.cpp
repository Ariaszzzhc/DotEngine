#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <print>
#include <string>
#include <utility>

import Dot.Core.Application;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Process;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.InputSource;
import Dot.Script.LiveScripts;
import Dot.World.Scene;

namespace Dot::Runtime {
  namespace {
    auto push(SDL_Event event) -> Core::Result<void> {
      if (!SDL_PushEvent(&event)) {
        return Core::fail(Core::ErrorCode::External,
                          "SDL input injection: " + std::string(SDL_GetError()));
      }

      return {};
    }

    auto key(std::uint32_t window, SDL_Scancode code, bool repeat = false) -> Core::Result<void> {
      SDL_Event event{};
      event.type = SDL_EVENT_KEY_DOWN;
      event.key.windowID = window;
      event.key.scancode = code;
      event.key.down = true;
      event.key.repeat = repeat;
      return push(event);
    }
  }  // namespace

  auto testScriptInput(const std::filesystem::path& executable,
                       const std::filesystem::path& sdkRoot, const std::filesystem::path& root)
      -> int {
    auto files = Core::FileSystem::open(root);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Input Acceptance");
    auto publisher = Script::InputSource::create();
    Core::ApplicationConfig options;
    options.window = {"Input acceptance", 320, 240, true, true, false};
    options.fixedStep = std::chrono::milliseconds{10};
    options.maxUpdates = 2;
    options.pauseOnFocusLoss = false;
    options.frameDelay = std::chrono::nanoseconds{0};
    auto application = Core::Application::create(options);

    if (!files || !sdk || !compiler || !created || !publisher || !application
        || !files->createDirectories("script-input-acceptance/dot")) {
      std::println(stderr, "Input acceptance setup failed");
      return 1;
    }

    for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
      auto source = sdk->readText(path);

      if (!source
          || !files->writeTextAtomic(std::filesystem::path{"script-input-acceptance"} / path,
                                     *source)) {
        std::println(stderr, "Input acceptance SDK copy failed");
        return 1;
      }
    }

    auto writeActor = [&](int factor) -> Core::Result<void> {
      auto source = std::string{R"ts(
import {ScriptComponent, input, keys, mouseButtons} from './dot/engine.js';
import type {ScriptContext} from './dot/engine.js';

export class Actor extends ScriptComponent {
  #ticks = 0;

  constructor(context: ScriptContext) {
    super(context);
    const frame = input.snapshot();

    if (Object.keys(frame.keys).length !== keys.length ||
        Object.keys(frame.buttons).length !== mouseButtons.length) {
      throw Error('native key coverage');
    }
  }

  onFixedUpdate(): void {
    const t = this.self.transform;
    t.position.y += input.key('Space').pressed ? 1 : 0;
    this.self.transform = t;
  }

  onUpdate(): void {
    ++this.#ticks;
    const speed = 1;
    const t = this.self.transform;
    t.position.x += input.key('A').down ? speed : 0;
    this.self.transform = t;
    this.self.name = this.#ticks + ':' + input.index + ':' + input.key('A').released + ':' + input.text;

    if (this.#ticks === 1 && (!input.button('Left').pressed || input.pointer.x !== 12 ||
        input.pointerDelta.y !== -2 || input.wheel.y !== -1)) {
      throw Error('SDL pointer/wheel/button bridge');
    }
  }

  onSaveState(): {ticks: number} {
    input.snapshot();
    return {ticks: this.#ticks};
  }

  onLoadState(state: {ticks: number}): void {
    input.key('A');
    this.#ticks = state.ticks;
  }

  onStop(): void {
    this.self.name += ':stopped';
  }
}
)ts"};
      source.replace(source.find("const speed = 1;"), std::string("const speed = 1;").size(),
                     "const speed = " + std::to_string(factor) + ";");
      return files->writeTextAtomic("script-input-acceptance/actor.ts", source);
    };
    auto scene = std::make_shared<World::Scene>(std::move(*created));
    auto actor = scene->createEntity("Actor");
    auto services = publisher->bindings();

    if (!actor || !services || !writeActor(1)
        || !scene->setScript(*actor, World::ScriptComponent{"actor.ts", "Actor"})
        || !application->window().setTextInput(true) || !Platform::pollEvents()
        || !publisher->publish(application->input())) {
      std::println(stderr, "Input acceptance scene/text mode/publication setup failed");
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();

    {
      Script::LiveScriptsConfig config;
      config.directory = "script-input-acceptance";
      config.pollInterval = std::chrono::milliseconds{0};
      config.scripts.services = std::move(*services);
      auto scripts = Script::LiveScripts::create(scene, *files, *compiler, config);

      if (!scripts) {
        std::println(stderr, "{}", scripts.error().message);
        return 1;
      }

      bool committed = false;
      auto dispatch = [&](Core::Result<Script::LiveScriptReport> result) -> Core::Result<void> {
        if (!result) {
          return std::unexpected(result.error());
        }

        if (result->source.error) {
          return std::unexpected(*result->source.error);
        }

        if (!result->frame.failures.empty()) {
          return std::unexpected(result->frame.failures.front().error);
        }

        if (result->frame.realmFaulted) {
          return Core::fail(Core::ErrorCode::InvalidState, "Input script realm faulted");
        }

        committed = committed || result->source.committed;
        return {};
      };
      Core::LoopCallbacks callbacks;
      callbacks.prepareFrame = [&](const Platform::InputState& state) -> Core::Result<void> {
        return publisher->publish(state).transform([](const auto&) {});
      };

      callbacks.fixedUpdate = [&](double seconds) -> Core::Result<void> {
        return dispatch(scripts->fixedUpdate(seconds));
      };

      callbacks.update = [&](double seconds) -> Core::Result<void> {
        return dispatch(scripts->update(seconds));
      };

      const auto window = application->window().id();
      SDL_Event mouse{};
      mouse.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      mouse.button.windowID = window;
      mouse.button.button = SDL_BUTTON_LEFT;
      mouse.button.down = true;
      SDL_Event motion{};
      motion.type = SDL_EVENT_MOUSE_MOTION;
      motion.motion.windowID = window;
      motion.motion.x = 12;
      motion.motion.y = 9;
      motion.motion.xrel = 3;
      motion.motion.yrel = -2;
      SDL_Event wheel{};
      wheel.type = SDL_EVENT_MOUSE_WHEEL;
      wheel.wheel.windowID = window;
      wheel.wheel.x = 1;
      wheel.wheel.y = -1;
      SDL_Event text{};
      text.type = SDL_EVENT_TEXT_INPUT;
      text.text.windowID = window;
      text.text.text = "中文🙂";

      if (!key(window, SDL_SCANCODE_A) || !key(window, SDL_SCANCODE_SPACE) || !push(mouse)
          || !push(motion) || !push(wheel) || !push(text)) {
        std::println(stderr, "Input acceptance SDL event injection failed");
        return 1;
      }

      auto first = application->step(std::chrono::milliseconds{20}, callbacks);
      auto state = scene->entity(*actor);

      if (!first || !state || state->transform.position.x != 1 || state->transform.position.y != 2
          || state->name != "1:2:false:中文🙂") {
        std::println(stderr, "Input acceptance first SDL/typed frame failed: {}",
                     first ? "Native state mismatch" : first.error().message);
        return 1;
      }

      if (!writeActor(3) || !key(window, SDL_SCANCODE_SPACE, true)) {
        std::println(stderr, "Input acceptance reload/repeat setup failed");
        return 1;
      }

      auto second = application->step(std::chrono::milliseconds{10}, callbacks);
      state = scene->entity(*actor);

      if (!second || !committed || !state || state->transform.position.x != 4
          || state->transform.position.y != 2 || state->name != "2:3:false:") {
        std::println(stderr, "Input acceptance repeat/reloaded input frame failed: {}",
                     second ? "Native state mismatch" : second.error().message);
        return 1;
      }

      SDL_Event focus{};
      focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
      focus.window.windowID = window;

      if (!push(focus)) {
        std::println(stderr, "Input acceptance focus injection failed");
        return 1;
      }

      auto third = application->step(std::chrono::milliseconds{10}, callbacks);
      state = scene->entity(*actor);
      auto stopped = scripts->stop();

      if (!third || !state || state->transform.position.x != 4 || state->transform.position.y != 2
          || state->name != "3:4:true:" || !stopped || !stopped->failures.empty()
          || scene->entity(*actor)->name != "3:4:true::stopped"
          || scripts->stats()->scripts.runtime.instances != 0) {
        std::println(stderr, "Input acceptance focus release/stop failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || after.bytes != memory.bytes
        || after.allocations != memory.allocations || Core::processResources().handles != 0
        || Core::processResources().children != 0 || !application->window().setTextInput(false)) {
      std::println(stderr, "Input acceptance compiler/process/VM/text mode cleanup failed");
      return 1;
    }

    std::println(
        "Input acceptance passed: injected SDL events through application publication, "
        "typed component/fixed edges/repeat/text/pointer/focus, hot reload, cleanup");
    return 0;
  }
}  // namespace Dot::Runtime
