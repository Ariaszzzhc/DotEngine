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
import Dot.Physics.Body;

namespace {
  using namespace Dot::Script;

  struct Project {
    std::filesystem::path root;

    Project() {
      std::random_device random;
      root = std::filesystem::temp_directory_path()
             / ("dot-sdk-test-" + std::to_string(random()) + "-" + std::to_string(random()));
      REQUIRE(std::filesystem::create_directory(root));
      auto project = files();
      REQUIRE(project.createDirectories("scripts"));
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
      "import {scene} from 'dot:engine';export function run():number[]{"
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
      "import {scene,Entity} from 'dot:engine';import type {HostError} from 'dot:engine';"
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
       {"import {Entity} from 'dot:engine';const e=new Entity({scene:1,value:2});",
        "import {scene} from 'dot:engine';const "
        "e=scene.create();e.transform={position:{x:0,y:0,z:0}};",
        "import {scene} from 'dot:engine';const e=scene.create();e.id.value='42';"}) {
    project.write(source);
    auto result = compiler.compileProject(project.files());
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}

TEST_CASE("script_sdk_native_callback_failure_keeps_context_and_prior_successful_mutations") {
  Project project;
  project.write(
      "import {scene} from 'dot:engine';export function run(){const e=scene.create('Kept');"
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

TEST_CASE("component_sdk_real_typescript_defaults_match_native_and_nested_factories_are_owned") {
  Project project;
  project.write(R"ts(
import {components, scene} from 'dot:engine';

export function run(): boolean {
  const e = scene.create('Configured');
  const a = components.mesh();
  const b = components.mesh();
  a.tint.x = 0;

  if (b.tint.x !== 1) {
    throw Error('shared default tint');
  }

  e.camera = components.camera();
  e.mesh = b;
  e.rigidBody = components.rigidBody();
  e.audioSource = components.audioSource('audio/missing.wav');
  e.audioListener = components.audioListener();
  e.script = components.script('missing.ts');
  const state = e.info();

  if (state.camera === null || state.mesh === null || state.rigidBody === null
      || state.audioSource === null || state.audioListener === null || state.script === null) {
    throw Error('missing snapshot');
  }

  const mesh = e.mesh!;
  mesh.tint.x = 0;
  state.audioSource.voice.volume = 0;

  if (e.mesh!.tint.x !== 1 || e.audioSource!.voice.volume !== 1) {
    throw Error('live instead of owned snapshot');
  }

  return e.exists && scene.primaryCamera === null && scene.primaryListener === null;
}
)ts");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto result = vm.call(module(vm), "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->value() == "true");
  auto state = scene->entities()->front();
  REQUIRE((state.camera && state.mesh && state.rigidBody && state.audioSource && state.audioListener
           && state.script));
  Dot::World::Camera camera;
  CHECK(state.camera->projection == camera.projection);
  CHECK(state.camera->verticalFovDegrees == camera.verticalFovDegrees);
  CHECK(state.camera->orthographicHeight == camera.orthographicHeight);
  CHECK(state.camera->nearPlane == camera.nearPlane);
  CHECK(state.camera->farPlane == camera.farPlane);
  CHECK(state.camera->primary == camera.primary);
  Dot::World::MeshRenderer mesh;
  CHECK(state.mesh->kind == mesh.kind);
  CHECK(state.mesh->asset == mesh.asset);
  CHECK(state.mesh->tint == mesh.tint);
  CHECK(state.mesh->visible == mesh.visible);
  CHECK(*state.rigidBody == Dot::World::RigidBody{});
  Dot::World::AudioSource audio;
  audio.asset = "audio/missing.wav";
  CHECK(*state.audioSource == audio);
  CHECK(*state.audioListener == Dot::World::AudioListener{});
  CHECK(*state.script == Dot::World::ScriptComponent{"missing.ts"});
  CHECK(vm.stats()->modules == 3);
  CHECK(vm.stats()->instances == 0);
}

TEST_CASE("component_sdk_changes_and_primary_queries_use_native_state_and_removal") {
  Project project;
  project.write(R"ts(
import {components, scene} from 'dot:engine';

export function run(): string[] {
  const camera = scene.create('Camera');
  const actor = scene.create('Actor');
  camera.camera
      = components.camera({primary: true, projection: 'orthographic', orthographicHeight: 4});
  actor.mesh = components.mesh({kind: 'quad', tint: {x: 0, y: 1, z: 0, w: 1}});
  actor.rigidBody = components.rigidBody({motion: 'kinematic', velocity: {x: 2, y: 0, z: 0}});
  actor.audioSource = components.audioSource('audio/missing.flac');
  const source = actor.audioSource!;
  source.voice.looping = true;
  source.voice.pitch = 2;
  source.spatial.enabled = true;
  source.spatial.attenuation = 'exponential';
  actor.audioSource = source;
  actor.audioListener = components.audioListener(true);
  actor.script = components.script('actor.mts', 'Actor');
  const script = actor.script!;
  script.properties = {name: '中文', nested: [true, null, {speed: 2}]};
  script.enabled = false;
  actor.script = script;

  if (scene.primaryCamera?.id.value !== camera.id.value
      || scene.primaryListener?.id.value !== actor.id.value) {
    throw Error('primary mismatch');
  }

  camera.camera = null;
  actor.audioListener = null;

  if (scene.primaryCamera !== null || scene.primaryListener !== null) {
    throw Error('removed primary');
  }

  const child = scene.create('Child');
  child.parent = actor;
  actor.destroy();
  return [String(actor.exists), String(child.exists), String(camera.exists)];
}
)ts");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto result = vm.call(module(vm), "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->value() == "[\"false\",\"false\",\"true\"]");
  REQUIRE(scene->entities()->size() == 1);
  CHECK_FALSE(scene->entities()->front().camera);
}

TEST_CASE("component_sdk_invalid_assignment_recovers_without_changing_previous_description") {
  Project project;
  project.write(R"ts(
import {components, Entity, scene} from 'dot:engine';
import type {HostError} from 'dot:engine';

export function run(): string[] {
  const e = scene.create('Actor');
  e.rigidBody = components.rigidBody();
  const body = e.rigidBody!;
  body.mass = 0;
  const codes: string[] = [];

  try {
    e.rigidBody = body;
  } catch (error) {
    codes.push((error as HostError).code);
  }

  if (e.rigidBody!.mass !== 1) {
    throw Error('invalid assignment changed state');
  }

  body.mass = 2;
  e.rigidBody = body;

  if (e.rigidBody!.mass !== 2) {
    throw Error('recovery failed');
  }

  e.camera = components.camera({primary: true});
  const other = scene.create();

  try {
    other.camera = components.camera({primary: true});
  } catch (error) {
    codes.push((error as HostError).code);
  }

  if (other.camera !== null) {
    throw Error('duplicate primary changed state');
  }

  const foreign = new Entity({scene: '18446744073709551614', value: '1'});

  try {
    void foreign.exists;
  } catch (error) {
    codes.push((error as HostError).code);
  }

  e.destroy();

  try {
    void e.mesh;
  } catch (error) {
    codes.push((error as HostError).code);
  }

  return codes;
}
)ts");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto result = vm.call(module(vm), "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->value()
        == "[\"InvalidArgument\",\"InvalidArgument\",\"InvalidArgument\",\"NotFound\"]");
  CHECK_FALSE(vm.stats()->faulted);
}

TEST_CASE("component_sdk_typecheck_rejects_incomplete_unknown_and_non_json_descriptions") {
  Project project;
  auto compiler = tool();

  for (const auto* source :
       {"import {scene} from 'dot:engine';scene.create().camera={primary:true};",
        "import {components} from 'dot:engine';components.mesh({kind:'triangle'});",
        "import {components} from 'dot:engine';components.rigidBody({motion:'auto'});",
        "import {components} from 'dot:engine';components.audioSource();",
        "import {components} from 'dot:engine';components.audioListener(1);",
        "import {components} from "
        "'dot:engine';components.script('actor.ts').properties={bad:()=>1};",
        "import {scene} from 'dot:engine';scene.primaryCamera=scene.create();"}) {
    project.write(source);
    auto result = compiler.compileProject(project.files());
    REQUIRE_FALSE(result);
    CHECK(result.error().message.find("TS") != std::string::npos);
  }
}

TEST_CASE("script_sdk.hierarchy_group_reparent_and_ungroup_share_native_world_preservation") {
  Project project;
  project.write(R"ts(
import {scene} from 'dot:engine';
export function run(): number[] {
  const p = scene.create('Parent');
  const a = scene.create('A');
  const b = scene.create('B');
  const pose = p.transform;
  pose.position.x = 10;
  p.transform = pose;
  const position = a.transform;
  position.position.x = 2;
  a.transform = position;
  a.reparent(p, 'world');
  const before = a.worldMatrix()[12]!;
  const group = scene.group([a, b], 'Assembly');
  const same = a.worldMatrix()[12] === before;
  const children = scene.ungroup([group]);
  return [before, a.worldMatrix()[12]!, same ? 1 : 0, group.exists ? 1 : 0,
          children.length, scene.info().entities];
}
)ts");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  auto result = vm.call(module(vm), "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(result->value() == "[2,2,1,0,2,3]");
}

TEST_CASE("script_sdk.hierarchy_rejected_arguments_leave_native_entities_unchanged") {
  Project project;
  project.write(R"ts(
import {scene, type HostError} from 'dot:engine';
export function run(): number {
  const a = scene.create('A');
  const before = JSON.stringify(a.info());
  let rejected = 0;

  for (const action of [() => scene.group([]), () => scene.group([a, a]),
      () => a.reparent(null, 'invalid' as any), () => scene.ungroup([a, a])]) {
    try {
      action();
    } catch (error) {
      if ((error as HostError).code === 'InvalidArgument') {
        ++rejected;
      }
    }
  }

  if (JSON.stringify(a.info()) !== before || scene.info().entities !== 1) {
    throw new Error('Partial mutation');
  }

  return rejected;
}
)ts");
  auto program = compile(project);
  auto scene = nativeScene();
  auto vm = runtime(program, scene);
  CHECK(vm.call(module(vm), "run").value().value() == "4");
  project.write("import {scene} from 'dot:engine';scene.group([1]);");
  CHECK_FALSE(tool().compileProject(project.files()));
}

TEST_CASE("script_sdk.named_lookup_finds_late_duplicates_with_a_small_host_call_budget") {
  Project project;
  project.write(R"ts(
import {scene} from 'dot:engine';
export function run(): string[] {
  const matches = scene.findEntities('目标🙂');
  if (scene.findEntities('missing').length !== 0) throw Error('unexpected match');
  return matches.map(entity => entity.id.value + ':' + entity.name);
}
)ts");
  auto program = compile(project);
  auto scene = nativeScene();

  for (int index = 0; index < 1000; ++index) {
    REQUIRE(scene->createEntity("Unrelated"));
  }

  auto first = scene->createEntity("目标🙂").value();
  auto second = scene->createEntity("目标🙂").value();
  RuntimeConfig config;
  config.maxHostCalls = 16;
  auto services = sceneBindings(scene).value();
  auto vm = Runtime::create(program.files().value(), config, services).value();
  auto result = vm.call(module(vm), "run");
  REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
  CHECK(nlohmann::json::parse(result->value())
        == nlohmann::json::array(
            {std::to_string(first.value) + ":目标🙂", std::to_string(second.value) + ":目标🙂"}));
  CHECK_FALSE(vm.stats()->faulted);
}
