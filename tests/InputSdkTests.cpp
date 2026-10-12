#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Platform.Events;
import Dot.Platform.Input;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Script.LiveScripts;
import Dot.Script.InputSource;
import Dot.World.Scene;

namespace {
  using namespace Dot::Script;
  using namespace Dot::Platform;
  using Json = nlohmann::json;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-input-sdk-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      auto sdk = Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY);
      REQUIRE(sdk);
      REQUIRE(files().createDirectories("scripts/dot"));

      for (const auto* name : {"dot/engine.ts", "dot/host.d.ts"}) {
        auto text = sdk->readText(name);
        REQUIRE(text);
        REQUIRE(files().writeTextAtomic(std::filesystem::path{"scripts"} / name, *text));
      }
    }

    ~Project() {
      std::error_code ignored;
      std::filesystem::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      auto result = Dot::Core::FileSystem::open(root);
      REQUIRE(result);
      return *result;
    }

    auto write(std::string_view source) const -> void {
      REQUIRE(files().writeTextAtomic("scripts/actor.ts", source));
    }

    auto compiler() const -> TypeScriptCompiler {
      auto result = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER);
      REQUIRE(result);
      return *result;
    }

    auto compile() const -> CompiledProgram {
      auto result = compiler().compileProject(files());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Input Scripts");
    REQUIRE(result);
    auto scene = std::make_shared<Dot::World::Scene>(std::move(*result));
    auto entity = scene->createEntity("Actor").value();
    REQUIRE(scene->setScript(entity, Dot::World::ScriptComponent{"actor.ts", "Actor"}));
    return scene;
  }

  auto input() -> InputSource {
    auto result = InputSource::create();
    REQUIRE(result);
    return std::move(*result);
  }

  auto config(const InputSource& input) -> SceneScriptsConfig {
    SceneScriptsConfig config;
    config.services = input.bindings().value();
    return config;
  }

  auto good(Dot::Core::Result<ScriptReport> result) -> void {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->failures.empty());
    REQUIRE_FALSE(result->realmFaulted);
  }

  auto actor(int factor = 1) -> std::string {
    return R"ts(
import {ScriptComponent, input} from './dot/engine.js';
import type {ScriptContext} from './dot/engine.js';

export class Actor extends ScriptComponent {
  #ticks = 0;

  constructor(context: ScriptContext) {
    super(context);
    input.snapshot();
  }

  onUpdate(): void {
    ++this.#ticks;
    const transform = this.self.transform;
    transform.position.x += input.key('A').down ? )ts"
           + std::to_string(factor) + R"ts( : 0;
    this.self.transform = transform;
    this.self.name = this.#ticks + ':' + input.index + ':' + input.text;
  }

  onFixedUpdate(): void {
    const transform = this.self.transform;
    transform.position.y += input.key('A').pressed ? 1 : 0;
    this.self.transform = transform;
  }

  onSaveState(): {ticks: number} {
    input.pointer;
    return {ticks: this.#ticks};
  }

  onLoadState(state: {ticks: number}): void {
    input.wheel;
    this.#ticks = state.ticks;
  }
}
)ts";
  }
}  // namespace

TEST_CASE("input_sdk_real_typescript_names_owned_snapshots_and_missing_services_fail_explicitly") {
  Project project;
  project.write(R"ts(
import {input, keys, mouseButtons} from './dot/engine.js';
import type {HostError} from './dot/engine.js';

export function run(): object {
  const frame = input.snapshot();
  frame.pointer.x = -999;
  frame.keys.A.down = false;
  return {index: input.index, keys: keys.length, buttons: mouseButtons.length,
    pressed: keys.filter(key => input.key(key).pressed), mouse: input.button('Right'),
    pointer: input.pointer, delta: input.pointerDelta, wheel: input.wheel, text: input.text};
}

export function missing(): string {
  try {
    input.text;
  } catch (error) {
    return (error as HostError).code;
  }

  return 'unexpected';
}
)ts");
  auto program = project.compile();
  auto publisher = input();
  auto vm = Runtime::create(program.files().value(), {}, publisher.bindings().value());
  REQUIRE(vm);
  auto module = vm->loadModule("actor.js").value();
  CHECK(vm->call(module, "missing").value().value() == "\"Unavailable\"");
  InputState native;
  REQUIRE(native.consume({0, 0, KeyChanged{Key::A, true, false}}));
  REQUIRE(native.consume({0, 0, ButtonChanged{MouseButton::Right, true}}));
  REQUIRE(native.consume({0, 0, PointerMoved{3, 4, 1, -1}}));
  REQUIRE(native.consume({0, 0, Wheel{0, 2}}));
  REQUIRE(native.consume({0, 0, Text{"中文🙂"}}));
  REQUIRE(publisher.publish(native));
  auto result = vm->call(module, "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  auto frame = Json::parse(**result);
  CHECK(frame["index"] == "1");
  CHECK(frame["keys"] == static_cast<int>(Key::Count) - 1);
  CHECK(frame["buttons"] == static_cast<int>(MouseButton::Count) - 1);
  CHECK(frame["pressed"] == Json::array({"A"}));
  CHECK(frame["mouse"]["pressed"] == true);
  CHECK(frame["pointer"] == Json{{"x", 3}, {"y", 4}});
  CHECK(frame["delta"] == Json{{"x", 1}, {"y", -1}});
  CHECK(frame["wheel"] == Json{{"x", 0}, {"y", 2}});
  CHECK(frame["text"] == "中文🙂");
  auto noHost = Runtime::create(
      program.files().value(), {},
      {{"other.read", [](auto) {
          return Dot::Core::Result<std::optional<std::string>>{std::optional<std::string>{"0"}};
        }}});
  REQUIRE(noHost);
  auto other = noHost->loadModule("actor.js").value();
  CHECK(noHost->call(other, "missing").value().value() == "\"NotFound\"");
}

TEST_CASE("input_sdk_lifecycle_reads_share_edges_across_fixed_steps_and_use_next_published_frame") {
  Project project;
  project.write(actor());
  auto program = project.compile();
  auto native = scene();
  auto id = native->entities()->front().id;
  auto publisher = input();
  InputState state;
  REQUIRE(state.consume({0, 0, KeyChanged{Key::A, true, false}}));
  REQUIRE(state.consume({0, 0, Text{"start"}}));
  REQUIRE(publisher.publish(state));
  auto scripts = SceneScripts::create(native, program, config(publisher));
  REQUIRE(scripts);
  good(scripts->fixedUpdate(0.1));
  good(scripts->fixedUpdate(0.1));
  good(scripts->update(0.1));
  CHECK(native->entity(id)->transform.position.y == 2);
  CHECK(native->entity(id)->transform.position.x == 1);
  CHECK(native->entity(id)->name == "1:1:start");
  state.beginFrame();
  REQUIRE(state.consume({0, 0, KeyChanged{Key::A, true, true}}));
  REQUIRE(publisher.publish(state));
  good(scripts->fixedUpdate(0.1));
  good(scripts->update(0.1));
  CHECK(native->entity(id)->transform.position.y == 2);
  CHECK(native->entity(id)->transform.position.x == 2);
  CHECK(native->entity(id)->name == "2:2:");
  state.beginFrame();
  REQUIRE(state.consume({0, 0, Focus{false}}));
  REQUIRE(publisher.publish(state));
  good(scripts->update(0.1));
  CHECK(native->entity(id)->transform.position.x == 2);
  CHECK(native->entity(id)->name == "3:3:");
  good(scripts->stop());
  CHECK(scripts->stats()->runtime.instances == 0);
}

TEST_CASE("input_sdk_failed_compile_and_reloaded_realms_keep_the_original_live_publisher") {
  Project project;
  project.write(actor());
  auto native = scene();
  auto id = native->entities()->front().id;
  auto publisher = input();
  InputState state;
  REQUIRE(state.consume({0, 0, KeyChanged{Key::A, true, false}}));
  REQUIRE(publisher.publish(state));
  LiveScriptsConfig options;
  options.scripts = config(publisher);
  options.pollInterval = std::chrono::milliseconds{0};
  auto scripts = LiveScripts::create(native, project.files(), project.compiler(), options);
  REQUIRE(scripts);
  auto first = scripts->update(0.1);
  REQUIRE(first);
  REQUIRE(first->frame.failures.empty());
  CHECK(native->entity(id)->transform.position.x == 1);
  project.write("export const bad: number = 'type error';");
  state.beginFrame();
  REQUIRE(publisher.publish(state));
  auto failed = scripts->update(0.1);
  REQUIRE(failed);
  CHECK(failed->source.error.has_value());
  CHECK(failed->frame.failures.empty());
  CHECK(native->entity(id)->transform.position.x == 2);
  project.write(actor(3));
  auto moved = std::move(publisher);
  state.beginFrame();
  REQUIRE(state.consume({0, 0, KeyChanged{Key::A, false, false}}));
  REQUIRE(moved.publish(state));
  auto reloaded = scripts->update(0.1);
  REQUIRE_MESSAGE(reloaded, (reloaded ? "" : reloaded.error().message));
  REQUIRE(reloaded->source.committed);
  REQUIRE(reloaded->source.reload);
  CHECK(reloaded->source.reload->migrated == 1);
  CHECK(reloaded->frame.failures.empty());
  CHECK(native->entity(id)->transform.position.x == 2);
  CHECK(native->entity(id)->name == "3:3:");
  state.beginFrame();
  REQUIRE(state.consume({0, 0, KeyChanged{Key::A, true, false}}));
  REQUIRE(moved.publish(state));
  auto next = scripts->update(0.1);
  REQUIRE(next);
  CHECK_FALSE(next->source.changed);
  CHECK(next->frame.failures.empty());
  CHECK(native->entity(id)->transform.position.x == 5);
  CHECK(native->entity(id)->name == "4:4:");
  CHECK(scripts->stats()->compileAttempts == 3);
  CHECK(scripts->stats()->reloads == 1);
  good(scripts->stop());
}

TEST_CASE("script_services_native_read_write_classification_survives_preparation_and_reload") {
  Project project;
  project.write(R"ts(
import {ScriptComponent} from './dot/engine.js';
import type {ScriptContext, HostError} from './dot/engine.js';
import {invoke} from 'dot:host';

function inspect(): void {
  if (invoke('custom.read', []) !== 7) {
    throw Error('read missing');
  }

  let denied = false;

  try {
    invoke('custom.write', []);
  } catch (error) {
    denied = (error as HostError).code === 'InvalidState';
  }

  if (!denied) {
    throw Error('write escaped guard');
  }
}

export class Actor extends ScriptComponent {
  constructor(context: ScriptContext) {
    super(context);
    inspect();
  }

  onUpdate(): void {
    invoke('custom.write', []);
  }

  onSaveState(): object {
    inspect();
    return {};
  }

  onLoadState(): void {
    inspect();
  }
}
)ts");
  auto program = project.compile();
  int reads = 0;
  int writes = 0;
  SceneScriptsConfig options;
  options.services = {{"custom.read",
                       [&](auto) -> Dot::Core::Result<std::optional<std::string>> {
                         ++reads;
                         return std::optional<std::string>{"7"};
                       },
                       HostAccess::ReadOnly},
                      {"custom.write", [&](auto) -> Dot::Core::Result<std::optional<std::string>> {
                         ++writes;
                         return std::optional<std::string>{};
                       }}};
  auto native = scene();
  auto scripts = SceneScripts::create(native, program, options);
  REQUIRE(scripts);
  good(scripts->start());
  CHECK(reads == 1);
  CHECK(writes == 0);
  good(scripts->update(0.1));
  CHECK(writes == 1);
  auto reload = scripts->reload(program);
  REQUIRE(reload);
  CHECK(reload->migrated == 1);
  CHECK(reads == 4);
  CHECK(writes == 1);
  good(scripts->update(0.1));
  CHECK(writes == 2);
  good(scripts->stop());
}

TEST_CASE("script_services_invalid_registry_fails_before_preparation_or_vm_allocation") {
  Project project;
  project.write(actor());
  auto program = project.compile();
  auto native = scene();
  auto callback = [](auto) -> Dot::Core::Result<std::optional<std::string>> {
    FAIL("Invalid service invoked");
    return std::optional<std::string>{};
  };
  auto memory = Runtime::backendMemory();

  for (const auto& services :
       std::vector<HostBindings>{{{"empty", {}}},
                                 {{"bad-name", callback}},
                                 {{"unknown.access", callback, static_cast<HostAccess>(99)}},
                                 {{"scene.info", callback}},
                                 {{"same", callback}, {"same", callback}}}) {
    SceneScriptsConfig options;
    options.services = services;
    auto result = SceneScripts::create(native, program, options);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Dot::Core::ErrorCode::InvalidArgument);
    CHECK(Runtime::backendMemory().bytes == memory.bytes);
    CHECK(Runtime::backendMemory().allocations == memory.allocations);
  }

  SceneScriptsConfig overflow;

  for (int index = 0; index < 241; ++index) {
    overflow.services.push_back({"extra" + std::to_string(index), callback});
  }

  CHECK_FALSE(SceneScripts::create(native, program, overflow));
  CHECK(Runtime::backendMemory().bytes == memory.bytes);
}

TEST_CASE("input_sdk_typecheck_rejects_unsupported_names_and_readonly_service_assignment") {
  Project project;

  for (const auto* source : {"import {input} from './dot/engine.js';input.key('Unknown');",
                             "import {input} from './dot/engine.js';input.key('ArrowLeft');",
                             "import {input} from './dot/engine.js';input.key(1);",
                             "import {input} from './dot/engine.js';input.button('primary');",
                             "import {input} from './dot/engine.js';input.text='injected';"}) {
    project.write(source);
    auto result = project.compiler().compileProject(project.files());
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}
