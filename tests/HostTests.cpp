#include <doctest/doctest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.Runtime;
import Dot.Script.Host;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Core.Text;

namespace {
  using namespace Dot::Script;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-host-test-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
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
      REQUIRE(files().writeTextAtomic("main.js", source));
    }
  };

  auto echo() -> HostBindings {
    return {{"echo", [](std::string_view args) -> Dot::Core::Result<std::optional<std::string>> {
               return std::optional{std::string(args)};
             }}};
  }

  auto module(Runtime& runtime) -> ModuleId {
    auto result = runtime.loadModule("main.js");
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }

  auto call(Runtime& runtime, ModuleId entry, std::string_view args = "[]")
      -> std::optional<std::string> {
    auto result = runtime.call(entry, "run", args);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }
}  // namespace

TEST_CASE("host.native_esm_owned_callbacks_and_json_transport_are_real") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(){return "
      "invoke('echo',[42,{text:'中文🙂',nil:null},true]);}");
  auto bindings = echo();
  auto runtime = Runtime::create(project.files(), {}, bindings);
  REQUIRE(runtime);
  bindings.clear();
  CHECK(call(*runtime, module(*runtime)) == "[42,{\"text\":\"中文🙂\",\"nil\":null},true]");
  CHECK(runtime->stats()->modules == 2);
}

TEST_CASE("host.native_module_is_absent_without_registration_and_imports_remain_scoped") {
  Project project;
  project.write("import {invoke} from 'dot:host';export function run(){return invoke('echo',[]);}");
  auto missing = Runtime::create(project.files());
  REQUIRE(missing);
  CHECK_FALSE(missing->loadModule("main.js"));
  CHECK(missing->stats()->faulted);

  for (const auto* name : {"dot:os", "dot:host/sub", "./dot:host", "dot:host\\u0000x"}) {
    project.write("import {invoke} from '" + std::string(name) + "';");
    auto rejected = Runtime::create(project.files(), {}, echo());
    REQUIRE(rejected);
    CHECK_FALSE(rejected->loadModule("main.js"));
  }

  project.write("import {invoke} from 'dot:host';");
  RuntimeConfig limits;
  limits.maxModules = 1;
  auto bounded = Runtime::create(project.files(), limits, echo());
  REQUIRE(bounded);
  CHECK_FALSE(bounded->loadModule("main.js"));
}

TEST_CASE("host_dynamic_import_and_top_level_await_use_one_native_module") {
  Project project;
  project.write(
      "const {invoke}=await import('dot:host');export async function run(){const "
      "other=await import('dot:host');return [invoke===other.invoke,other.invoke('echo',[42])];}");
  auto runtime = Runtime::create(project.files(), {}, echo());
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  CHECK(call(*runtime, entry) == "[true,[42]]");
  CHECK(call(*runtime, entry) == "[true,[42]]");
  CHECK(runtime->stats()->modules == 2);
}

TEST_CASE("host_errors_have_native_code_context_and_are_catchable_without_rollback") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(){try{invoke('fail',[]);}"
      "catch(e){return [e.name,e.code,e.message];}}");
  const std::vector codes{Dot::Core::ErrorCode::InvalidArgument,
                          Dot::Core::ErrorCode::NotFound,
                          Dot::Core::ErrorCode::Io,
                          Dot::Core::ErrorCode::Unavailable,
                          Dot::Core::ErrorCode::InvalidState,
                          Dot::Core::ErrorCode::External};
  const std::vector<std::string> names{"InvalidArgument", "NotFound",     "Io",
                                       "Unavailable",     "InvalidState", "External"};
  int changes = 0;

  for (std::size_t index = 0; index < codes.size(); ++index) {
    HostBindings bindings{{"fail",
                           [&, code = codes[index]](
                               std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
                             ++changes;
                             return Dot::Core::fail(code, "原生失败🙂");
                           }}};
    auto runtime = Runtime::create(project.files(), {}, bindings);
    REQUIRE(runtime);
    const auto result = call(*runtime, module(*runtime)).value();
    CHECK(result.find(names[index]) != std::string::npos);
    CHECK(result.find("DotHostError") != std::string::npos);
    CHECK(result.find("Host fail: 原生失败🙂") != std::string::npos);
    CHECK_FALSE(runtime->stats()->faulted);
  }

  CHECK(changes == 6);
}

TEST_CASE("host_cpp_exceptions_are_contained_and_invalid_result_json_is_an_error") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(){try{return invoke('value',[]);}"
      "catch(e){return e.code;}}");

  for (const auto& text :
       std::vector<std::string>{"", "{", "undefined", "1e309", "\"\\ud800\"",
                                std::string{"\xc0\x80"}, std::string{"[]\0", 3}}) {
    HostBindings bindings{
        {"value", [text](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
           return std::optional{text};
         }}};
    auto runtime = Runtime::create(project.files(), {}, bindings);
    REQUIRE(runtime);
    CHECK(call(*runtime, module(*runtime)) == "\"External\"");
    CHECK_FALSE(runtime->stats()->faulted);
  }

  for (const bool standard : {true, false}) {
    HostBindings bindings{
        {"value", [standard](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
           if (standard) {
             throw std::runtime_error("expected native throw");
           }

           throw 42;
         }}};
    auto runtime = Runtime::create(project.files(), {}, bindings);
    REQUIRE(runtime);
    CHECK(call(*runtime, module(*runtime)) == "\"External\"");
    CHECK_FALSE(runtime->stats()->faulted);
  }
}

TEST_CASE("host_undefined_and_null_results_are_distinct_and_no_host_globals_appear") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(n){return invoke(n?'nil':'empty',[]);}"
      "export function globals(){return [typeof globalThis.invoke,typeof process,typeof os];}");
  HostBindings bindings{
      {"empty",
       [](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         return std::optional<std::string>{};
       }},
      {"nil", [](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         return std::optional<std::string>{"null"};
       }}};
  auto runtime = Runtime::create(project.files(), {}, bindings);
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  CHECK_FALSE(call(*runtime, entry, "[false]"));
  CHECK(call(*runtime, entry, "[true]") == "null");
  CHECK(runtime->call(entry, "globals").value().value()
        == "[\"undefined\",\"undefined\",\"undefined\"]");
}

TEST_CASE("host_argument_validation_prevents_callback_side_effects") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(n){const args=[undefined,{},"
      "[undefined],[NaN],[1n],[Symbol('x')],[()=>1],[Promise.resolve(1)],Array(65).fill(1),"
      "['\\ud800'],Object.assign([],{toJSON(){return {value:1};}})];"
      "try{invoke('value',args[n]);return 'unexpected';}catch(e){return e.code;}}");
  int calls = 0;
  HostBindings bindings{
      {"value", [&](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         ++calls;
         return std::optional<std::string>{"42"};
       }}};
  auto runtime = Runtime::create(project.files(), {}, bindings);
  REQUIRE(runtime);
  const auto entry = module(*runtime);

  for (int index = 0; index < 11; ++index) {
    CHECK(call(*runtime, entry, "[" + std::to_string(index) + "]") == "\"InvalidArgument\"");
  }

  CHECK(calls == 0);
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("host_unknown_method_invalid_names_and_registration_limits_fail_explicitly") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(n){try{return invoke(n,[]);}"
      "catch(e){return e.code;}}");
  auto runtime = Runtime::create(project.files(), {}, echo());
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  CHECK(call(*runtime, entry, "[\"missing\"]") == "\"NotFound\"");
  CHECK(call(*runtime, entry, "[\"echo\\u0000x\"]") == "\"InvalidArgument\"");
  CHECK(call(*runtime, entry, "[42]") == "\"InvalidArgument\"");

  for (const auto* name : {"", ".x", "x.", "x..y", "1x", "x-y", "x/y", "中文"}) {
    auto bindings = echo();
    bindings[0].name = name;
    CHECK_FALSE(Runtime::create(project.files(), {}, bindings));
  }

  auto duplicate = echo();
  duplicate.push_back(duplicate[0]);
  CHECK_FALSE(Runtime::create(project.files(), {}, duplicate));
  CHECK_FALSE(Runtime::create(project.files(), {}, {{"empty", {}}}));
  HostBindings tooMany;

  for (int index = 0; index < 257; ++index) {
    auto binding = echo()[0];
    binding.name = "f" + std::to_string(index);
    tooMany.push_back(std::move(binding));
  }

  CHECK_FALSE(Runtime::create(project.files(), {}, tooMany));
  RuntimeConfig limits;
  limits.maxHostCalls = 0;
  CHECK_FALSE(Runtime::create(project.files(), limits, echo()));
}

TEST_CASE("host_call_limit_is_shared_by_jobs_and_serialization_and_cannot_be_caught_away") {
  Project project;
  RuntimeConfig limits;
  limits.maxHostCalls = 1;

  for (const auto* source :
       {"export function run(){invoke('echo',[]);try{invoke('echo',[]);}catch{}return 42;}",
        "export async function run(){invoke('echo',[]);await Promise.resolve();return "
        "invoke('echo',[]);}",
        "export function run(){invoke('echo',[]);return {toJSON(){return invoke('echo',[]);}};}"}) {
    project.write("import {invoke} from 'dot:host';" + std::string(source));
    auto runtime = Runtime::create(project.files(), limits, echo());
    REQUIRE(runtime);
    auto failed = runtime->call(module(*runtime), "run");
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.find("host-call limit") != std::string::npos);
    CHECK(runtime->stats()->faulted);
  }

  project.write(
      "import {invoke} from 'dot:host';export function run(){return invoke('echo',[42]);}");
  auto runtime = Runtime::create(project.files(), limits, echo());
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  CHECK(call(*runtime, entry) == "[42]");
  CHECK(call(*runtime, entry) == "[42]");
}

TEST_CASE("host_recursive_argument_conversion_checks_limits_before_outer_native_mutation") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(){return invoke('write',"
      "[{toJSON(){try{invoke('write',[]);}catch{}return 42;}}]);}");
  int writes = 0;
  HostBindings bindings{
      {"write", [&](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         ++writes;
         return std::optional<std::string>{"42"};
       }}};
  RuntimeConfig limits;
  limits.maxHostCalls = 1;
  auto runtime = Runtime::create(project.files(), limits, bindings);
  REQUIRE(runtime);
  CHECK_FALSE(runtime->call(module(*runtime), "run"));
  CHECK(runtime->stats()->faulted);
  CHECK(writes == 0);
}

TEST_CASE("host_native_deadline_observation_faults_after_work_and_reentry_is_rejected") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(){try{return invoke('work',[]);}"
      "catch{return 42;}}");
  RuntimeConfig limits;
  limits.milliseconds = 10;
  HostBindings slow{{"work", [](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
                       std::this_thread::sleep_for(std::chrono::milliseconds{30});
                       return std::optional<std::string>{"42"};
                     }}};
  auto bounded = Runtime::create(project.files(), limits, slow);
  REQUIRE(bounded);
  CHECK_FALSE(bounded->call(module(*bounded), "run"));
  CHECK(bounded->stats()->faulted);
  Runtime* active = nullptr;
  ModuleId entry{};
  HostBindings reentry{
      {"work", [&](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         const bool rejected
             = !active->stats() && !active->loadModule("main.js") && !active->call(entry, "run");
         return std::optional<std::string>{rejected ? "true" : "false"};
       }}};
  auto runtime = Runtime::create(project.files(), {}, reentry);
  REQUIRE(runtime);
  active = &*runtime;
  entry = module(*runtime);
  CHECK(call(*runtime, entry) == "true");
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("host_error_properties_do_not_run_script_prototype_setters") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';Object.defineProperty(Error.prototype,'code',"
      "{set(){throw new Error('setter executed');}});export function run(){try{"
      "invoke('missing',[]);}catch(e){return e.code;}}");
  auto runtime = Runtime::create(project.files(), {}, echo());
  REQUIRE(runtime);
  CHECK(call(*runtime, module(*runtime)) == "\"NotFound\"");
}

TEST_CASE("host_json_byte_limits_reject_before_native_mutation_or_result_delivery") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(n){try{return invoke('work',"
      "n?[]:['x'.repeat(65)]);}catch(e){return e.code;}}");
  int calls = 0;
  HostBindings services{
      {"work", [&](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         ++calls;
         return std::optional{std::string(65, '1')};
       }}};
  RuntimeConfig limits;
  limits.jsonBytes = 64;
  auto runtime = Runtime::create(project.files(), limits, services);
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  CHECK(call(*runtime, entry, "[false]") == "\"InvalidArgument\"");
  CHECK(calls == 0);
  CHECK(call(*runtime, entry, "[true]") == "\"External\"");
  CHECK(calls == 1);
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("host_diagnostics_are_bounded_utf8_and_uncaught_errors_keep_method_context") {
  Project project;
  project.write("import {invoke} from 'dot:host';export function run(){return invoke('work',[]);}");

  for (const auto& message : std::vector<std::string>{
           std::string(8191, 'x') + "中文🙂" + std::string(10000, 'x'), std::string{"\xc0\x80"}}) {
    HostBindings services{
        {"work", [message](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
           return Dot::Core::fail(Dot::Core::ErrorCode::Io, message);
         }}};
    auto runtime = Runtime::create(project.files(), {}, services);
    REQUIRE(runtime);
    auto failed = runtime->call(module(*runtime), "run");
    REQUIRE_FALSE(failed);
    CHECK(Dot::Core::isValidUtf8(failed.error().message));
    CHECK(failed.error().message.find("Host work:") != std::string::npos);
    CHECK(failed.error().message.size() < 17000);
    CHECK_FALSE(runtime->stats()->faulted);
  }
}

TEST_CASE("host_callbacks_and_vm_payload_release_after_move_and_failed_initialization") {
  Project project;
  project.write("import {invoke} from 'dot:host';export function run(){return invoke('echo',[]);}");
  const auto before = Runtime::backendMemory();
  std::weak_ptr<int> weak;

  {
    auto owner = std::make_shared<int>(42);
    weak = owner;
    HostBindings bindings{
        {"echo", [owner](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
           return std::optional{std::to_string(*owner)};
         }}};
    auto runtime = Runtime::create(project.files(), {}, bindings);
    REQUIRE(runtime);
    const auto entry = module(*runtime);
    owner.reset();
    bindings.clear();
    auto moved = std::move(*runtime);
    CHECK(call(moved, entry) == "42");
    CHECK_FALSE(weak.expired());
    std::thread destroyer([owned = std::move(moved)] {});
    destroyer.join();
    CHECK(weak.expired());
  }

  RuntimeConfig limits;
  limits.memoryBytes = 128;
  CHECK_FALSE(Runtime::create(project.files(), limits, echo()));
  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("host_deep_valid_json_result_retains_native_context_when_vm_parse_rejects_stack") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export function run(){try{return invoke('deep',[]);}"
      "catch(e){return [e.name,e.code,e.message.includes('Host deep:')];}}");
  const auto before = Runtime::backendMemory();

  {
    const auto text = std::string(4096, '[') + "0" + std::string(4096, ']');
    HostBindings services{
        {"deep", [text](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
           return std::optional{text};
         }}};
    RuntimeConfig limits;
    limits.milliseconds = 1000;
    auto runtime = Runtime::create(project.files(), limits, services);
    REQUIRE(runtime);
    CHECK(call(*runtime, module(*runtime)) == "[\"DotHostError\",\"External\",true]");
    CHECK_FALSE(runtime->stats()->faulted);
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}
