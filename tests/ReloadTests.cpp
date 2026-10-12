#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

import Dot.Core.Files;
import Dot.Core.Result;
import Dot.World.Scene;
import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Script.SceneScripts;
import Dot.Script.LiveScripts;

namespace {
  using namespace Dot::Script;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-reload-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      auto sdk = Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY);
      REQUIRE(sdk);
      REQUIRE(files().createDirectories("scripts/dot"));

      for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
        auto source = sdk->readText(path);
        REQUIRE(source);
        REQUIRE(files().writeTextAtomic(std::filesystem::path{"scripts"} / path, *source));
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

    auto compiler(const CompilerLimits& limits = {}) const -> TypeScriptCompiler {
      auto result = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER, limits);
      REQUIRE(result);
      return *result;
    }

    auto compile() const -> CompiledProgram {
      auto result = compiler().compileProject(files());
      REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
      return std::move(*result);
    }
  };

  auto actor(int factor = 1, std::string_view load = "this.#ticks=state.ticks;",
             std::string_view save = "return {ticks:this.#ticks};") -> std::string {
    return "import {ScriptComponent} from './dot/engine.js';export class Actor extends ScriptComponent{"
           "#ticks=0;onStart(){const t=this.self.transform;t.position.x=0;this.self.transform=t;this.self.name='started';}"
           "onUpdate(){++this.#ticks;const t=this.self.transform;t.position.x+=" + std::to_string(factor)
           + ";this.self.transform=t;this.self.name='tick'+this.#ticks;}"
             "onStop(){this.self.name='stopped';}onSaveState(){" + std::string(save)
           + "}onLoadState(state:{ticks:number}){" + std::string(load) + "}}";
  }

  auto scene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("Reload");
    REQUIRE(result);
    auto shared = std::make_shared<Dot::World::Scene>(std::move(*result));
    auto entity = shared->createEntity("Actor");
    REQUIRE(entity);
    REQUIRE(
        shared->setScript(*entity, Dot::World::ScriptComponent{"actor.ts", "Actor", "{}", true}));
    return shared;
  }

  auto good(Dot::Core::Result<ScriptReport> report) -> void {
    REQUIRE(report);
    REQUIRE(report->failures.empty());
    REQUIRE_FALSE(report->realmFaulted);
  }

  auto good(Dot::Core::Result<LiveScriptReport> report) -> LiveScriptReport {
    REQUIRE(report);
    REQUIRE(report->frame.failures.empty());
    REQUIRE_FALSE(report->frame.realmFaulted);
    return std::move(*report);
  }
}  // namespace

TEST_CASE("reload_source_snapshots_compare_exact_contents_and_compile_captured_bytes") {
  Project project;
  project.write(actor());
  auto compiler = project.compiler();
  auto first = compiler.captureProject(project.files());
  auto same = compiler.captureProject(project.files());
  REQUIRE(first);
  REQUIRE(same);
  CHECK(first->equals(*same));
  std::filesystem::last_write_time(project.root / "scripts/actor.ts",
                                   std::filesystem::file_time_type::clock::now());
  CHECK(first->equals(compiler.captureProject(project.files()).value()));
  project.write(actor(2));
  auto changed = compiler.captureProject(project.files());
  REQUIRE(changed);
  CHECK_FALSE(first->equals(*changed));
  auto before = compiler.compileSnapshot(*first);
  REQUIRE(before);
  auto compiled = before->files()->readText("actor.js");
  REQUIRE(compiled);
  CHECK(compiled->find("+= 1") != std::string::npos);
  CHECK(compiled->find("+= 2") == std::string::npos);
  auto moved = std::move(*first);
  CHECK_FALSE(first->equals(moved));
  CHECK_FALSE(compiler.compileSnapshot(*first));
  CompilerLimits limits;
  limits.fileBytes = 1;
  CHECK_FALSE(project.compiler(limits).compileSnapshot(moved));
}

TEST_CASE("reload_live_explicit_start_uses_prepared_generation_without_polling") {
  Project project;
  project.write(actor());
  auto native = scene();
  LiveScriptsConfig config;
  config.pollInterval = std::chrono::milliseconds{0};
  auto scripts = LiveScripts::create(native, project.files(), project.compiler(), config);
  REQUIRE(scripts);
  project.write("export const broken: number = 'invalid';");
  good(scripts->start());
  CHECK(native->entities()->front().name == "started");
  CHECK(scripts->stats()->compileAttempts == 1);
  good(scripts->start());
  CHECK(native->entities()->front().name == "started");
  auto frame = good(scripts->update(0));
  CHECK(frame.source.error.has_value());
  CHECK(native->entities()->front().name == "tick1");
  good(scripts->start());
  CHECK(native->entities()->front().name == "tick1");
  good(scripts->stop());
  CHECK_FALSE(scripts->start());
}

TEST_CASE("reload_new_realm_migrates_private_state_and_skips_duplicate_start_old_stop") {
  Project project;
  project.write(actor());
  auto native = scene();
  auto old = project.compile();
  auto scripts = SceneScripts::create(native, old);
  REQUIRE(scripts);
  good(scripts->update(0));
  good(scripts->update(0));
  CHECK(native->entities()->front().name == "tick2");
  CHECK(native->entities()->front().transform.position.x == 2);
  project.write(actor(3));
  auto next = project.compile();
  auto report = scripts->reload(next);
  REQUIRE_MESSAGE(report, (report ? "" : report.error().message));
  CHECK(report->migrated == 1);
  CHECK(report->stateBytes > 0);
  CHECK(scripts->stats()->running == 1);
  CHECK(native->entities()->front().name == "tick2");
  good(scripts->update(0));
  CHECK(native->entities()->front().name == "tick3");
  CHECK(native->entities()->front().transform.position.x == 5);
  good(scripts->stop());
  CHECK(native->entities()->front().name == "stopped");
}

TEST_CASE("reload_constructor_initializer_and_state_load_failure_keep_old_realm_running") {
  Project project;
  project.write(actor());
  auto native = scene();
  auto scripts = SceneScripts::create(native, project.compile());
  REQUIRE(scripts);
  good(scripts->update(0));

  for (const auto& bad :
       {actor(10, "throw Error('load failed');"),
        std::string{"import {ScriptComponent} from './dot/engine.js';export class Actor extends "
                    "ScriptComponent{constructor(c:any){super(c);throw Error('ctor failed');}}"},
        std::string{
            "import {scene} from './dot/engine.js';scene.rename('BAD');export class Actor{}"}}) {
    project.write(bad);
    auto candidate = project.compile();
    const auto before = native->entities()->front().transform.position.x;
    CHECK_FALSE(scripts->reload(candidate));
    CHECK(native->name().value() == "Reload");
    CHECK(native->entities()->front().transform.position.x == before);
    CHECK_FALSE(scripts->stats()->faulted);
    good(scripts->update(0));
    CHECK(native->entities()->front().transform.position.x == before + 1);
  }
}

TEST_CASE("reload_native_writes_are_denied_in_save_and_load_hooks") {
  Project project;
  project.write(
      actor(1, "this.#ticks=state.ticks;", "this.self.name='BAD';return {ticks:this.#ticks};"));
  auto native = scene();
  auto scripts = SceneScripts::create(native, project.compile());
  REQUIRE(scripts);
  good(scripts->update(0));
  project.write(actor(2));
  CHECK_FALSE(scripts->reload(project.compile()));
  CHECK(native->entities()->front().name == "tick1");
  good(scripts->update(0));
  CHECK(native->entities()->front().name == "tick2");
  auto clean = SceneScripts::create(native, project.compile());
  REQUIRE(clean);
  good(clean->update(0));
  project.write(actor(3, "this.self.name='BAD';this.#ticks=state.ticks;"));
  CHECK_FALSE(clean->reload(project.compile()));
  CHECK(native->entities()->front().name == "tick1");
  good(clean->update(0));
  CHECK(native->entities()->front().name == "tick2");
}

TEST_CASE("reload_state_contract_missing_loader_undefined_nonvoid_and_aggregate_limits_fail") {
  Project project;
  auto native = scene();
  project.write(actor());
  SceneScriptsConfig config;
  config.reloadStateBytes = 1;
  auto bounded = SceneScripts::create(native, project.compile(), config);
  REQUIRE(bounded);
  good(bounded->update(0));
  project.write(actor(2));
  CHECK_FALSE(bounded->reload(project.compile()));
  CHECK_FALSE(bounded->stats()->faulted);
  auto old = SceneScripts::create(native, project.compile());
  REQUIRE(old);
  good(old->update(0));
  project.write("export class Actor{};");
  CHECK_FALSE(old->reload(project.compile()));
  project.write(actor(3, "return 42;"));
  CHECK_FALSE(old->reload(project.compile()));
  project.write(actor(1, "this.#ticks=state.ticks;", "return undefined;"));
  auto empty = SceneScripts::create(native, project.compile());
  REQUIRE(empty);
  good(empty->update(0));
  project.write(actor(2));
  CHECK_FALSE(empty->reload(project.compile()));
}

TEST_CASE("reload_faulted_realm_can_recover_and_unstarted_or_new_components_start_normally") {
  Project project;
  project.write(
      "import {ScriptComponent} from './dot/engine.js';export class Actor extends "
      "ScriptComponent{onUpdate(){while(true){}}}");
  auto native = scene();
  SceneScriptsConfig config;
  config.runtime.milliseconds = 10;
  auto scripts = SceneScripts::create(native, project.compile(), config);
  REQUIRE(scripts);
  auto failed = scripts->update(0);
  REQUIRE(failed);
  CHECK(failed->realmFaulted);
  project.write(actor(2));
  auto recovered = scripts->reload(project.compile());
  REQUIRE(recovered);
  CHECK(recovered->migrated == 0);
  good(scripts->update(0));
  CHECK(native->entities()->front().transform.position.x == 2);
  auto added = native->createEntity("Added");
  REQUIRE(added);
  REQUIRE(native->setScript(*added, Dot::World::ScriptComponent{"actor.ts", "Actor", "{}", true}));
  auto next = scripts->reload(project.compile());
  REQUIRE(next);
  CHECK(scripts->stats()->pending == 1);
  good(scripts->update(0));
  CHECK(native->entity(*added)->name == "tick1");
}

TEST_CASE(
    "reload_polling_compiler_failures_keep_old_updates_and_identical_failed_sources_do_not_retry") {
  Project project;
  project.write(actor());
  auto native = scene();
  LiveScriptsConfig config;
  config.pollInterval = std::chrono::milliseconds{0};
  auto live = LiveScripts::create(native, project.files(), project.compiler(), config);
  REQUIRE(live);
  good(live->update(0));
  project.write("export const wrong:number='type failure';");
  auto failed = good(live->update(0));
  REQUIRE(failed.source.error);
  CHECK(failed.source.error->message.find("TS2322") != std::string::npos);
  CHECK(native->entities()->front().name == "tick2");
  const auto attempts = live->stats()->compileAttempts;
  auto repeated = good(live->update(0));
  CHECK(repeated.source.error);
  CHECK(live->stats()->compileAttempts == attempts);
  CHECK(native->entities()->front().name == "tick3");
  project.write(actor(3));
  auto replaced = good(live->update(0));
  CHECK(replaced.source.committed);
  CHECK_FALSE(replaced.source.error);
  CHECK(native->entities()->front().name == "tick4");
  CHECK(native->entities()->front().transform.position.x == 6);
  CHECK(live->stats()->reloads == 1);
}

TEST_CASE("reload_poll_interval_force_retry_and_invalid_delta_have_explicit_boundaries") {
  Project project;
  project.write(actor());
  auto native = scene();
  LiveScriptsConfig config;
  config.pollInterval = std::chrono::milliseconds{60000};
  auto live = LiveScripts::create(native, project.files(), project.compiler(), config);
  REQUIRE(live);
  CHECK_FALSE(live->poll()->checked);
  CHECK_FALSE(live->update(-1));
  CHECK(live->stats()->compileAttempts == 1);
  auto forced = live->poll(true);
  REQUIRE(forced);
  CHECK(forced->checked);
  CHECK_FALSE(forced->changed);
  CHECK(forced->committed);
  CHECK(live->stats()->compileAttempts == 2);
  config.pollInterval = std::chrono::milliseconds{-1};
  CHECK_FALSE(LiveScripts::create(native, project.files(), project.compiler(), config));
  good(live->stop());
  CHECK_FALSE(live->poll(true));
}

TEST_CASE("reload_capture_failure_removal_and_added_imports_report_without_faking_success") {
  Project project;
  project.write(actor());
  auto native = scene();
  LiveScriptsConfig config;
  config.pollInterval = std::chrono::milliseconds{0};
  auto live = LiveScripts::create(native, project.files(), project.compiler(), config);
  REQUIRE(live);
  good(live->update(0));
  const auto original = project.files().readText("scripts/actor.ts").value();
  std::filesystem::remove_all(project.root / "scripts");
  auto missing = good(live->update(0));
  REQUIRE(missing.source.error);
  CHECK(native->entities()->front().name == "tick2");
  REQUIRE(project.files().createDirectories("scripts/dot"));
  auto sdk = Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY);
  REQUIRE(sdk);

  for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
    REQUIRE(project.files().writeTextAtomic(std::filesystem::path{"scripts"} / path,
                                            sdk->readText(path).value()));
  }

  project.write(original + "\nimport './extra.js';\n");
  REQUIRE(project.files().writeTextAtomic("scripts/extra.ts", "export const value=42;"));
  auto repaired = good(live->update(0));
  CHECK(repaired.source.committed);
  CHECK_FALSE(repaired.source.error);
}

TEST_CASE("reload_moves_thread_scope_and_failed_candidates_release_generations_and_vm_storage") {
  Project project;
  project.write(actor());
  auto native = scene();
  const auto memory = Runtime::backendMemory();
  const auto generations = compilerGenerations();

  {
    LiveScriptsConfig config;
    config.pollInterval = std::chrono::milliseconds{0};
    auto live = LiveScripts::create(native, project.files(), project.compiler(), config);
    REQUIRE(live);
    good(live->update(0));
    const auto retained = compilerGenerations();
    project.write(actor(2, "throw Error('candidate failure');"));
    auto rejected = good(live->update(0));
    REQUIRE(rejected.source.error);
    CHECK(compilerGenerations() == retained);
    auto moved = std::move(*live);
    CHECK_FALSE(live->poll());
    bool wrongThread = false;
    std::thread worker([&] {
      wrongThread = !moved.update(0) && !moved.poll(true) && !moved.stats() && !moved.stop();
    });
    worker.join();
    CHECK(wrongThread);
    project.write(actor(3));
    CHECK(good(moved.update(0)).source.committed);
    std::thread destroyer([owned = std::move(moved)] {});
    destroyer.join();
  }

  CHECK(compilerGenerations() == generations);
  CHECK(Runtime::backendMemory().bytes == memory.bytes);
  CHECK(Runtime::backendMemory().allocations == memory.allocations);
}

TEST_CASE("reload_restored_identical_capture_clears_filesystem_error_without_recompiling") {
  Project project;
  project.write(actor());
  auto native = scene();
  LiveScriptsConfig config;
  config.pollInterval = std::chrono::milliseconds{0};
  auto live = LiveScripts::create(native, project.files(), project.compiler(), config);
  REQUIRE(live);
  good(live->update(0));
  std::filesystem::rename(project.root / "scripts", project.root / "temporarily-hidden");
  REQUIRE(good(live->update(0)).source.error);
  const auto attempts = live->stats()->compileAttempts;
  std::filesystem::rename(project.root / "temporarily-hidden", project.root / "scripts");
  auto restored = good(live->update(0));
  CHECK_FALSE(restored.source.error);
  CHECK_FALSE(live->stats()->error);
  CHECK(live->stats()->compileAttempts == attempts);
  CHECK(native->entities()->front().name == "tick3");
}

TEST_CASE("reload_removed_disabled_and_changed_type_components_stop_after_successful_commit") {
  Project project;
  project.write(actor());
  auto native = scene();
  auto id = native->entities()->front().id;
  auto scripts = SceneScripts::create(native, project.compile());
  REQUIRE(scripts);
  good(scripts->update(0));
  auto description = native->entity(id)->script.value();
  description.enabled = false;
  REQUIRE(native->setScript(id, description));
  auto disabled = scripts->reload(project.compile());
  REQUIRE(disabled);
  CHECK(disabled->retired.failures.empty());
  CHECK(disabled->retired.removed == 1);
  CHECK(native->entity(id)->name == "stopped");
  CHECK(scripts->stats()->disabled == 1);
  description.enabled = true;
  REQUIRE(native->setScript(id, description));
  good(scripts->update(0));
  REQUIRE(native->setScript(id, std::nullopt));
  auto removed = scripts->reload(project.compile());
  REQUIRE(removed);
  CHECK(removed->retired.removed == 1);
  CHECK(removed->components == 0);
  CHECK(native->entity(id)->name == "stopped");
}

TEST_CASE("reload_cleanup_failure_reports_committed_generation_and_lost_candidate_owner") {
  Project project;
  project.write(
      "import {ScriptComponent} from './dot/engine.js';export class Actor extends ScriptComponent{"
      "onStop(){this.self.destroy();throw Error('cleanup failure');}}export class Other extends "
      "ScriptComponent{}");
  auto native = scene();
  auto id = native->entities()->front().id;
  auto scripts = SceneScripts::create(native, project.compile());
  REQUIRE(scripts);
  good(scripts->start());
  auto description = native->entity(id)->script.value();
  description.className = "Other";
  REQUIRE(native->setScript(id, description));
  auto committed = scripts->reload(project.compile());
  REQUIRE(committed);
  CHECK(committed->retired.failures.size() == 2);
  CHECK(committed->components == 0);
  CHECK(scripts->stats()->runtime.instances == 0);
  CHECK(native->entityCount().value() == 0);
}
