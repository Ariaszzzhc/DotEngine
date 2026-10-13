#include <expected>
#include <filesystem>
#include <print>
#include <string>

import Dot.Script.Runtime;
import Dot.Core.Files;

namespace Dot::Runtime {
  auto testScript(const std::filesystem::path& root) -> int {
    auto files = Core::FileSystem::open(root);

    if (!files || !files->createDirectories("scripts")) {
      std::println(stderr, "Script acceptance project setup failed");
      return 1;
    }

    auto model = files->writeTextAtomic("scripts/model.js",
                                        "let ticks=0;export function step(){return ++ticks;}");
    auto main = files->writeTextAtomic(
        "scripts/main.mjs",
        "import {step} from './model';"
        "export async function sum(a,b){await Promise.resolve();return {sum:a+b,ticks:step()};}"
        "export function fail(){throw new Error('expected failure');}"
        "export function loop(){while(true){}}");

    if (!model || !main) {
      std::println(stderr, "Script acceptance source creation failed");
      return 1;
    }

    const auto before = Script::Runtime::backendMemory();

    {
      auto runtime = Script::Runtime::create(*files);

      if (!runtime) {
        std::println(stderr, "{}", runtime.error().message);
        return 1;
      }

      auto module = runtime->loadModule("scripts/main.mjs");

      if (!module) {
        std::println(stderr, "{}", module.error().message);
        return 1;
      }

      auto first = runtime->call(*module, "sum", "[20,22]");
      auto failure = runtime->call(*module, "fail");
      auto second = runtime->call(*module, "sum", "[21,22]");

      if (!first || *first != "{\"sum\":42,\"ticks\":1}" || failure || !second
          || *second != "{\"sum\":43,\"ticks\":2}" || runtime->stats()->faulted
          || runtime->stats()->modules != 2) {
        std::println(stderr, "Script acceptance ESM/JSON/jobs/recovery failed");
        return 1;
      }

      auto interrupted = runtime->call(*module, "loop");

      if (interrupted || !runtime->stats()->faulted || runtime->call(*module, "sum")) {
        std::println(stderr, "Script acceptance execution limit failed");
        return 1;
      }
    }

    const auto after = Script::Runtime::backendMemory();

    if (after.bytes != before.bytes || after.allocations != before.allocations) {
      std::println(stderr, "Script acceptance resource cleanup failed");
      return 1;
    }

    std::println("Script acceptance passed: real ESM, JSON, jobs, exceptions, limits, cleanup");
    return 0;
  }
}  // namespace Dot::Runtime
