#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.Core.Result;

namespace {
  using namespace Dot::Script;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = std::filesystem::temp_directory_path()
               / ("dot-script-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;

        if (std::filesystem::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create script test directory");
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

    auto write(std::string_view path, std::string_view source) const -> void {
      auto file = std::filesystem::path{std::u8string(path.begin(), path.end())};
      auto access = files();

      if (!file.parent_path().empty()) {
        REQUIRE(access.createDirectories(file.parent_path()));
      }

      REQUIRE(access.writeTextAtomic(file, source));
    }
  };

  auto load(Runtime& runtime, std::string_view path = "main.js") -> ModuleId {
    auto module = runtime.loadModule(path);
    REQUIRE_MESSAGE(module, (module ? "" : module.error().message));
    return *module;
  }

  auto call(Runtime& runtime, ModuleId module, std::string_view function = "run",
            std::string_view args = "[]") -> std::optional<std::string> {
    auto result = runtime.call(module, function, args);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }
}  // namespace

TEST_CASE("script.esm_relative_graph_and_imported_entry_reuse_owned_live_bindings") {
  Project project;
  project.write("shared/counter.js", "let count=0;export function next(){return ++count;}");
  project.write("game/main.js",
                "import {next} from '../shared/counter';export function run(){return next();}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto main = load(*runtime, "game/main.js");
  CHECK(call(*runtime, main) == "1");
  auto counter = load(*runtime, "shared/counter.js");
  CHECK(call(*runtime, counter, "next") == "2");
  CHECK(call(*runtime, main) == "3");
  CHECK(runtime->loadModule("game/main.js").value() == main);
  CHECK(runtime->stats()->modules == 2);
  CHECK(runtime->stats()->entries == 2);
  project.write("shared/counter.js", "export function next(){return 999;}");
  CHECK(call(*runtime, main) == "4");
}

TEST_CASE("script.json_arguments_unicode_undefined_and_null_are_distinct") {
  Project project;
  project.write("中文🙂.mjs",
                "export function run(a,b){return {text:a,value:b+1};} export function empty(){} "
                "export function nil(){return null;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime, "中文🙂.mjs");
  CHECK(call(*runtime, module, "run", "[\"中文🙂\",41]") == "{\"text\":\"中文🙂\",\"value\":42}");
  CHECK_FALSE(call(*runtime, module, "empty"));
  CHECK(call(*runtime, module, "nil") == "null");
}

TEST_CASE("script.top_level_await_and_returned_promises_drain_real_jobs") {
  Project project;
  project.write("main.js",
                "const n=await Promise.resolve(40);export async function run(a){await "
                "Promise.resolve();return n+a;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);
  CHECK(call(*runtime, module, "run", "[2]") == "42");
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("script.dynamic_import_loads_real_module_and_keeps_module_state") {
  Project project;
  project.write("part.js", "export let count=0;export function next(){return ++count;}");
  project.write("main.js",
                "export async function run(){const m=await import('./part');return m.next();}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);
  CHECK(call(*runtime, module) == "1");
  CHECK(call(*runtime, module) == "2");
  auto part = load(*runtime, "part.js");
  CHECK(call(*runtime, part, "next") == "3");
}

TEST_CASE("script.cyclic_module_graph_resolves_without_duplicate_execution") {
  Project project;
  project.write(
      "a.js",
      "import {read} from './b';export const value=42;export function run(){return read();}");
  project.write("b.js", "import {value} from './a';export function read(){return value;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  CHECK(call(*runtime, load(*runtime, "a.js")) == "42");
  CHECK(runtime->stats()->modules == 2);
}

TEST_CASE("script.ordinary_exception_has_context_and_does_not_rollback_mutations") {
  Project project;
  project.write("main.js",
                "let count=0;export function fail(){++count;throw new Error('失败🙂');}export "
                "function run(){return count;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);
  auto failed = runtime->call(module, "fail");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("main.js::fail") != std::string::npos);
  CHECK(failed.error().message.find("失败🙂") != std::string::npos);
  CHECK_FALSE(runtime->stats()->faulted);
  CHECK(call(*runtime, module) == "1");
}

TEST_CASE("script.syntax_missing_and_corrupt_source_failures_fault_and_release_realm") {
  Project project;
  const auto before = Runtime::backendMemory();

  for (const auto& source :
       std::vector<std::string>{"export function {", "import './missing';", std::string{"\xc0\x80"},
                                std::string{"let a='\0';", 10}}) {
    project.write("main.js", source);
    auto runtime = Runtime::create(project.files());
    REQUIRE(runtime);
    CHECK_FALSE(runtime->loadModule("main.js"));
    CHECK(runtime->stats()->faulted);
    CHECK_FALSE(runtime->loadModule("main.js"));
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("script.paths_and_imports_reject_escaping_bare_and_unsupported_attributes") {
  Project project;
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);

  for (const auto& path :
       std::vector<std::string>{"", "/x.js", "../x.js", "./x.js", "a//x.js", "a\\x.js", "C:/x.js",
                                "x.ts", std::string{"x\0.js", 5}}) {
    CHECK_FALSE(runtime->loadModule(path));
    CHECK_FALSE(runtime->stats()->faulted);
  }

  project.write("ok.js", "export const value=42;");

  for (const auto* source :
       {"import 'ok.js';", "import '../ok';", "import './ok' with {type:'json'};"}) {
    project.write("main.js", source);
    auto candidate = Runtime::create(project.files());
    REQUIRE(candidate);
    CHECK_FALSE(candidate->loadModule("main.js"));
    CHECK(candidate->stats()->faulted);
  }
}

TEST_CASE("script.embedded_nul_static_and_dynamic_imports_never_alias_valid_file") {
  Project project;
  project.write("ok.js", "export const value=42;");
  project.write("main.js", "import './ok.js\\u0000evil';");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto rejected = runtime->loadModule("main.js");
  REQUIRE_FALSE(rejected);
  CHECK(rejected.error().message.find("NUL") != std::string::npos);
  project.write("main.js",
                "export async function run(){return (await import('./ok.js\\u0000evil')).value;}");
  auto dynamic = Runtime::create(project.files());
  REQUIRE(dynamic);
  auto result = dynamic->call(load(*dynamic), "run");
  REQUIRE_FALSE(result);
  CHECK(result.error().message.find("NUL") != std::string::npos);
}

TEST_CASE("script.invalid_handles_names_and_argument_boundaries_are_recoverable") {
  Project project;
  project.write("main.js", "export function run(){return 42;}export const value=1;");
  auto runtime = Runtime::create(project.files());
  auto other = Runtime::create(project.files());
  REQUIRE(runtime);
  REQUIRE(other);
  auto module = load(*runtime);
  auto foreign = load(*other);
  CHECK_FALSE(runtime->call(foreign, "run"));
  CHECK_FALSE(runtime->call({module.runtime, 999}, "run"));
  CHECK_FALSE(runtime->call(module, "missing"));
  CHECK_FALSE(runtime->call(module, "value"));
  CHECK_FALSE(runtime->call(module, std::string{"run\0wrong", 9}));

  for (const auto* input : {"{", "{}", "null", "[1] trailing"}) {
    CHECK_FALSE(runtime->call(module, "run", input));
  }

  std::string tooMany = "[0";

  for (int index = 1; index < 65; ++index) {
    tooMany += ",0";
  }

  tooMany += ']';
  CHECK_FALSE(runtime->call(module, "run", tooMany));
  CHECK_FALSE(runtime->stats()->faulted);
  CHECK(call(*runtime, module) == "42");
}

TEST_CASE("script.non_json_results_are_errors_instead_of_omitted_or_substituted_values") {
  Project project;
  project.write(
      "main.js",
      "export function run(n){const bad=[NaN,Infinity,1n,Symbol('x'),()=>1,undefined];return "
      "{data:bad[n]};}export function cycle(){const a={};a.self=a;return a;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);

  for (int index = 0; index < 7; ++index) {
    CHECK_FALSE(runtime->call(module, "run", "[" + std::to_string(index) + "]"));
    CHECK_FALSE(runtime->stats()->faulted);
  }

  CHECK_FALSE(runtime->call(module, "cycle"));
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("script.infinite_call_and_module_initializer_are_interrupted") {
  Project project;
  RuntimeConfig config;
  config.milliseconds = 10;
  project.write("main.js", "export function run(){while(true){}}");
  auto runtime = Runtime::create(project.files(), config);
  REQUIRE(runtime);
  auto module = load(*runtime);
  const auto start = std::chrono::steady_clock::now();
  auto failed = runtime->call(module, "run");
  REQUIRE_FALSE(failed);
  CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds{2});
  CHECK(failed.error().message.find("time limit") != std::string::npos);
  CHECK(runtime->stats()->faulted);
  CHECK_FALSE(runtime->call(module, "run"));
  project.write("main.js", "while(true){} export function run(){}");
  auto initializer = Runtime::create(project.files(), config);
  REQUIRE(initializer);
  CHECK_FALSE(initializer->loadModule("main.js"));
  CHECK(initializer->stats()->faulted);
}

TEST_CASE("script.exception_diagnostic_getters_cannot_bypass_execution_limit") {
  Project project;
  project.write("main.js", "export function run(){throw {toString(){while(true){}}};}");
  RuntimeConfig config;
  config.milliseconds = 10;
  auto runtime = Runtime::create(project.files(), config);
  REQUIRE(runtime);
  auto failed = runtime->call(load(*runtime), "run");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("time limit") != std::string::npos);
  CHECK(runtime->stats()->faulted);
}

TEST_CASE("script_memory_limit_failure_retains_bounded_payload_and_releases_every_allocation") {
  Project project;
  project.write("main.js", "export function run(){return new Uint8Array(16777216);}");
  const auto before = Runtime::backendMemory();

  {
    RuntimeConfig config;
    config.memoryBytes = 1024 * 1024;
    auto runtime = Runtime::create(project.files(), config);
    REQUIRE(runtime);
    auto failed = runtime->call(load(*runtime), "run");
    REQUIRE_FALSE(failed);
    INFO(failed.error().message);
    CHECK(runtime->stats()->faulted);
    CHECK(runtime->stats()->memory.bytes <= config.memoryBytes);
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("script.pending_promises_unhandled_rejections_and_infinite_jobs_fault_realm") {
  Project project;
  RuntimeConfig config;
  config.maxJobs = 8;
  config.milliseconds = 1000;

  for (const auto* source :
       {"export function run(){return new Promise(()=>{});}",
        "export function run(){Promise.reject(new Error('unhandled'));return 1;}",
        "export function run(){const next=()=>Promise.resolve().then(next);next();return 1;}"}) {
    project.write("main.js", source);
    auto runtime = Runtime::create(project.files(), config);
    REQUIRE(runtime);
    auto module = load(*runtime);
    CHECK_FALSE(runtime->call(module, "run"));
    CHECK(runtime->stats()->faulted);
  }
}

TEST_CASE("script.handled_rejections_and_rejected_returned_promise_have_correct_ownership") {
  Project project;
  project.write("main.js",
                "export async function fail(){throw new Error('rejected');}export async function "
                "run(){return await Promise.reject('handled').catch(()=>42);}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);
  CHECK_FALSE(runtime->call(module, "fail"));
  CHECK_FALSE(runtime->stats()->faulted);
  CHECK(call(*runtime, module) == "42");
}

TEST_CASE("script_source_json_and_module_limits_reject_boundaries") {
  Project project;
  project.write(
      "main.js",
      "import './dep'; export function run(){return 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx';}");
  project.write("dep.js", "export const value=1;");
  RuntimeConfig config;
  config.maxModules = 1;
  auto modules = Runtime::create(project.files(), config);
  REQUIRE(modules);
  CHECK_FALSE(modules->loadModule("main.js"));
  config.maxModules = 256;
  config.sourceBytes = 10;
  auto source = Runtime::create(project.files(), config);
  REQUIRE(source);
  CHECK_FALSE(source->loadModule("main.js"));
  config.sourceBytes = 1024;
  config.jsonBytes = 8;
  auto json = Runtime::create(project.files(), config);
  REQUIRE(json);
  auto module = load(*json);
  CHECK_FALSE(json->call(module, "run", "[123456789]"));
  CHECK_FALSE(json->call(module, "run"));
  CHECK_FALSE(json->stats()->faulted);
}

TEST_CASE("script.realm_has_no_implicit_host_io_bindings") {
  Project project;
  project.write("main.js",
                "export function run(){return [typeof std,typeof os,typeof process,typeof "
                "require,typeof fetch,typeof setTimeout];}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  CHECK(call(*runtime, load(*runtime))
        == "[\"undefined\",\"undefined\",\"undefined\",\"undefined\",\"undefined\",\"undefined\"]");
}

TEST_CASE("script.stack_overflow_is_reported_without_crashing_owner") {
  Project project;
  project.write("main.js", "export function run(){return run();}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto failed = runtime->call(load(*runtime), "run");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("stack") != std::string::npos);
}

TEST_CASE("script.move_thread_scope_and_foreign_quiescent_destruction_release_resources") {
  Project project;
  project.write("main.js", "const cycle={};cycle.self=cycle;export function run(){return 42;}");
  const auto before = Runtime::backendMemory();

  for (int attempt = 0; attempt < 12; ++attempt) {
    auto runtime = Runtime::create(project.files());
    REQUIRE(runtime);
    auto module = load(*runtime);
    auto moved = std::move(*runtime);
    CHECK_FALSE(runtime->stats());
    CHECK(call(moved, module) == "42");
    std::atomic<bool> rejected{};
    std::thread worker([&] {
      rejected = !moved.call(module, "run") && !moved.stats() && !moved.loadModule("main.js");
    });
    worker.join();
    CHECK(rejected.load());
    std::thread destroyer([owned = std::move(moved)]() mutable {});
    destroyer.join();
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("script.failed_initialization_and_invalid_configuration_release_partial_runtime") {
  Project project;
  const auto before = Runtime::backendMemory();
  RuntimeConfig config;

  for (auto bytes : std::vector<std::uint64_t>{1, 128, 4096, 16384}) {
    config.memoryBytes = bytes;
    CHECK_FALSE(Runtime::create(project.files(), config));
    CHECK(Runtime::backendMemory().bytes == before.bytes);
    CHECK(Runtime::backendMemory().allocations == before.allocations);
  }

  config = {};
  config.stackBytes = 1;
  CHECK_FALSE(Runtime::create(project.files(), config));
  config = {};
  config.maxJobs = 0;
  CHECK_FALSE(Runtime::create(project.files(), config));
  config = {};
  config.milliseconds = 0;
  CHECK_FALSE(Runtime::create(project.files(), config));
}

TEST_CASE("script_top_level_non_json_returns_never_become_invalid_json_text") {
  Project project;
  project.write("main.js",
                "export function run(n){return [Symbol('x'),()=>1,NaN,Infinity,1n][n];}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);

  for (int index = 0; index < 5; ++index) {
    CHECK_FALSE(runtime->call(module, "run", "[" + std::to_string(index) + "]"));
  }
}

TEST_CASE("script_throwing_stack_getter_retains_original_exception_without_coercing_sentinel") {
  Project project;
  project.write("main.js",
                "export function run(){const error=new "
                "Error('original');Object.defineProperty(error,'stack',{get(){throw "
                "'nested';}});throw error;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto failed = runtime->call(load(*runtime), "run");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("original") != std::string::npos);
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("script_job_budget_is_shared_by_execution_and_serialization") {
  Project project;
  project.write("main.js",
                "export function run(){Promise.resolve().then(()=>1);return "
                "{toJSON(){Promise.resolve().then(()=>2);return 42;}};}");
  RuntimeConfig config;
  config.maxJobs = 1;
  auto runtime = Runtime::create(project.files(), config);
  REQUIRE(runtime);
  auto failed = runtime->call(load(*runtime), "run");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("job limit") != std::string::npos);
  CHECK(runtime->stats()->faulted);
}

TEST_CASE("script_json_argument_views_are_copied_and_empty_or_nul_buffers_are_rejected") {
  Project project;
  project.write("main.js", "export function run(value){return value+1;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto module = load(*runtime);
  const std::string buffer = "[41]unrelated storage";
  CHECK(call(*runtime, module, "run", std::string_view{buffer}.substr(0, 4)) == "42");
  CHECK_FALSE(runtime->call(module, "run", std::string_view{}));
  CHECK_FALSE(runtime->call(module, "run", std::string{"[]\0garbage", 10}));
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("script_enumerable_symbol_result_keys_are_rejected_instead_of_dropped") {
  Project project;
  project.write("main.js", "export function run(){return {[Symbol('secret')]:42,value:1};}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  auto failed = runtime->call(load(*runtime), "run");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("symbol keys") != std::string::npos);
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("script_engine_module_is_owned_cached_and_missing_services_are_explicit") {
  Project project;
  project.write("main.js", R"js(
import {keys, input} from 'dot:engine';
export async function run() {
  const again = await import('dot:engine');
  let missing = false;
  try { input.snapshot(); } catch (error) { missing = error.name === 'DotHostError' && error.message.includes('Host input.snapshot:'); }
  return {same: again.keys === keys, first: keys[0], missing};
}
)js");
  RuntimeConfig limits;
  limits.maxModules = 1;
  auto runtime = Runtime::create(project.files(), limits).value();
  auto module = load(runtime);
  CHECK(call(runtime, module) == "{\"same\":true,\"first\":\"A\",\"missing\":true}");
  CHECK(runtime.stats()->modules == 3);
  CHECK_FALSE(runtime.stats()->faulted);
}
