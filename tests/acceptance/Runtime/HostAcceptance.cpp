#include <cmath>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <string>
#include <utility>
#include <vector>

import Dot.Script.Compiler;
import Dot.Script.SceneHost;
import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.Core.Process;
import Dot.World.Scene;
import Dot.Graphics.Renderer;
import Dot.Graphics.SceneRenderer;

namespace Dot::Runtime {
  auto testScriptHost(const std::filesystem::path& executable, const std::filesystem::path& root,
                      bool render) -> int {
    auto files = Core::FileSystem::open(root);
    auto compiler = Script::TypeScriptCompiler::create(executable);
    auto created = World::Scene::create("Native Host Scene");

    if (!files || !compiler || !created || !files->createDirectories("script-host-acceptance")) {
      std::println(stderr, "Script host acceptance initialization failed");
      return 1;
    }

    auto scene = std::make_shared<World::Scene>(std::move(*created));
    auto camera = scene->createEntity("Camera");
    auto cube = scene->createEntity("Cube");

    if (!camera || !cube) {
      std::println(stderr, "Script host acceptance native entity creation failed");
      return 1;
    }

    World::Camera lens;
    lens.primary = true;
    World::MeshRenderer mesh;
    mesh.tint = {0.2F, 0.7F, 1, 1};

    if (!scene->setTransform(*camera, {{0, 0, 4}}) || !scene->setCamera(*camera, lens)
        || !scene->setMeshRenderer(*cube, mesh)) {
      std::println(stderr, "Script host acceptance native components failed");
      return 1;
    }

    auto source = files->writeTextAtomic(
        "script-host-acceptance/main.ts",
        "import {Entity,scene} from 'dot:engine';"
        "import type {EntityId,HostError} from 'dot:engine';let self:Entity;"
        "export function init(id:EntityId):void{self=new Entity(id);self.name='Script Cube';"
        "scene.rename('Typed Host Scene');const child=scene.create('Child');child.parent=self;"
        "if(child.parent?.id.value!==self.id.value)throw Error('parent identity');child.destroy();}"
        "export function tick(dt:number):number{const t=self.transform;t.position.x+=dt;"
        "self.transform=t;return self.worldMatrix()[12]!;}"
        "export function bad():string{const t=self.transform;t.scale.x=0;try{self.transform=t;}"
        "catch(e){return (e as HostError).code;}return 'unexpected';}"
        "export function count():number{return scene.entities().length;}");

    if (!source) {
      std::println(stderr, "{}", source.error().message);
      return 1;
    }

    const auto memory = Script::Runtime::backendMemory();
    const auto generations = Script::compilerGenerations();
    std::filesystem::path output;

    {
      auto program = compiler->compileProject(*files, "script-host-acceptance");
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

      const auto id
          = std::format("[{{\"scene\":\"{}\",\"value\":\"{}\"}}]", cube->scene, cube->value);
      auto initialized = runtime->call(*module, "init", id);
      auto rejected = runtime->call(*module, "bad");
      auto count = runtime->call(*module, "count");

      if (!initialized || !rejected || *rejected != "\"InvalidArgument\"" || !count || *count != "2"
          || scene->name().value() != "Typed Host Scene"
          || scene->entity(*cube)->name != "Script Cube") {
        std::println(stderr, "Script host acceptance SDK/entity/hierarchy/error behavior failed");
        return 1;
      }

      std::optional<Graphics::Renderer> renderer;
      std::optional<Graphics::SceneRenderer> graphics;
      std::optional<Graphics::Image> before;

      if (render) {
        auto device = Graphics::Renderer::create();

        if (!device) {
          std::println(stderr, "{}", device.error().message);
          return 1;
        }

        renderer.emplace(std::move(*device));
        auto service = Graphics::SceneRenderer::create(*renderer);

        if (!service) {
          std::println(stderr, "{}", service.error().message);
          return 1;
        }

        graphics.emplace(std::move(*service));
        auto image = graphics->render(*scene, 64, 64);

        if (!image) {
          std::println(stderr, "{}", image.error().message);
          return 1;
        }

        before.emplace(std::move(*image));
      }

      for (int frame = 0; frame < 60; ++frame) {
        auto tick = runtime->call(*module, "tick", "[0.025]");

        if (!tick || !tick->has_value()) {
          std::println(stderr, "Script host acceptance typed tick failed");
          return 1;
        }
      }

      auto actual = scene->entity(*cube);

      if (!actual || std::abs(actual->transform.position.x - 1.5F) > 0.00001F
          || actual->transform.scale.x != 1 || !actual->mesh || runtime->stats()->faulted
          || runtime->stats()->modules != 3) {
        std::println(stderr, "Script host acceptance native transform/state retention failed");
        return 1;
      }

      if (render) {
        auto after = graphics->render(*scene, 64, 64);
        constexpr std::size_t center = (32 * 64 + 32) * 4;

        if (!after || before->pixels[center + 2] < 200 || after->pixels[center + 2] > 20
            || before->pixels == after->pixels || graphics->stats()->meshes != 1) {
          std::println(stderr, "Script host acceptance actual GPU pixel change failed");
          return 1;
        }
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || std::filesystem::exists(output)
        || after.bytes != memory.bytes || after.allocations != memory.allocations
        || Core::processResources().children != 0 || Core::processResources().handles != 0) {
      std::println(stderr, "Script host acceptance compiler/process/VM cleanup failed");
      return 1;
    }

    std::println(
        "Script host acceptance passed: actual TypeScript SDK, ESM, scene/entity/hierarchy, "
        "60 native transform ticks, contextual errors, cleanup{}",
        render ? ", changed GPU pixels" : "");
    return 0;
  }
}  // namespace Dot::Runtime
