#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

import Dot.Script.Compiler;
import Dot.Script.Runtime;
import Dot.Core.Files;
import Dot.Core.Process;

namespace {
  using namespace Dot::Script;
  namespace Fs = std::filesystem;

  struct Project {
    Fs::path root;

    Project() {
      std::random_device random;

      for (int attempt = 0; attempt < 64; ++attempt) {
        auto candidate
            = Fs::temp_directory_path()
              / ("dot-compiler-test-" + std::to_string(random()) + "-" + std::to_string(random()));

        if (Fs::create_directory(candidate)) {
          root = std::move(candidate);
          return;
        }
      }

      throw std::runtime_error("Cannot create compiler test project");
    }

    ~Project() {
      std::error_code ignored;
      Fs::remove_all(root, ignored);
    }

    auto files() const -> Dot::Core::FileSystem {
      auto result = Dot::Core::FileSystem::open(root);
      REQUIRE(result);
      return *result;
    }

    auto write(std::string_view path, std::string_view text) const -> void {
      const Fs::path relative{std::u8string(path.begin(), path.end())};
      auto access = files();
      REQUIRE(access.createDirectories(relative.parent_path()));
      REQUIRE(access.writeTextAtomic(relative, text));
    }
  };

  auto compiler(const CompilerLimits& limits = {}, const Fs::path& tool = DOT_TYPESCRIPT_COMPILER)
      -> TypeScriptCompiler {
    auto result = TypeScriptCompiler::create(tool, limits);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return *result;
  }

  auto compile(const TypeScriptCompiler& tool, const Project& project,
               std::string_view directory = "scripts") -> CompiledProgram {
    auto result = tool.compileProject(project.files(), directory);
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    return std::move(*result);
  }

  auto run(const CompiledProgram& program, std::string_view entry = "main.js") -> std::string {
    auto files = program.files();
    REQUIRE(files);
    auto runtime = Runtime::create(*files);
    REQUIRE(runtime);
    auto module = runtime->loadModule(entry);
    REQUIRE_MESSAGE(module, (module ? "" : module.error().message));
    auto result = runtime->call(*module, "run");
    REQUIRE_MESSAGE(result, (result ? "" : result.error().message));
    REQUIRE(result->has_value());
    return **result;
  }
}  // namespace

TEST_CASE("compiler.real_typescript_strict_esm_relative_graph_runs_in_runtime") {
  Project project;
  project.write("scripts/model.ts", "export const n:number=40;");
  project.write("scripts/main.ts",
                "import {n} from './model.js';export async function run():Promise<number>"
                "{return n+await Promise.resolve(2);}");
  auto tool = compiler();
  auto program = compile(tool, project);
  CHECK(program.modules().value() == std::vector<std::string>{"main.js", "model.js"});
  CHECK(program.stats()->sourceFiles == 2);
  CHECK(program.stats()->sourceBytes > 0);
  CHECK(program.stats()->outputBytes > program.stats()->sourceBytes);
  CHECK(program.diagnostics()->empty());
  CHECK(run(program) == "42");
  CHECK_FALSE(Fs::exists(project.root / "scripts/main.js"));
}

TEST_CASE("compiler.source_maps_retain_original_typescript_and_relative_mapping") {
  Project project;
  const std::string source = "export function run():number { return 42; }\n";
  project.write("scripts/nested/main.ts", source);
  auto program = compile(compiler(), project);
  auto map = program.files()->readText("nested/main.js.map");
  REQUIRE(map);
  const auto parsed = nlohmann::json::parse(*map);
  CHECK(parsed.at("version") == 3);
  CHECK(parsed.at("sourcesContent") == nlohmann::json::array({source}));
  CHECK(parsed.at("sources") == nlohmann::json::array({"../../src/nested/main.ts"}));
  CHECK_FALSE(parsed.at("mappings").get<std::string>().empty());
  CHECK(run(program, "nested/main.js") == "42");
}

TEST_CASE("compiler_unicode_paths_mts_and_declarations_compile_without_node") {
  Project project;
  project.write("类型🙂/globals.d.mts", "declare const unusedGlobal:number;");
  project.write("类型🙂/中文.mts", "export const value:string='中文🙂';");
  project.write("类型🙂/main.mts",
                "import {value} from './中文.mjs';export function run():string{return value;}");
  auto program = compile(compiler(), project, "类型🙂");
  CHECK(program.modules().value() == std::vector<std::string>{"main.mjs", "中文.mjs"});
  CHECK(program.stats()->sourceFiles == 3);
  CHECK(run(program, "main.mjs") == "\"中文🙂\"");
}

TEST_CASE("compiler_type_failure_preserves_old_bundle_source_and_runtime") {
  Project project;
  project.write("scripts/main.ts", "let n=40;export function run():number{return ++n;}");
  auto tool = compiler();
  auto program = compile(tool, project);
  auto files = program.files().value();
  auto original = files.readText("main.js").value();
  auto runtime = Runtime::create(files);
  REQUIRE(runtime);
  auto module = runtime->loadModule("main.js");
  REQUIRE(module);
  CHECK(runtime->call(*module, "run").value().value() == "41");
  const auto before = compilerGenerations();
  project.write("scripts/main.ts", "export function run():number{return '错误🙂';}");
  auto failed = tool.compileProject(project.files());
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("TS2322") != std::string::npos);
  CHECK(failed.error().message.find("src/main.ts") != std::string::npos);
  CHECK(compilerGenerations() == before);
  CHECK(files.readText("main.js").value() == original);
  CHECK(runtime->call(*module, "run").value().value() == "42");
  CHECK(project.files().readText("scripts/main.ts")->find("错误🙂") != std::string::npos);
  project.write("scripts/main.ts", "export function run():number{return 99;}");
  auto candidate = compile(tool, project);
  CHECK(candidate.files()->root() != files.root());
  CHECK(run(candidate) == "99");
  CHECK(runtime->call(*module, "run").value().value() == "43");
}

TEST_CASE("compiler_bundle_copy_move_last_owner_and_foreign_destruction_cleanup") {
  Project project;
  project.write("scripts/main.ts", "export function run():number{return 42;}");
  const auto before = compilerGenerations();
  Fs::path generation;

  {
    auto program = compile(compiler(), project);
    generation = program.files()->root().parent_path();
    auto copy = program;
    auto moved = std::move(program);
    CHECK_FALSE(program.files());
    CHECK_FALSE(program.modules());
    CHECK_FALSE(program.diagnostics());
    CHECK_FALSE(program.stats());
    CHECK(compilerGenerations() == before + 1);
    CHECK(run(copy) == "42");
    std::optional<CompiledProgram> surviving{copy};
    surviving = std::move(moved);
    CHECK(run(*surviving) == "42");
  }

  CHECK(compilerGenerations() == before);
  CHECK_FALSE(Fs::exists(generation));
  auto program = compile(compiler(), project);
  generation = program.files()->root().parent_path();
  std::thread destroyer([owned = std::move(program)] {});
  destroyer.join();
  CHECK_FALSE(Fs::exists(generation));
  CHECK(compilerGenerations() == before);
}

TEST_CASE("compiler_snapshot_survives_authoring_removal_for_lazy_module_import") {
  Project project;
  project.write("scripts/model.ts", "export const n:number=42;");
  project.write("scripts/main.ts",
                "export async function run():Promise<number>"
                "{return (await import('./model.js')).n;}");
  auto program = compile(compiler(), project);
  Fs::remove_all(project.root / "scripts");
  CHECK(run(program) == "42");
}

TEST_CASE("compiler_project_config_and_ambient_node_types_cannot_change_contract") {
  Project project;
  project.write("scripts/main.ts", "export function run():number{return 42;}");
  project.write("scripts/tsconfig.json", "{broken project configuration");
  auto tool = compiler();
  CHECK(run(compile(tool, project)) == "42");
  project.write("scripts/main.ts", "export function run(){return process.pid;}");
  auto failed = tool.compileProject(project.files());
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("process") != std::string::npos);
}

TEST_CASE("compiler_imports_do_not_implicitly_add_sources_outside_snapshot") {
  Project project;
  project.write("outside/model.ts", "export const n:number=42;");
  project.write("scripts/main.ts",
                "import {n} from '../outside/model.js';export function run(){return n;}");
  auto tool = compiler();
  auto failed = tool.compileProject(project.files());
  REQUIRE_FALSE(failed);
  CHECK(failed.error().message.find("TS2307") != std::string::npos);
  project.write("scripts/main.ts", "import './missing.js';export function run(){return 42;}");
  CHECK_FALSE(tool.compileProject(project.files()));
  project.write("scripts/main.ts", "import {n} from 'package';export function run(){return n;}");
  CHECK_FALSE(tool.compileProject(project.files()));
}

TEST_CASE("compiler_empty_declaration_only_missing_and_invalid_paths_are_errors") {
  Project project;
  auto access = project.files();
  REQUIRE(access.createDirectories("scripts"));
  auto tool = compiler();
  CHECK_FALSE(tool.compileProject(access));
  project.write("scripts/globals.d.ts", "declare const n:number;");
  CHECK_FALSE(tool.compileProject(access));

  for (const auto& path : std::vector<std::string>{
           "", "/scripts", "../scripts", "./scripts", "scripts/", "scripts//nested", "a\\b", "C:/x",
           "scripts\n", "scripts ", "a?b", std::string{"scripts\0evil", 12}, "missing"}) {
    CHECK_FALSE(tool.compileProject(access, path));
  }
}

TEST_CASE("compiler_encoding_source_extension_and_symlink_boundaries_reject") {
  Project project;
  auto tool = compiler();

  for (const auto& text :
       std::vector<std::string>{std::string{"\xc0\x80"}, std::string{"export const n='\0';", 19}}) {
    project.write("scripts/main.ts", text);
    CHECK_FALSE(tool.compileProject(project.files()));
  }

  project.write("scripts/main.ts", "export const n=42;");
  project.write("scripts/bad.tsx", "export const n=42;");
  CHECK_FALSE(tool.compileProject(project.files()));
  Fs::remove(project.root / "scripts/bad.tsx");
  project.write("outside/source.ts", "export const n=42;");
  std::error_code error;
  Fs::create_symlink(project.root / "outside/source.ts", project.root / "scripts/link.ts", error);

  if (!error) {
    CHECK_FALSE(tool.compileProject(project.files()));
    Fs::remove(project.root / "scripts/link.ts");
    Fs::create_directory_symlink(project.root / "scripts", project.root / "alias", error);
    REQUIRE_FALSE(error);
    CHECK_FALSE(tool.compileProject(project.files(), "alias"));
  }
}

TEST_CASE("compiler_source_count_entry_depth_and_exact_byte_limits") {
  Project project;
  const std::string source = "export function run():number{return 42;}";
  project.write("scripts/main.ts", source);
  CompilerLimits limits;
  limits.fileBytes = source.size();
  limits.sourceBytes = source.size();
  CHECK(run(compile(compiler(limits), project)) == "42");
  --limits.fileBytes;
  CHECK_FALSE(compiler(limits).compileProject(project.files()));
  limits.fileBytes = source.size();
  --limits.sourceBytes;
  CHECK_FALSE(compiler(limits).compileProject(project.files()));
  limits = {};
  limits.sourceFiles = 1;
  project.write("scripts/part.ts", "export const n=1;");
  CHECK_FALSE(compiler(limits).compileProject(project.files()));
  limits = {};
  limits.directoryEntries = 1;
  CHECK_FALSE(compiler(limits).compileProject(project.files()));
  Fs::path nested = "scripts";

  for (int index = 0; index < 34; ++index) {
    nested /= "d";
  }

  REQUIRE(project.files().createDirectories(nested));
  CHECK_FALSE(compiler().compileProject(project.files()));
}

TEST_CASE("compiler_actual_output_byte_limit_cleans_candidate") {
  Project project;
  project.write("scripts/main.ts", "export function run():number{return 42;}");
  auto program = compile(compiler(), project);
  CompilerLimits limits;
  limits.outputBytes = program.stats()->outputBytes;
  CHECK(run(compile(compiler(limits), project)) == "42");
  --limits.outputBytes;
  const auto before = compilerGenerations();
  CHECK_FALSE(compiler(limits).compileProject(project.files()));
  CHECK(compilerGenerations() == before);
}

TEST_CASE("compiler_invalid_configuration_missing_and_incompatible_tool_fail_probe") {
  Project project;
  CompilerLimits limits;
  limits.sourceFiles = 0;
  CHECK_FALSE(TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER, limits));
  limits = {};
  limits.fileBytes = 0;
  CHECK_FALSE(TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER, limits));
  limits = {};
  limits.sourceBytes = 64 * 1024 * 1024 + 1;
  CHECK_FALSE(TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER, limits));
  limits = {};
  limits.timeout = std::chrono::milliseconds{0};
  CHECK_FALSE(TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER, limits));
  CHECK_FALSE(TypeScriptCompiler::create("tsc"));
  CHECK_FALSE(TypeScriptCompiler::create(project.root / "missing"));
  auto incompatible = TypeScriptCompiler::create(DOT_PROCESS_FIXTURE);
  REQUIRE_FALSE(incompatible);
  CHECK(incompatible.error().message.find("requires 7.0.2") != std::string::npos);
  limits = {};
  limits.diagnosticBytes = 1;
  CHECK_FALSE(TypeScriptCompiler::create(DOT_TYPESCRIPT_COMPILER, limits));
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("compiler_zero_exit_partial_output_and_unexpected_files_are_never_success") {
  Project project;
  auto tool = compiler({}, DOT_COMPILER_FIXTURE);
  const auto before = compilerGenerations();

  for (const auto* source : {"zero", "partial", "unexpected", "failure"}) {
    project.write("scripts/main.ts", source);
    auto failed = tool.compileProject(project.files());
    REQUIRE_FALSE(failed);

    if (std::string_view(source) == "failure") {
      CHECK(failed.error().message.find("35") != std::string::npos);
      CHECK(failed.error().message.find("fixture diagnostic") != std::string::npos);
    }

    CHECK(compilerGenerations() == before);
    CHECK(Dot::Core::processResources().children == 0);
    CHECK(Dot::Core::processResources().handles == 0);
  }
}

TEST_CASE("compiler_timeout_and_diagnostic_limit_release_children_and_generations") {
  Project project;
  REQUIRE(TypeScriptCompiler::create(DOT_COMPILER_FIXTURE));
  CompilerLimits limits;
  limits.timeout = std::chrono::milliseconds{1000};
  auto tool = compiler(limits, DOT_COMPILER_FIXTURE);
  project.write("scripts/main.ts", "sleep");
  const auto before = compilerGenerations();
  auto timeout = tool.compileProject(project.files());
  REQUIRE_FALSE(timeout);
  CHECK(timeout.error().message.find("timeout") != std::string::npos);
  limits = {};
  limits.diagnosticBytes = 128;
  project.write("scripts/main.ts", "diagnostics");
  auto output = compiler(limits, DOT_COMPILER_FIXTURE).compileProject(project.files());
  REQUIRE_FALSE(output);
  CHECK(output.error().message.find("output exceeds") != std::string::npos);
  CHECK(compilerGenerations() == before);
  CHECK(Dot::Core::processResources().children == 0);
  CHECK(Dot::Core::processResources().handles == 0);
}

TEST_CASE("compiler_concurrent_real_compilation_owns_distinct_generations") {
  Project project;
  project.write("scripts/main.ts", "export function run():number{return 42;}");
  auto tool = compiler();
  std::optional<CompiledProgram> first;
  std::optional<CompiledProgram> second;
  std::atomic<bool> failed{};
  const auto before = compilerGenerations();
  const auto input = project.files();
  auto compileOne = [&](std::optional<CompiledProgram>& destination) {
    auto result = tool.compileProject(input);

    if (result) {
      destination = std::move(*result);
    } else {
      failed = true;
    }
  };
  std::thread one([&] { compileOne(first); });
  std::thread two([&] { compileOne(second); });
  one.join();
  two.join();
  REQUIRE_FALSE(failed.load());
  REQUIRE(first);
  REQUIRE(second);
  CHECK(first->files()->root() != second->files()->root());
  CHECK(run(*first) == "42");
  CHECK(run(*second) == "42");
  CHECK(compilerGenerations() == before + 2);
  first.reset();
  second.reset();
  CHECK(compilerGenerations() == before);
  CHECK(Dot::Core::processResources().children == 0);
}
