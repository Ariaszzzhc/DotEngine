#include <doctest/doctest.h>

#include <expected>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>

import Dot.Script.Compiler;
import Dot.Script.SceneHost;
import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.World.Scene;

namespace {
  using namespace Dot::Script;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-sdk-test-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      auto sdk = Dot::Core::FileSystem::open(DOT_SCRIPT_SDK_DIRECTORY);
      REQUIRE(sdk);
      auto project = files();
      REQUIRE(project.createDirectories("scripts/dot"));

      for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
        auto source = sdk->readText(path);
        REQUIRE(source);
        REQUIRE(project.writeTextAtomic(std::filesystem::path{"scripts"} / path, *source));
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
      REQUIRE(files().writeTextAtomic("scripts/main.ts", code));
    }
  };

  auto tool() -> TypeScriptCompiler {
    auto result = TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER);
    REQUIRE(result);
    return *result;
  }

  auto compile(const Project& project) -> CompiledProgram {
    auto result = tool().compileProject(project.files());
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto nativeScene() -> std::shared_ptr<Dot::World::Scene> {
    auto result = Dot::World::Scene::create("SDK Scene");
    REQUIRE(result);
    return std::make_shared<Dot::World::Scene>(std::move(*result));
  }

  auto runtime(const CompiledProgram& program, const std::shared_ptr<Dot::World::Scene>& scene)
      -> Runtime {
    auto bindings = sceneBindings(scene);
    REQUIRE(bindings);
    auto result = Runtime::create(program.files().value(), {}, *bindings);
    REQUIRE(result);
    return std::move(*result);
  }

  auto module(Runtime& runtime) -> ModuleId {
    auto result = runtime.loadModule("main.js");
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }
}  // namespace

TEST_CASE("script_sdk.real_typescript_entities_transforms_and_hierarchy_edit_native_scene") {
  Project project;
  project.write(
      "import {scene} from './dot/engine.js';export function run():number[]{"
      "scene.rename('Script Created');const parent=scene.create('Parent');const "
      "child=scene.create('Child');"
      "const a=parent.transform;a.position.x=2;parent.transform=a;child.parent=parent;"
      "const b=child.transform;b.position.x=3;child.transform=b;child.name='Moved Child';"
      "const before=child.worldMatrix()[12]!;child.parent=null;const "
      "after=child.worldMatrix()[12]!;"
      "if(child.info().parent!==null||child.name!=='Moved Child')throw Error('snapshot mismatch');"
      "const count=scene.entities().length;child.destroy();return "
      "[before,after,count,scene.info().entities];}");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto result = vm.call(module(vm), "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->value() == "[5,3,2,1]");
  CHECK(scene->name().value() == "Script Created");
  auto entities = scene->entities();
  REQUIRE(entities);
  REQUIRE(entities->size() == 1);
  CHECK((*entities)[0].name == "Parent");
  CHECK((*entities)[0].transform.position.x == 2);
  CHECK(vm.stats()->modules == 3);
}

TEST_CASE("script_sdk_snapshot_edits_commit_only_on_assignment_and_native_failures_recover") {
  Project project;
  project.write(
      "import {scene,Entity} from './dot/engine.js';import type {HostError} from './dot/engine.js';"
      "let self:Entity;export function init(){self=scene.create('Actor');}"
      "export function preview():number{const t=self.transform;t.position.x=9;return "
      "self.transform.position.x;}"
      "export function run():string{const t=self.transform;t.scale.x=0;try{self.transform=t;}"
      "catch(e){return (e as HostError).code;}return 'unexpected';}"
      "export function tick(dt:number):number{const "
      "t=self.transform;t.position.x+=dt;self.transform=t;"
      "return self.worldMatrix()[12]!;}"
      "export function stale():string{self.destroy();try{return self.name;}catch(e){return (e as "
      "HostError).code;}}");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto entry = module(vm);
  REQUIRE(vm.call(entry, "init"));
  CHECK(vm.call(entry, "preview").value().value() == "0");
  CHECK(vm.call(entry, "run").value().value() == "\"InvalidArgument\"");
  CHECK_FALSE(vm.stats()->faulted);
  CHECK(vm.call(entry, "tick", "[0.5]").value().value() == "0.5");
  CHECK(vm.call(entry, "tick", "[0.5]").value().value() == "1");
  CHECK(scene->entities()->front().transform.position.x == 1);
  CHECK(vm.call(entry, "stale").value().value() == "\"NotFound\"");
  CHECK(scene->entityCount().value() == 0);
}

TEST_CASE("script_sdk_type_errors_prevent_invalid_ids_and_incomplete_transforms") {
  Project project;
  auto compiler = tool();

  for (const auto* source :
       {"import {Entity} from './dot/engine.js';const e=new Entity({scene:1,value:2});",
        "import {scene} from './dot/engine.js';const "
        "e=scene.create();e.transform={position:{x:0,y:0,z:0}};",
        "import {scene} from './dot/engine.js';const e=scene.create();e.id.value='42';"}) {
    project.write(source);
    auto result = compiler.compileProject(project.files());
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}

TEST_CASE("script_sdk_native_callback_failure_keeps_context_and_prior_successful_mutations") {
  Project project;
  project.write(
      "import {scene} from './dot/engine.js';export function run(){const e=scene.create('Kept');"
      "const t=e.transform;t.scale.y=0;e.transform=t;}export function count(){return "
      "scene.info().entities;}");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto entry = module(vm);
  auto failed = vm.call(entry, "run");
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("Host transform.set") != std::string::npos);
  CHECK_FALSE(vm.stats()->faulted);
  CHECK(vm.call(entry, "count").value().value() == "1");
  CHECK(scene->entities()->front().name == "Kept");
  CHECK(scene->entities()->front().transform.scale.y == 1);
}
