#include <doctest/doctest.h>

#include <cmath>
#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.Core.Files;
import Dot.Core.Result;

namespace {
  using namespace Dot::Script;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-components-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      auto sdk = Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY);
      REQUIRE(sdk);
      REQUIRE(files().createDirectories("scripts/dot"));

      for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
        auto text = sdk->readText(path);
        REQUIRE(text);
        REQUIRE(files().writeTextAtomic(std::filesystem::path{"scripts"} / path, *text));
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

    auto write(std::string_view code) const -> void {
      REQUIRE(files().writeTextAtomic("scripts/actor.ts", code));
    }

    auto compile() const -> CompiledProgram {
      auto compiler = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER);
      REQUIRE(compiler);
      auto result = compiler->compileProject(files());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  constexpr auto actor
      = "import {ScriptComponent,scene} from './dot/engine.js';"
        "export class Actor extends "
        "ScriptComponent<{speed:number,failStart?:boolean,failUpdate?:boolean,failStop?:boolean}>{"
        "#ticks=0;onStart(){this.self.name+='S';if(this.properties.failStart)throw Error('start "
        "failed');}"
        "async onUpdate(seconds:number){await "
        "Promise.resolve();if(this.properties.failUpdate)throw Error('update failed');"
        "++this.#ticks;const "
        "t=this.self.transform;t.position.x+=this.properties.speed*seconds;this.self.transform=t;}"
        "onFixedUpdate(seconds:number){const "
        "t=this.self.transform;t.position.y+=seconds;this.self.transform=t;}"
        "onStop(){this.self.name+='T';if(this.properties.failStop)throw Error('stop failed');}}";

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Components");
    REQUIRE(result);
    return std::make_shared<Dot::World::Scene>(std::move(*result));
  }

  auto entity(const std::shared_ptr<Dot::World::Scene>& scene, std::string_view properties,
              bool enabled = true, std::string_view name = "E") -> Dot::World::EntityId {
    auto result = scene->createEntity(name);
    REQUIRE(result);
    REQUIRE(scene->setScript(
        *result, ComponentProperties{"actor.ts", "Actor", std::string(properties), enabled}));
    return *result;
  }

  auto service(const std::shared_ptr<Dot::World::Scene>& scene, const CompiledProgram& program,
               const SceneScriptsConfig& config = {}) -> SceneScripts {
    auto result = SceneScripts::create(scene, program, config);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto good(Dot::Core::Result<ScriptReport> result) -> ScriptReport {
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));

    if (!result->failures.empty()) {
      INFO(result->failures.front().error.message);
    }

    REQUIRE(result->failures.empty());
    REQUIRE_FALSE(result->realmFaulted);
    return std::move(*result);
  }
}  // namespace

TEST_CASE(
    "components_real_typed_classes_start_once_update_fixed_stop_and_preserve_independent_native_"
    "state") {
  Project project;
  project.write(actor);
  auto program = project.compile();
  auto native = scene();
  auto a = entity(native, "{\"speed\":2}", true, "A");
  auto b = entity(native, "{\"speed\":-3}", true, "B");
  auto scripts = service(native, program);
  CHECK(good(scripts.sync()).created == 2);
  CHECK(scripts.stats()->pending == 2);
  good(scripts.start());
  good(scripts.start());
  CHECK(native->entity(a)->name == "AS");
  CHECK(native->entity(b)->name == "BS");
  good(scripts.update(0.5));
  good(scripts.fixedUpdate(0.25));
  CHECK(native->entity(a)->transform.position.x == 1);
  CHECK(native->entity(b)->transform.position.x == -1.5F);
  CHECK(native->entity(a)->transform.position.y == 0.25F);
  CHECK(scripts.stats()->running == 2);
  CHECK(good(scripts.stop()).removed == 2);
  CHECK(native->entity(a)->name == "AST");
  CHECK(native->entity(b)->name == "BST");
  CHECK(good(scripts.stop()).calls == 0);
  CHECK(scripts.stats()->runtime.instances == 0);
  CHECK_FALSE(scripts.update(0.1));
}

TEST_CASE(
    "components_disabled_sources_load_nothing_enable_disable_and_property_edits_restart_objects") {
  Project project;
  project.write(actor);
  auto program = project.compile();
  auto native = scene();
  auto id = entity(native, "{\"speed\":1}", false);
  auto missing = native->entity(id)->script.value();
  missing.source = "missing.ts";
  REQUIRE(native->setScript(id, missing));
  auto scripts = service(native, program);
  good(scripts.update(0.5));
  CHECK(scripts.stats()->disabled == 1);
  CHECK(scripts.stats()->runtime.modules == 0);
  CHECK(native->entity(id)->name == "E");
  missing.source = "actor.ts";
  missing.enabled = true;
  REQUIRE(native->setScript(id, missing));
  good(scripts.update(0.5));
  CHECK(native->entity(id)->transform.position.x == 0.5F);
  missing.properties = "{\"speed\":2}";
  REQUIRE(native->setScript(id, missing));
  good(scripts.update(0.5));
  CHECK(native->entity(id)->name == "ESTS");
  CHECK(native->entity(id)->transform.position.x == 1.5F);
  missing.enabled = false;
  REQUIRE(native->setScript(id, missing));
  good(scripts.update(0.5));
  CHECK(native->entity(id)->name == "ESTST");
  CHECK(scripts.stats()->runtime.instances == 0);
}

TEST_CASE("components_preparation_forbids_native_constructor_initializer_and_getter_mutations") {
  for (const auto* code :
       {"import {ScriptComponent} from './dot/engine.js';export class Actor extends "
        "ScriptComponent{constructor(c:any){super(c);this.self.name='BAD';}}",
        "import {scene} from './dot/engine.js';scene.rename('BAD');export class Actor{}",
        "import {Entity} from './dot/engine.js';export class "
        "Actor{self:Entity;constructor(c:any){this.self=new Entity(c.entity);}"
        "get onUpdate():any{this.self.name='BAD';return ()=>{};}}"}) {
    Project project;
    project.write(code);
    auto program = project.compile();
    auto native = scene();
    auto id = entity(native, "{}");
    auto scripts = service(native, program);
    auto result = scripts.sync();
    REQUIRE(result);
    REQUIRE(result->failures.size() == 1);
    CHECK(result->failures[0].phase == ScriptPhase::Prepare);
    CHECK(result->failures[0].error.message.find("preparing scripts") != std::string::npos);
    CHECK(native->entity(id)->name == "E");
    CHECK(native->name().value() == "Components");
    CHECK(scripts.stats()->runtime.instances == 0);
  }
}

TEST_CASE("components_failed_candidate_preserves_existing_instances_until_authoring_is_repaired") {
  Project project;
  project.write(actor);
  auto program = project.compile();
  auto native = scene();
  auto a = entity(native, "{\"speed\":1}");
  auto scripts = service(native, program);
  good(scripts.update(0.5));
  auto changed = native->entity(a)->script.value();
  changed.className = "Missing";
  REQUIRE(native->setScript(a, changed));
  auto failed = scripts.update(0.5);
  REQUIRE(failed);
  REQUIRE(failed->failures.size() == 1);
  CHECK(scripts.stats()->runtime.instances == 1);
  CHECK(native->entity(a)->name == "ES");
  CHECK(native->entity(a)->transform.position.x == 0.5F);
  changed.className = "Actor";
  REQUIRE(native->setScript(a, changed));
  good(scripts.update(0.5));
  CHECK(native->entity(a)->name == "ES");
  CHECK(native->entity(a)->transform.position.x == 1);
}

TEST_CASE(
    "components_callback_errors_stop_once_isolate_failures_and_keep_other_components_running") {
  Project project;
  project.write(actor);
  auto program = project.compile();
  auto native = scene();
  auto bad = entity(native, "{\"speed\":1,\"failUpdate\":true,\"failStop\":true}", true, "Bad");
  auto valid = entity(native, "{\"speed\":2}", true, "Good");
  auto scripts = service(native, program);
  auto first = scripts.update(0.5);
  REQUIRE(first);
  REQUIRE(first->failures.size() == 2);
  CHECK(first->failures[0].phase == ScriptPhase::Update);
  CHECK(first->failures[1].phase == ScriptPhase::Stop);
  CHECK(native->entity(bad)->name == "BadST");
  CHECK(native->entity(valid)->transform.position.x == 1);
  CHECK(scripts.stats()->failed == 1);
  CHECK(scripts.stats()->running == 1);
  good(scripts.update(0.5));
  CHECK(native->entity(bad)->name == "BadST");
  CHECK(native->entity(valid)->transform.position.x == 2);
  good(scripts.stop());
  CHECK(native->entity(valid)->name == "GoodST");
}

TEST_CASE("components_start_failure_is_reported_and_cleanup_runs_once") {
  Project project;
  project.write(actor);
  auto program = project.compile();
  auto native = scene();
  auto id = entity(native, "{\"speed\":1,\"failStart\":true}");
  auto scripts = service(native, program);
  auto result = scripts.start();
  REQUIRE(result);
  REQUIRE(result->failures.size() == 1);
  CHECK(result->failures[0].phase == ScriptPhase::Start);
  CHECK(native->entity(id)->name == "EST");
  good(scripts.start());
  CHECK(native->entity(id)->name == "EST");
  CHECK(scripts.stats()->failed == 1);
}

TEST_CASE("components_all_pending_starts_finish_before_frame_updates_in_creation_order") {
  Project project;
  project.write(
      "import {ScriptComponent,scene} from './dot/engine.js';export class Actor extends "
      "ScriptComponent{"
      "onStart(){this.self.name+='S';}onUpdate(){if(scene.entities().some(e=>!e.name.endsWith('S'))"
      ")"
      "throw Error('update before all starts');}}");
  auto program = project.compile();
  auto native = scene();
  entity(native, "{}");
  entity(native, "{}");
  auto scripts = service(native, program);
  good(scripts.update(0));
  CHECK(scripts.stats()->running == 2);
}

TEST_CASE("components_self_deletion_during_start_and_update_releases_bindings_immediately") {
  for (const auto* hook : {"onStart", "onUpdate"}) {
    Project project;
    project.write(
        "import {ScriptComponent} from './dot/engine.js';export class Actor extends "
        "ScriptComponent{"
        + std::string(hook) + "(){this.self.destroy();}}");
    auto program = project.compile();
    auto native = scene();
    entity(native, "{}");
    auto scripts = service(native, program);
    good(scripts.update(0));
    CHECK(native->entityCount().value() == 0);
    CHECK(scripts.stats()->components == 0);
    CHECK(scripts.stats()->runtime.instances == 0);
  }
}

TEST_CASE("components_capacity_and_delta_boundaries_fail_before_native_lifecycle_changes") {
  Project project;
  project.write(actor);
  auto program = project.compile();
  auto native = scene();
  auto a = entity(native, "{\"speed\":1}");
  SceneScriptsConfig config;
  config.maxComponents = 1;
  config.runtime.maxInstances = 2;
  auto scripts = service(native, program, config);
  good(scripts.update(0.25));
  auto changed = native->entity(a)->script.value();
  changed.properties = "{\"speed\":2}";
  REQUIRE(native->setScript(a, changed));
  good(scripts.update(0.25));
  CHECK(scripts.stats()->runtime.instances == 1);

  for (double seconds : {-1.0, 1.001, std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
    CHECK_FALSE(scripts.update(seconds));
    CHECK_FALSE(scripts.fixedUpdate(seconds));
  }

  auto b = entity(native, "{\"speed\":1}");
  CHECK_FALSE(scripts.sync());
  CHECK(native->entity(b)->name == "E");
  CHECK(scripts.stats()->runtime.instances == 1);
  config.runtime.maxInstances = 1;
  CHECK_FALSE(SceneScripts::create(native, program, config));
}

TEST_CASE("components_realm_resource_failure_stops_further_dispatch_and_releases_all_objects") {
  Project project;
  project.write(
      "import {ScriptComponent} from './dot/engine.js';export class Actor extends ScriptComponent{"
      "onUpdate(){while(true){}}}");
  auto program = project.compile();
  auto native = scene();
  entity(native, "{}");
  entity(native, "{}");
  SceneScriptsConfig config;
  config.runtime.milliseconds = 10;
  auto scripts = service(native, program, config);
  auto result = scripts.update(0);
  REQUIRE(result);
  CHECK(result->realmFaulted);
  REQUIRE(result->failures.size() == 1);
  CHECK(scripts.stats()->runtime.instances == 0);
  CHECK(scripts.stats()->failed == 2);
  CHECK_FALSE(scripts.update(0));
  CHECK(scripts.stop());
}

TEST_CASE("components_nonvoid_lifecycle_results_and_noncallable_hooks_fail_explicitly") {
  for (const auto* source :
       {"export class Actor{onUpdate(){return 42;}}", "export class Actor{onUpdate=42;}"}) {
    Project project;
    project.write(source);
    auto program = project.compile();
    auto native = scene();
    entity(native, "{}");
    auto scripts = service(native, program);
    auto result = scripts.update(0);
    REQUIRE(result);
    REQUIRE(result->failures.size() == 1);
    CHECK(scripts.stats()->runtime.instances == 0);
  }
}

TEST_CASE(
    "components_owned_program_scene_move_thread_and_destructor_release_without_stop_callbacks") {
  Project project;
  project.write(actor);
  const auto memory = Runtime::backendMemory();
  const auto generations = compilerGenerations();
  auto native = scene();
  auto id = entity(native, "{\"speed\":1}");
  std::filesystem::path output;

  {
    auto program = project.compile();
    output = program.files()->root().parent_path();
    auto scripts = service(native, program);
    good(scripts.update(0.25));
    auto moved = std::move(scripts);
    CHECK_FALSE(scripts.stats());
    bool rejected = false;
    std::thread worker(
        [&] { rejected = !moved.sync() && !moved.update(0) && !moved.stop() && !moved.stats(); });
    worker.join();
    CHECK(rejected);
    good(moved.update(0.25));
    std::thread destroyer([owned = std::move(moved)] {});
    destroyer.join();
    CHECK(native->entity(id)->name == "ES");
  }

  CHECK_FALSE(std::filesystem::exists(output));
  CHECK(compilerGenerations() == generations);
  CHECK(Runtime::backendMemory().bytes == memory.bytes);
  CHECK(Runtime::backendMemory().allocations == memory.allocations);
}

TEST_CASE("components_replacement_stop_that_removes_owner_never_activates_stale_candidate") {
  Project project;
  project.write(
      "import {ScriptComponent} from './dot/engine.js';export class Actor extends ScriptComponent{"
      "onStop(){this.self.destroy();}}");
  auto program = project.compile();
  auto native = scene();
  auto id = entity(native, "{}");
  auto scripts = service(native, program);
  good(scripts.start());
  auto changed = native->entity(id)->script.value();
  changed.properties = "{\"changed\":true}";
  REQUIRE(native->setScript(id, changed));
  auto result = scripts.sync();
  REQUIRE(result);
  REQUIRE(result->failures.size() == 1);
  CHECK(result->failures[0].phase == ScriptPhase::Prepare);
  CHECK(scripts.stats()->components == 0);
  CHECK(scripts.stats()->runtime.instances == 0);
  CHECK(native->entityCount().value() == 0);
}

TEST_CASE("components_pending_generation_stays_owned_after_caller_releases_program_and_scene") {
  Project project;
  project.write(actor);
  auto native = scene();
  entity(native, "{\"speed\":1}");
  std::weak_ptr<Dot::World::Scene> weak = native;
  std::optional<SceneScripts> scripts;
  std::filesystem::path output;

  {
    auto program = project.compile();
    output = program.files()->root().parent_path();
    scripts.emplace(service(native, program));
  }

  native.reset();
  CHECK_FALSE(weak.expired());
  CHECK(std::filesystem::exists(output));
  good(scripts->update(0.5));
  CHECK(weak.lock()->entities()->front().transform.position.x == 0.5F);
  good(scripts->stop());
  scripts.reset();
  CHECK(weak.expired());
  CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("components_preparation_loader_fault_releases_existing_active_handles") {
  Project project;
  project.write(actor);
  REQUIRE(project.files().writeTextAtomic("scripts/bad.ts",
                                          "await new Promise(()=>{});export class Bad{}"));
  auto program = project.compile();
  auto native = scene();
  auto a = entity(native, "{\"speed\":1}");
  auto scripts = service(native, program);
  good(scripts.update(0.25));
  auto changed = native->entity(a)->script.value();
  changed.source = "bad.ts";
  changed.className = "Bad";
  REQUIRE(native->setScript(a, changed));
  auto result = scripts.sync();
  REQUIRE(result);
  CHECK(result->realmFaulted);
  CHECK(scripts.stats()->runtime.instances == 0);
  CHECK(scripts.stats()->failed == 1);
  CHECK(native->entity(a)->transform.position.x == 0.25F);
}

TEST_CASE("components_replacement_stop_resource_fault_releases_newly_prepared_objects") {
  Project project;
  project.write(
      "import {ScriptComponent} from './dot/engine.js';export class Actor extends ScriptComponent{"
      "onStop(){while(true){}}}");
  auto program = project.compile();
  auto native = scene();
  auto id = entity(native, "{}");
  SceneScriptsConfig config;
  config.runtime.milliseconds = 10;
  auto scripts = service(native, program, config);
  good(scripts.start());
  auto changed = native->entity(id)->script.value();
  changed.properties = "{\"changed\":true}";
  REQUIRE(native->setScript(id, changed));
  auto result = scripts.sync();
  REQUIRE(result);
  CHECK(result->realmFaulted);
  REQUIRE(result->failures.size() == 1);
  CHECK(result->failures[0].phase == ScriptPhase::Stop);
  CHECK(scripts.stats()->runtime.instances == 0);
  CHECK(scripts.stats()->failed == 1);
}
