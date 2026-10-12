module;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module Dot.Script.Compiler;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Text;
import Dot.Core.Process;

namespace Dot::Script {
  namespace {
    namespace Fs = std::filesystem;
    using Core::ErrorCode;
    std::atomic<std::uint64_t> generations{};

    struct Generation {
      Fs::path root;

      Generation() = default;

      Generation(const Generation&) = delete;
      auto operator=(const Generation&) -> Generation& = delete;

      ~Generation() {
        if (!root.empty()) {
          std::error_code ignored;
          Fs::remove_all(root, ignored);
          generations.fetch_sub(1, std::memory_order_relaxed);
        }
      }
    };

    struct Source {
      Fs::path relative;
      std::string text;
    };

    auto utf8(const Fs::path& path) -> std::string {
      const auto text = path.generic_u8string();
      return {text.begin(), text.end()};
    }

    auto pathValid(std::string_view text) -> bool {
      if (text.empty() || text.size() > 4096 || !Core::isValidUtf8(text)
          || text.find_first_of("\\:<>\"|?*") != std::string_view::npos
          || text.find('\0') != std::string_view::npos) {
        return false;
      }

      const Fs::path path{std::u8string(text.begin(), text.end())};

      if (path.has_root_path() || utf8(path) != text) {
        return false;
      }

      return std::ranges::all_of(path, [](const auto& part) {
        const auto name = utf8(part);
        return !name.empty() && name != "." && name != ".." && name.back() != '.'
               && name.back() != ' ' && !std::ranges::any_of(name, [](unsigned char character) {
                    return character < 32;
                  });
      });
    }

    auto validateLimits(const CompilerLimits& limits) -> bool {
      constexpr std::uint64_t maxBytes = 64 * 1024 * 1024;
      return limits.sourceFiles >= 1 && limits.sourceFiles <= 1024 && limits.directoryEntries >= 1
             && limits.directoryEntries <= 65536 && limits.fileBytes >= 1
             && limits.fileBytes <= 16 * 1024 * 1024 && limits.sourceBytes >= 1
             && limits.sourceBytes <= maxBytes && limits.outputBytes >= 1
             && limits.outputBytes <= maxBytes && limits.diagnosticBytes >= 1
             && limits.diagnosticBytes <= maxBytes && limits.timeout >= std::chrono::milliseconds{1}
             && limits.timeout <= std::chrono::minutes{10};
    }

    auto reserveGeneration() -> Core::Result<std::shared_ptr<Generation>> {
      std::error_code error;
      const auto temporary = Fs::temp_directory_path(error);

      if (error) {
        return Core::fail(ErrorCode::Io, "TypeScript temporary root: " + error.message());
      }

      std::random_device random;
      auto generation = std::make_shared<Generation>();

      for (int attempt = 0; attempt < 64; ++attempt) {
        auto candidate = temporary
                         / std::format("dot-typescript-{:08x}{:08x}{:08x}{:08x}", random(),
                                       random(), random(), random());

        if (Fs::create_directory(candidate, error)) {
          generation->root = std::move(candidate);
          generations.fetch_add(1, std::memory_order_relaxed);
          return generation;
        }

        if (error) {
          return Core::fail(ErrorCode::Io, "TypeScript temporary directory: " + error.message());
        }
      }

      return Core::fail(ErrorCode::Io, "TypeScript cannot reserve a generation directory");
    }

    auto capture(const Core::FileSystem& project, const Fs::path& relative,
                 const CompilerLimits& limits) -> Core::Result<std::vector<Source>> {
      auto root = project.resolve(relative);

      if (!root) {
        return std::unexpected(root.error());
      }

      const auto lexical = project.root() / relative;
      Fs::path prefix = project.root();
      std::error_code error;

      for (const auto& part : relative) {
        prefix /= part;
        const auto status = Fs::symlink_status(prefix, error);

        if (error || Fs::is_symlink(status) || !Fs::is_directory(status)) {
          return Core::fail(
              ErrorCode::InvalidArgument,
              "TypeScript source directory must contain only real directories: " + utf8(prefix));
        }
      }

      std::vector<Source> sources;
      std::uint64_t bytes = 0;
      std::uint32_t entries = 0;
      Fs::recursive_directory_iterator iterator(lexical, error), end;

      if (error) {
        return Core::fail(ErrorCode::Io, "TypeScript source enumeration: " + error.message());
      }

      while (iterator != end) {
        if (++entries > limits.directoryEntries || iterator.depth() > 32) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "TypeScript source directory entry/depth limit exceeded");
        }

        const auto path = iterator->path();
        const auto status = iterator->symlink_status(error);

        if (error) {
          return Core::fail(ErrorCode::Io, "TypeScript source status: " + error.message());
        }

        if (Fs::is_symlink(status) || (!Fs::is_directory(status) && !Fs::is_regular_file(status))) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "TypeScript source contains a symlink/special file: " + utf8(path));
        }

        const auto local = path.lexically_relative(lexical);

        if (!pathValid(utf8(local))) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "TypeScript source has an invalid relative path: " + utf8(local));
        }

        if (Fs::is_regular_file(status)) {
          const auto extension = path.extension();

          if (extension == ".tsx" || extension == ".cts" || extension == ".js"
              || extension == ".mjs" || extension == ".jsx" || extension == ".cjs") {
            return Core::fail(ErrorCode::InvalidArgument,
                              "TypeScript unsupported source extension: " + utf8(local));
          }

          if (extension == ".ts" || extension == ".mts") {
            if (sources.size() >= limits.sourceFiles) {
              return Core::fail(ErrorCode::InvalidArgument,
                                "TypeScript source file limit exceeded");
            }

            auto text = project.readText(path.lexically_relative(project.root()),
                                         static_cast<std::size_t>(limits.fileBytes));

            if (!text) {
              return Core::fail(text.error().code,
                                "TypeScript source " + utf8(local) + ": " + text.error().message);
            }

            if (!Core::isValidUtf8(*text) || text->find('\0') != std::string::npos) {
              return Core::fail(ErrorCode::InvalidArgument,
                                "TypeScript source must be UTF-8 without NUL: " + utf8(local));
            }

            if (text->size() > limits.sourceBytes - bytes) {
              return Core::fail(ErrorCode::InvalidArgument,
                                "TypeScript aggregate source byte limit exceeded");
            }

            bytes += text->size();
            sources.push_back({local, std::move(*text)});
          }
        }

        iterator.increment(error);

        if (error) {
          return Core::fail(ErrorCode::Io, "TypeScript source enumeration: " + error.message());
        }
      }

      if (sources.empty()) {
        return Core::fail(ErrorCode::InvalidArgument, "TypeScript source directory has no sources");
      }

      std::ranges::sort(sources, {}, [](const auto& source) { return utf8(source.relative); });
      return sources;
    }

    auto writeSnapshot(const Core::FileSystem& generation, const std::vector<Source>& sources)
        -> Core::Result<void> {
      auto made = generation.createDirectories("src");

      if (!made) {
        return made;
      }

      nlohmann::json files = nlohmann::json::array();

      for (const auto& source : sources) {
        const auto target = Fs::path{"src"} / source.relative;
        auto directories = generation.createDirectories(target.parent_path());

        if (!directories) {
          return directories;
        }

        auto written = generation.writeTextAtomic(target, source.text);

        if (!written) {
          return written;
        }

        files.push_back(utf8(target));
      }

      const nlohmann::json options{{"target", "ES2022"},
                                   {"module", "ESNext"},
                                   {"moduleResolution", "Bundler"},
                                   {"moduleDetection", "force"},
                                   {"strict", true},
                                   {"noEmitOnError", true},
                                   {"isolatedModules", true},
                                   {"verbatimModuleSyntax", true},
                                   {"noResolve", true},
                                   {"noUncheckedSideEffectImports", true},
                                   {"forceConsistentCasingInFileNames", true},
                                   {"types", nlohmann::json::array()},
                                   {"lib", {"ES2022"}},
                                   {"sourceMap", true},
                                   {"inlineSources", true},
                                   {"rootDir", "src"},
                                   {"outDir", "out"}};
      return generation.writeTextAtomic(
          "tsconfig.json", nlohmann::json{{"compilerOptions", options}, {"files", files}}.dump(2));
    }
  }  // namespace

  auto compilerGenerations() noexcept -> std::uint64_t {
    return generations.load(std::memory_order_relaxed);
  }

  struct CompiledProgram::Impl {
    std::shared_ptr<Generation> generation;
    Core::FileSystem files;
    std::vector<std::string> modules;
    std::string diagnostics;
    CompiledStats stats;
  };

  CompiledProgram::CompiledProgram(std::shared_ptr<const Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  CompiledProgram::CompiledProgram(const CompiledProgram&) noexcept = default;

  auto CompiledProgram::operator=(const CompiledProgram&) noexcept -> CompiledProgram& = default;

  CompiledProgram::CompiledProgram(CompiledProgram&&) noexcept = default;

  auto CompiledProgram::operator=(CompiledProgram&&) noexcept -> CompiledProgram& = default;

  CompiledProgram::~CompiledProgram() = default;

  auto CompiledProgram::files() const -> Core::Result<Core::FileSystem> {
    if (!m_impl) {
      return Core::fail(ErrorCode::InvalidState, "Compiled program was moved from");
    }

    return m_impl->files;
  }

  auto CompiledProgram::modules() const -> Core::Result<std::vector<std::string>> {
    if (!m_impl) {
      return Core::fail(ErrorCode::InvalidState, "Compiled program was moved from");
    }

    return m_impl->modules;
  }

  auto CompiledProgram::diagnostics() const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(ErrorCode::InvalidState, "Compiled program was moved from");
    }

    return m_impl->diagnostics;
  }

  auto CompiledProgram::stats() const -> Core::Result<CompiledStats> {
    if (!m_impl) {
      return Core::fail(ErrorCode::InvalidState, "Compiled program was moved from");
    }

    return m_impl->stats;
  }

  TypeScriptCompiler::TypeScriptCompiler(Fs::path executable, CompilerLimits limits)
      : m_executable(std::move(executable)), m_limits(limits) {
  }

  auto TypeScriptCompiler::create(const Fs::path& executable, const CompilerLimits& limits)
      -> Core::Result<TypeScriptCompiler> {
    if (!validateLimits(limits) || !executable.is_absolute()) {
      return Core::fail(ErrorCode::InvalidArgument,
                        "TypeScript needs an absolute executable and valid compiler limits");
    }

    auto probe = Core::runProcess({executable,
                                   {"--version"},
                                   executable.parent_path(),
                                   limits.timeout,
                                   limits.diagnosticBytes});

    if (!probe) {
      return Core::fail(probe.error().code, "TypeScript version probe: " + probe.error().message);
    }

    if (probe->exitCode != 0 || !probe->standardError.empty()
        || (probe->standardOutput != "Version 7.0.2\n"
            && probe->standardOutput != "Version 7.0.2\r\n")) {
      return Core::fail(ErrorCode::External,
                        std::format("TypeScript requires 7.0.2; probe exit {}: {}{}",
                                    probe->exitCode, probe->standardOutput, probe->standardError));
    }

    return TypeScriptCompiler(executable, limits);
  }

  auto TypeScriptCompiler::compileProject(const Core::FileSystem& project,
                                          std::string_view directory) const
      -> Core::Result<CompiledProgram> {
    if (!pathValid(directory)) {
      return Core::fail(ErrorCode::InvalidArgument,
                        "TypeScript source directory must be canonical root-relative UTF-8");
    }

    try {
      const Fs::path relative{std::u8string(directory.begin(), directory.end())};
      auto sources = capture(project, relative, m_limits);

      if (!sources) {
        return std::unexpected(sources.error());
      }

      auto generation = reserveGeneration();

      if (!generation) {
        return std::unexpected(generation.error());
      }

      auto access = Core::FileSystem::open((*generation)->root);

      if (!access) {
        return std::unexpected(access.error());
      }

      auto snapshot = writeSnapshot(*access, *sources);

      if (!snapshot) {
        return std::unexpected(snapshot.error());
      }

      auto process = Core::runProcess({m_executable,
                                       {"--project", "tsconfig.json", "--pretty", "false"},
                                       access->root(),
                                       m_limits.timeout,
                                       m_limits.diagnosticBytes});

      if (!process) {
        return Core::fail(process.error().code, "TypeScript compile: " + process.error().message);
      }

      auto diagnostics = process->standardOutput + process->standardError;

      if (process->exitCode != 0) {
        return Core::fail(ErrorCode::External, std::format("TypeScript compile exit {}: {}",
                                                           process->exitCode, diagnostics));
      }

      auto output = Core::FileSystem::open(access->root() / "out");

      if (!output) {
        return Core::fail(output.error().code,
                          "TypeScript produced no output: " + output.error().message);
      }

      std::vector<std::string> modules;
      CompiledStats stats{static_cast<std::uint32_t>(sources->size()), 0, 0};

      for (const auto& source : *sources) {
        stats.sourceBytes += source.text.size();
        const auto name = utf8(source.relative);

        if (name.ends_with(".d.ts") || name.ends_with(".d.mts")) {
          continue;
        }

        auto module = source.relative;
        module.replace_extension(source.relative.extension() == ".mts" ? ".mjs" : ".js");
        modules.push_back(utf8(module));
      }

      if (modules.empty()) {
        return Core::fail(ErrorCode::External, "TypeScript produced no executable modules");
      }

      std::vector<std::string> expected;

      for (const auto& module : modules) {
        expected.push_back(module);
        expected.push_back(module + ".map");
      }

      std::ranges::sort(expected);
      std::vector<std::string> actual;
      std::error_code error;
      Fs::recursive_directory_iterator iterator(output->root(), error), end;

      if (error) {
        return Core::fail(ErrorCode::Io, "TypeScript output enumeration: " + error.message());
      }

      std::uint32_t entries = 0;

      while (iterator != end) {
        if (++entries > m_limits.directoryEntries + m_limits.sourceFiles * 2
            || iterator.depth() > 32) {
          return Core::fail(ErrorCode::External, "TypeScript output entry/depth limit exceeded");
        }

        const auto status = iterator->symlink_status(error);

        if (error || Fs::is_symlink(status)
            || (!Fs::is_directory(status) && !Fs::is_regular_file(status))) {
          return Core::fail(ErrorCode::External, "TypeScript output has invalid file type");
        }

        if (Fs::is_regular_file(status)) {
          const auto path = iterator->path().lexically_relative(output->root());
          const auto name = utf8(path);

          if (!std::ranges::binary_search(expected, name)) {
            return Core::fail(ErrorCode::External, "TypeScript emitted unexpected file: " + name);
          }

          auto text = output->readText(
              path, static_cast<std::size_t>(m_limits.outputBytes - stats.outputBytes));

          if (!text) {
            return Core::fail(text.error().code,
                              "TypeScript output " + name + ": " + text.error().message);
          }

          if (text->empty() || !Core::isValidUtf8(*text) || text->find('\0') != std::string::npos) {
            return Core::fail(ErrorCode::External,
                              "TypeScript emitted invalid UTF-8/empty file: " + name);
          }

          stats.outputBytes += text->size();
          actual.push_back(name);
        }

        iterator.increment(error);

        if (error) {
          return Core::fail(ErrorCode::Io, "TypeScript output enumeration: " + error.message());
        }
      }

      std::ranges::sort(actual);

      if (actual != expected) {
        return Core::fail(ErrorCode::External, "TypeScript output is missing modules/source maps");
      }

      std::ranges::sort(modules);
      return CompiledProgram(std::make_shared<CompiledProgram::Impl>(
          std::move(*generation), std::move(*output), std::move(modules), std::move(diagnostics),
          stats));
    } catch (const std::exception& exception) {
      return Core::fail(ErrorCode::External,
                        "TypeScript compile: " + std::string(exception.what()));
    }
  }
}  // namespace Dot::Script
