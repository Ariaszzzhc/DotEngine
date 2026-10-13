#include <expected>
#include <filesystem>
#include <print>
#include <string>

import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.Core.Process;

namespace Dot::Runtime {
  auto testCompiler(const std::filesystem::path& executable, const std::filesystem::path& root)
      -> int {
    auto files = Core::FileSystem::open(root);
    auto compiler = Script::TypeScriptCompiler::create(executable);

    if (!files || !compiler || !files->createDirectories("typescript-acceptance")) {
      std::println(stderr, "TypeScript acceptance setup failed: {}",
                   compiler ? "project directory" : compiler.error().message);
      return 1;
    }

    auto model
        = files->writeTextAtomic("typescript-acceptance/model.ts",
                                 "let ticks=0;export function step():number{return ++ticks;}");
    auto main = files->writeTextAtomic(
        "typescript-acceptance/main.ts",
        "import {keys} from 'dot:engine';import {step} from './model';"
        "if(!keys.includes('A'))throw new Error('Engine namespace failed');"
        "export async function sum(a:number,b:number):Promise<{sum:number,ticks:number}>"
        "{await Promise.resolve();return {sum:a+b,ticks:step()};}");

    if (!model || !main) {
      std::println(stderr, "TypeScript acceptance source write failed");
      return 1;
    }

    const auto generations = Script::compilerGenerations();
    const auto memory = Script::Runtime::backendMemory();
    std::filesystem::path output;

    {
      auto program = compiler->compileProject(*files, "typescript-acceptance");

      if (!program) {
        std::println(stderr, "{}", program.error().message);
        return 1;
      }

      auto generated = program->files();

      if (!generated || program->modules()->size() != 2 || !generated->readText("main.js.map")) {
        std::println(stderr, "TypeScript acceptance output/source map failed");
        return 1;
      }

      if (program->stats()->sourceFiles != 2 || std::filesystem::exists(generated->root() / "dot")
          || std::filesystem::exists(files->root() / "typescript-acceptance/dot")) {
        std::println(stderr, "TypeScript namespace unexpectedly required/copied a project SDK");
        return 1;
      }

      output = generated->root().parent_path();
      auto runtime = Script::Runtime::create(*generated);

      if (!runtime) {
        std::println(stderr, "{}", runtime.error().message);
        return 1;
      }

      auto module = runtime->loadModule("main.js");

      if (!module) {
        std::println(stderr, "{}", module.error().message);
        return 1;
      }

      auto first = runtime->call(*module, "sum", "[20,22]");
      auto broken = files->writeTextAtomic("typescript-acceptance/main.ts",
                                           "export const n:number='deliberate type error';");
      auto failed = compiler->compileProject(*files, "typescript-acceptance");
      auto second = runtime->call(*module, "sum", "[21,22]");

      if (!first || *first != "{\"sum\":42,\"ticks\":1}" || !broken || failed
          || failed.error().message.find("TS2322") == std::string::npos || !second
          || *second != "{\"sum\":43,\"ticks\":2}"
          || Script::compilerGenerations() != generations + 1) {
        std::println(stderr,
                     "TypeScript acceptance type failure/previous runtime retention failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (Script::compilerGenerations() != generations || std::filesystem::exists(output)
        || memory.bytes != after.bytes || memory.allocations != after.allocations
        || Core::processResources().children != 0 || Core::processResources().handles != 0) {
      std::println(stderr, "TypeScript acceptance generation/process/VM cleanup failed");
      return 1;
    }

    std::println(
        "TypeScript acceptance passed: native compiler, strict ESM, source maps, "
        "type diagnostics, previous runtime retention, cleanup");
    return 0;
  }
}  // namespace Dot::Runtime
