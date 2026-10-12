#include <doctest/doctest.h>

#include <atomic>
#include <expected>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
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
      root = std::filesystem::temp_directory_path()
             / ("dot-instance-" + std::to_string(random()) + "-" + std::to_string(random()));
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

    auto write(std::string_view code) const -> void {
      REQUIRE(files().writeTextAtomic("main.js", code));
    }
  };

  auto module(Runtime& runtime) -> ModuleId {
    auto result = runtime.loadModule("main.js");
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }

  auto instance(Runtime& runtime, ModuleId module, std::string_view args = "[]",
                std::string_view constructor = "Counter") -> InstanceId {
    auto result = runtime.createInstance(module, constructor, args);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }

  auto call(Runtime& runtime, InstanceId instance, std::string_view method = "next",
            std::string_view args = "[]") -> std::optional<std::string> {
    auto result = runtime.callMethod(instance, method, args);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }
}  // namespace

TEST_CASE("instance_private_fields_inheritance_and_this_keep_independent_state") {
  Project project;
  project.write(
      "class Base{#value;constructor(n=0){this.#value=n;}next(step=1){this.#value+=step;"
      "return this.#value;}read(){return this.#value;}}export class Counter extends Base{};");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  const auto first = instance(*runtime, entry, "[40]");
  const auto second = instance(*runtime, entry, "[100]");
  CHECK(call(*runtime, first) == "41");
  CHECK(call(*runtime, second) == "101");
  CHECK(call(*runtime, first, "next", "[2]") == "43");
  CHECK(call(*runtime, second, "read") == "101");
  CHECK(runtime->hasMethod(first, "read").value());
  CHECK(runtime->stats()->instances == 2);
}

TEST_CASE("instance_factory_returned_object_aliasing_and_async_methods_follow_javascript") {
  Project project;
  project.write(
      "const shared={value:0,async next(){await Promise.resolve();return ++this.value;}};"
      "export function Counter(){return shared;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  const auto first = instance(*runtime, entry);
  const auto second = instance(*runtime, entry);
  CHECK(first != second);
  CHECK(call(*runtime, first) == "1");
  CHECK(call(*runtime, second) == "2");
  REQUIRE(runtime->releaseInstance(first));
  CHECK(call(*runtime, second) == "3");
}

TEST_CASE("instance_capacity_is_checked_before_constructor_side_effects_and_ids_never_reuse") {
  Project project;
  project.write(
      "let count=0;export class Counter{constructor(){++count;}next(){return count;}}"
      "export function created(){return count;}");
  RuntimeConfig config;
  config.maxInstances = 1;
  auto runtime = Runtime::create(project.files(), config);
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  const auto first = instance(*runtime, entry);
  CHECK_FALSE(runtime->createInstance(entry, "Counter"));
  CHECK(runtime->call(entry, "created").value().value() == "1");
  REQUIRE(runtime->releaseInstance(first));
  const auto second = instance(*runtime, entry);
  CHECK(second.value > first.value);
  CHECK_FALSE(runtime->callMethod(first, "next"));
  CHECK_FALSE(runtime->releaseInstance(first));
  CHECK(call(*runtime, second) == "2");
  config.maxInstances = 0;
  CHECK_FALSE(Runtime::create(project.files(), config));
  config.maxInstances = 10001;
  CHECK_FALSE(Runtime::create(project.files(), config));
}

TEST_CASE("instance_constructor_errors_preserve_prior_mutations_and_release_partial_objects") {
  Project project;
  project.write(
      "let count=0;export class Counter{constructor(bad){++count;this.data=new Uint8Array(1024);"
      "if(bad)throw new Error('constructor failure');}next(){return count;}}");
  const auto before = Runtime::backendMemory();

  {
    auto runtime = Runtime::create(project.files());
    REQUIRE(runtime);
    const auto entry = module(*runtime);
    auto failed = runtime->createInstance(entry, "Counter", "[true]");
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.find("Construct main.js::Counter") != std::string::npos);
    CHECK(failed.error().message.find("constructor failure") != std::string::npos);
    CHECK(runtime->stats()->instances == 0);
    CHECK_FALSE(runtime->stats()->faulted);
    CHECK(call(*runtime, instance(*runtime, entry, "[false]")) == "2");
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("instance_nonconstructable_exports_and_promise_constructor_results_are_errors") {
  Project project;
  project.write(
      "export const arrow=()=>({});export const number=42;export async function asyncFn(){};"
      "export function Counter(n){return n?Promise.reject(new "
      "Error('rejected')):Promise.resolve({});}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  const auto entry = module(*runtime);

  for (const auto* name : {"missing", "arrow", "number", "asyncFn"}) {
    CHECK_FALSE(runtime->createInstance(entry, name));
  }

  CHECK_FALSE(runtime->createInstance(entry, "Counter", "[false]"));
  CHECK_FALSE(runtime->createInstance(entry, "Counter", "[true]"));
  CHECK(runtime->stats()->instances == 0);
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("instance_argument_views_and_malformed_inputs_precede_method_getters") {
  Project project;
  project.write(
      "let reads=0;export class Counter{constructor(n){this.n=n;}get next(){++reads;return "
      "function(value=0){return this.n+value;};}}export function getters(){return reads;}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  const auto entry = module(*runtime);
  const std::string constructor = "[40]trailing buffer";
  const auto object = instance(*runtime, entry, std::string_view{constructor}.substr(0, 4));
  const std::string arguments = "[2]trailing buffer";
  CHECK(call(*runtime, object, "next", std::string_view{arguments}.substr(0, 3)) == "42");
  std::string count = "[0";

  for (int index = 1; index < 65; ++index) {
    count += ",0";
  }

  count += "]";

  for (const auto& text :
       std::vector<std::string>{"", "{", "{}", "null", "[1]junk", count, std::string{"[]\0x", 4},
                                std::string{"\xc0\x80"}}) {
    CHECK_FALSE(runtime->callMethod(object, "next", text));
    CHECK_FALSE(runtime->createInstance(entry, "Counter", text));
  }

  CHECK(runtime->call(entry, "getters").value().value() == "1");
  CHECK_FALSE(runtime->callMethod(object, ""));
  CHECK_FALSE(runtime->hasMethod(object, std::string{"next\0x", 6}));
  CHECK_FALSE(runtime->createInstance(entry, std::string(257, 'x')));
}

TEST_CASE("instance_optional_hook_lookup_drains_getter_jobs_and_rejects_nonfunctions") {
  Project project;
  project.write(
      "export class Counter{value=42;nil=null;get next(){Promise.resolve().then(()=>this.value++);"
      "return function(){return this.value;};}get "
      "missing(){Promise.resolve().then(()=>this.value++);"
      "return undefined;}get bad(){Promise.resolve().then(()=>this.value++);return 1;}"
      "read(){return this.value;}}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  const auto object = instance(*runtime, module(*runtime));
  CHECK_FALSE(runtime->hasMethod(object, "absent").value());
  CHECK_FALSE(runtime->hasMethod(object, "missing").value());
  CHECK(call(*runtime, object, "read") == "43");
  CHECK(runtime->hasMethod(object, "next").value());
  CHECK(call(*runtime, object, "read") == "44");
  CHECK_FALSE(runtime->hasMethod(object, "value"));
  CHECK_FALSE(runtime->hasMethod(object, "nil"));
  CHECK_FALSE(runtime->callMethod(object, "bad"));
  CHECK(call(*runtime, object, "read") == "45");
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE("instance_clean_method_exceptions_and_rejected_promises_retain_state_and_context") {
  Project project;
  project.write(
      "export class Counter{value=0;next(){return ++this.value;}fail(){++this.value;throw new "
      "Error('失败');}"
      "async reject(){++this.value;throw new Error('async failure');}empty(){}nil(){return "
      "null;}}");
  auto runtime = Runtime::create(project.files());
  REQUIRE(runtime);
  const auto object = instance(*runtime, module(*runtime));
  auto failed = runtime->callMethod(object, "fail");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("main.js::Counter[1].fail") != std::string::npos);
  CHECK(failed.error().message.find("失败") != std::string::npos);
  CHECK_FALSE(runtime->callMethod(object, "reject"));
  CHECK(call(*runtime, object) == "3");
  CHECK_FALSE(call(*runtime, object, "empty"));
  CHECK(call(*runtime, object, "nil") == "null");
  CHECK_FALSE(runtime->stats()->faulted);
}

TEST_CASE(
    "instance_scope_move_thread_checks_and_foreign_quiescent_destruction_preserve_ownership") {
  Project project;
  project.write("export class Counter{constructor(){this.cycle=this;}next(){return 42;}}");
  const auto before = Runtime::backendMemory();

  {
    auto runtime = Runtime::create(project.files());
    auto other = Runtime::create(project.files());
    REQUIRE(runtime);
    REQUIRE(other);
    const auto entry = module(*runtime);
    const auto object = instance(*runtime, entry);
    CHECK_FALSE(other->callMethod(object, "next"));
    CHECK_FALSE(other->hasMethod(object, "next"));
    CHECK_FALSE(other->releaseInstance(object));
    CHECK_FALSE(other->createInstance(entry, "Counter"));
    auto moved = std::move(*runtime);
    CHECK_FALSE(runtime->releaseInstance(object));
    CHECK(call(moved, object) == "42");
    bool rejected = false;
    std::thread worker([&] {
      rejected = !moved.callMethod(object, "next") && !moved.hasMethod(object, "next")
                 && !moved.releaseInstance(object) && !moved.createInstance(entry, "Counter");
    });
    worker.join();
    CHECK(rejected);
    std::thread destroyer([owned = std::move(moved)] {});
    destroyer.join();
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("instance_constructor_and_getter_deadlines_fault_realm_without_publishing_handles") {
  Project project;
  RuntimeConfig config;
  config.milliseconds = 10;
  project.write("export class Counter{constructor(){while(true){}}}");
  auto constructor = Runtime::create(project.files(), config);
  REQUIRE(constructor);
  CHECK_FALSE(constructor->createInstance(module(*constructor), "Counter"));
  CHECK(constructor->stats()->faulted);
  CHECK(constructor->stats()->instances == 0);
  project.write("export class Counter{get next(){while(true){}}}");
  auto getter = Runtime::create(project.files(), config);
  REQUIRE(getter);
  auto object = instance(*getter, module(*getter));
  CHECK_FALSE(getter->hasMethod(object, "next"));
  CHECK(getter->stats()->faulted);
  REQUIRE(getter->releaseInstance(object));
  CHECK(getter->stats()->instances == 0);
}

TEST_CASE("instance_constructor_jobs_and_method_serialization_share_existing_resource_limits") {
  Project project;
  RuntimeConfig config;
  config.maxJobs = 2;
  project.write(
      "export class Counter{constructor(){const "
      "again=()=>Promise.resolve().then(again);again();}}");
  auto jobs = Runtime::create(project.files(), config);
  REQUIRE(jobs);
  CHECK_FALSE(jobs->createInstance(module(*jobs), "Counter"));
  CHECK(jobs->stats()->faulted);
  CHECK(jobs->stats()->instances == 0);
  project.write("export class Counter{next(){return {value:NaN};}}");
  auto json = Runtime::create(project.files());
  REQUIRE(json);
  const auto object = instance(*json, module(*json));
  CHECK_FALSE(json->callMethod(object, "next"));
  CHECK_FALSE(json->stats()->faulted);
}

TEST_CASE("instance_failed_constructor_allocation_and_repeated_cycles_release_every_vm_payload") {
  Project project;
  const auto before = Runtime::backendMemory();
  project.write("export class Counter{constructor(){this.bytes=new Uint8Array(16777216);}}");

  {
    RuntimeConfig config;
    config.memoryBytes = 1024 * 1024;
    auto runtime = Runtime::create(project.files(), config);
    REQUIRE(runtime);
    CHECK_FALSE(runtime->createInstance(module(*runtime), "Counter"));
    CHECK(runtime->stats()->faulted);
    CHECK(runtime->stats()->instances == 0);
  }

  project.write("export class Counter{constructor(){this.cycle=this;}next(){return 42;}}");

  for (int index = 0; index < 12; ++index) {
    auto runtime = Runtime::create(project.files());
    REQUIRE(runtime);
    const auto object = instance(*runtime, module(*runtime));
    CHECK(call(*runtime, object) == "42");
    REQUIRE(runtime->releaseInstance(object));
    CHECK(runtime->stats()->instances == 0);
  }

  CHECK(Runtime::backendMemory().bytes == before.bytes);
  CHECK(Runtime::backendMemory().allocations == before.allocations);
}

TEST_CASE("instance_host_callbacks_cannot_release_or_reenter_the_active_object") {
  Project project;
  project.write(
      "import {invoke} from 'dot:host';export class Counter{next(){return invoke('reenter',[]);}}");
  Runtime* active = nullptr;
  InstanceId object{};
  HostBindings bindings{
      {"reenter", [&](std::string_view) -> Dot::Core::Result<std::optional<std::string>> {
         const bool rejected = !active->callMethod(object, "next")
                               && !active->hasMethod(object, "next")
                               && !active->releaseInstance(object);
         return std::optional<std::string>{rejected ? "true" : "false"};
       }}};
  auto runtime = Runtime::create(project.files(), {}, bindings);
  REQUIRE(runtime);
  active = &*runtime;
  object = instance(*runtime, module(*runtime));
  CHECK(call(*runtime, object) == "true");
  CHECK(runtime->stats()->instances == 1);
}
