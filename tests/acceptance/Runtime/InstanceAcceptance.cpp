#include <cmath>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <string>
#include <utility>

import Dot.Script.Compiler;
import Dot.Script.SceneHost;
import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.Core.Process;
import Dot.World.Scene;

namespace Dot::Runtime {
  auto testScriptInstances(const std::filesystem::path& executable,
                           const std::filesystem::path& sdkRoot, const std::filesystem::path& root)
      -> int {
    auto files = Core::FileSystem::open(root);
    auto sdk = Core::FileSystem::open(sdkRoot);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Instance Acceptance");

    if (!files || !sdk || !compiler || !created
        || !files->createDirectories("script-instance-acceptance/dot")) {
      std::println(stderr, "Script instance acceptance setup failed");
      return 1;
    }

    auto scene = std::make_shared<World::Scene>(std::move(*created));
    auto first = scene->createEntity("First");
    auto second = scene->createEntity("Second");

    if (!first || !second) {
      std::println(stderr, "Script instance acceptance native entities failed");
      return 1;
    }

    for (const auto* path : {"dot/engine.ts", "dot/host.d.ts"}) {
      auto source = sdk->readText(path);

      if (!source
          || !files->writeTextAtomic(std::filesystem::path{"script-instance-acceptance"} / path,
                                     *source)) {
        std::println(stderr, "Script instance acceptance SDK copy failed");
        return 1;
      }
    }

    auto source = files->writeTextAtomic(
        "script-instance-acceptance/main.ts",
        "import {Entity} from './dot/engine.js';import type {EntityId} from './dot/engine.js';"
        "class Base{readonly self:Entity;constructor(id:EntityId){this.self=new Entity(id);}"
        "onStart():void{this.self.name+=' started';}onStop():void{this.self.name+=' stopped';}}"
        "export class Actor extends Base{#ticks=0;readonly speed:number;"
        "constructor(id:EntityId,speed:number){super(id);this.speed=speed;}"
        "async onUpdate(seconds:number):Promise<void>{await Promise.resolve();++this.#ticks;"
        "const "
        "transform=this.self.transform;transform.position.x+=this.speed*seconds;this.self."
        "transform=transform;}"
        "ticks():number{return this.#ticks;}fail():void{throw new Error('deliberate instance "
        "error');}}");

    if (!source) {
      std::println(stderr, "{}", source.error().message);
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();
    std::filesystem::path output;

    {
      auto program = compiler->compileProject(*files, "script-instance-acceptance");
      auto bindings = Script::sceneBindings(scene);

      if (!program || !bindings) {
        std::println(stderr, "{}", program ? bindings.error().message : program.error().message);
        return 1;
      }

      auto generated = program->files();
      output = generated->root().parent_path();
      auto runtime = Script::Runtime::create(*generated, {}, *bindings);

      if (!runtime) {
        std::println(stderr, "{}", runtime.error().message);
        return 1;
      }

      auto module = runtime->loadModule("main.js");

      if (!module) {
        std::println(stderr, "{}", module.error().message);
        return 1;
      }

      const auto arguments = [](World::EntityId id, int speed) -> std::string {
        return std::format("[{{\"scene\":\"{}\",\"value\":\"{}\"}},{}]", id.scene, id.value, speed);
      };
      auto a = runtime->createInstance(*module, "Actor", arguments(*first, 1));
      auto b = runtime->createInstance(*module, "Actor", arguments(*second, -2));

      if (!a || !b || !runtime->hasMethod(*a, "onStart").value()
          || runtime->hasMethod(*a, "onFixedUpdate").value() || !runtime->callMethod(*a, "onStart")
          || !runtime->callMethod(*b, "onStart")) {
        std::println(stderr, "Script instance acceptance construction/hooks failed");
        return 1;
      }

      for (int frame = 0; frame < 60; ++frame) {
        if (!runtime->callMethod(*a, "onUpdate", "[0.016666666666666666]")
            || !runtime->callMethod(*b, "onUpdate", "[0.016666666666666666]")) {
          std::println(stderr, "Script instance acceptance async updates failed");
          return 1;
        }
      }

      auto failure = runtime->callMethod(*a, "fail");
      auto ticksA = runtime->callMethod(*a, "ticks");
      auto ticksB = runtime->callMethod(*b, "ticks");
      auto stateA = scene->entity(*first);
      auto stateB = scene->entity(*second);

      if (failure || failure.error().message.find("Actor[1].fail") == std::string::npos || !ticksA
          || *ticksA != "60" || !ticksB || *ticksB != "60" || !stateA || !stateB
          || std::abs(stateA->transform.position.x - 1) > 0.00001F
          || std::abs(stateB->transform.position.x + 2) > 0.00001F
          || runtime->stats()->instances != 2 || runtime->stats()->faulted) {
        std::println(stderr, "Script instance acceptance state/context verification failed");
        return 1;
      }

      if (!runtime->callMethod(*a, "onStop") || !runtime->callMethod(*b, "onStop")
          || !runtime->releaseInstance(*a) || !runtime->releaseInstance(*b)
          || runtime->stats()->instances != 0 || runtime->callMethod(*a, "ticks")
          || scene->entity(*first)->name != "First started stopped"
          || scene->entity(*second)->name != "Second started stopped") {
        std::println(stderr, "Script instance acceptance stop/release verification failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || std::filesystem::exists(output)
        || after.bytes != memory.bytes || after.allocations != memory.allocations
        || Core::processResources().handles != 0 || Core::processResources().children != 0) {
      std::println(stderr, "Script instance acceptance resource cleanup failed");
      return 1;
    }

    std::println(
        "Script instance acceptance passed: real TypeScript classes/private fields, "
        "two native entities, independent async updates, inherited hooks, errors, release");
    return 0;
  }
}  // namespace Dot::Runtime
