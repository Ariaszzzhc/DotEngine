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
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Script.Compiler;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Text;
import Dot.Core.Process;
import Dot.Core.Paths;
import Dot.Core.StagedDirectory;
import Dot.Script.Modules;
import Dot.Script.Runtime;

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
      auto operator==(const Source&) const -> bool = default;
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

      /** Bundler resolution does not probe .mts for an extensionless name.
       * Private declaration aliases retain its real exports/types and .mjs emit;
       * the shared runtime linker checks actual exports, including default.
       * Bundler 不为无扩展名探测 .mts；私有声明映射保留真实类型和 .mjs 输出，
       * 共用运行时链接器校验实际导出（含 default）。 */
      for (const auto& source : sources) {
        const auto name = utf8(source.relative);

        if (!name.ends_with(".mts") || name.ends_with(".d.mts")) {
          continue;
        }

        auto alias = source.relative;
        alias.replace_extension(".d.ts");
        auto ts = source.relative;
        ts.replace_extension(".ts");

        if (std::ranges::any_of(sources, [&](const auto& candidate) {
              return candidate.relative == alias || candidate.relative == ts;
            })) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Ambiguous extensionless TypeScript module: " + name);
        }

        auto emitted = source.relative.filename();
        emitted.replace_extension(".mjs");
        const auto import = "./" + utf8(emitted);
        const auto quoted = nlohmann::json(import).dump();
        const auto declaration = "export * from " + quoted + ";\n"
            "declare const defaultValue: typeof import(" + quoted
            + ") extends { default: infer T } ? T : never;\nexport default defaultValue;\n";
        const auto path = Fs::path{"src"} / alias;
        auto written = generation.writeTextAtomic(path, declaration);

        if (!written) {
          return written;
        }

        files.push_back(utf8(path));
      }

      auto builtinDirectory = generation.createDirectories("engine-types");

      if (!builtinDirectory) {
        return builtinDirectory;
      }

      std::size_t index{};

      for (const auto& module : engineModules()) {
        const auto name = std::format("engine-types/module-{}.d.ts", index++);
        auto written = generation.writeTextAtomic(name, module.declarations);

        if (!written) {
          return written;
        }

        files.push_back(name);
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

  struct SourceSnapshot::Impl {
    std::vector<Source> sources;
  };

  SourceSnapshot::SourceSnapshot(std::shared_ptr<const Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  SourceSnapshot::SourceSnapshot(const SourceSnapshot&) noexcept = default;

  auto SourceSnapshot::operator=(const SourceSnapshot&) noexcept -> SourceSnapshot& = default;

  SourceSnapshot::SourceSnapshot(SourceSnapshot&&) noexcept = default;

  auto SourceSnapshot::operator=(SourceSnapshot&&) noexcept -> SourceSnapshot& = default;

  SourceSnapshot::~SourceSnapshot() = default;

  auto SourceSnapshot::equals(const SourceSnapshot& other) const noexcept -> bool {
    return m_impl && other.m_impl && m_impl->sources == other.m_impl->sources;
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

  auto CompiledProgram::publish(const Core::FileSystem& parent, const Fs::path& folder) const
      -> Core::Result<Core::FileSystem> {
    if (!m_impl) {
      return Core::fail(ErrorCode::InvalidState, "Compiled program was moved from");
    }

    if (folder.filename() != folder || !Core::isPortableRelativePath(utf8(folder))) {
      return Core::fail(ErrorCode::InvalidArgument,
                        "Published program folder must be one portable name");
    }

    try {
      for (auto current = parent.root();;) {
        std::error_code error;

        if (Fs::equivalent(current, m_impl->generation->root, error) && !error) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Cannot publish inside a temporary compiled generation");
        }

        const auto next = current.parent_path();

        if (next == current) {
          break;
        }

        current = next;
      }

      auto staging = Core::StagedDirectory::create(parent);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto files = staging->files().value();
      auto made = files.createDirectories("modules");

      if (!made) {
        return std::unexpected(made.error());
      }

      nlohmann::json entries = nlohmann::json::array();
      std::uint64_t total = 0;
      std::unordered_set<std::string> names;

      for (const auto& module : m_impl->modules) {
        if (!Core::isPortableRelativePath(module)) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Published module path is not portable: " + module);
        }

        auto folded = module;

        for (auto& c : folded) {
          if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
          }
        }

        if (!names.insert(folded).second) {
          return Core::fail(ErrorCode::InvalidArgument, "Published modules collide by ASCII case");
        }

        const Fs::path relative{std::u8string(module.begin(), module.end())};
        auto current = m_impl->files.root();

        for (const auto& part : relative) {
          current /= part;

          if (Fs::is_symlink(Fs::symlink_status(current))) {
            return Core::fail(ErrorCode::InvalidArgument, "Compiled module contains a symlink");
          }
        }

        if (!Fs::is_regular_file(Fs::symlink_status(current))) {
          return Core::fail(ErrorCode::InvalidArgument, "Compiled module must be a regular file");
        }

        auto text = m_impl->files.readText(relative, 64 * 1024 * 1024);

        if (!text) {
          return std::unexpected(text.error());
        }

        const auto marker = text->rfind("//# sourceMappingURL=");

        if (marker != std::string::npos && (marker == 0 || (*text)[marker - 1] == '\n')
            && (text->find('\n', marker) == std::string::npos
                || text->find('\n', marker) == text->size() - 1)) {
          text->erase(marker);
        }

        if (text->empty() || !Core::isValidUtf8(*text) || text->find('\0') != std::string::npos
            || text->size() > 64 * 1024 * 1024 - total) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Published module text is invalid or exceeds 64 MiB total");
        }

        const auto destination = Fs::path{"modules"} / relative;
        auto directory = files.createDirectories(destination.parent_path());

        if (!directory) {
          return std::unexpected(directory.error());
        }

        auto written = files.writeTextAtomic(destination, *text);

        if (!written) {
          return std::unexpected(written.error());
        }

        total += text->size();
        entries.push_back({{"path", module}, {"bytes", text->size()}});
      }

      const auto manifest = nlohmann::json{
          {"version", 1},
          {"format", "dot-esm"},
          {"sourceFiles", m_impl->stats.sourceFiles},
          {"sourceBytes", m_impl->stats.sourceBytes},
          {"outputBytes", total},
          {"modules", entries}}.dump();

      if (manifest.size() > 1024 * 1024) {
        return Core::fail(ErrorCode::Unavailable, "Published program manifest exceeds 1 MiB");
      }

      auto written = files.writeTextAtomic("program.json", manifest);

      if (!written) {
        return std::unexpected(written.error());
      }

      auto checked = loadPublished(files);

      if (!checked) {
        return Core::fail(checked.error().code, "Published program exceeds default load contract: "
                                                    + checked.error().message);
      }

      return staging->publish(folder);
    } catch (const Fs::filesystem_error& error) {
      return Core::fail(ErrorCode::Io, "Publish compiled program: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(ErrorCode::External,
                        "Publish compiled program: " + std::string(error.what()));
    }
  }

  auto CompiledProgram::loadPublished(const Core::FileSystem& package, const CompilerLimits& limits)
      -> Core::Result<CompiledProgram> {
    if (!validateLimits(limits)) {
      return Core::fail(ErrorCode::InvalidArgument, "Invalid published program limits");
    }

    try {
      const auto status = Fs::symlink_status(package.root() / "program.json");

      if (!Fs::is_regular_file(status)) {
        return Core::fail(ErrorCode::InvalidArgument, "Program manifest must be a regular file");
      }

      auto text = package.readText("program.json", 1024 * 1024);

      if (!text) {
        return std::unexpected(text.error());
      }

      auto manifest = Serialization::Detail::parseJson(*text);
      Serialization::Detail::requireFields(
          manifest, {"version", "format", "sourceFiles", "sourceBytes", "outputBytes", "modules"});

      const auto number = [](const nlohmann::json& value) -> std::uint64_t {
        if (!value.is_number_unsigned()) {
          throw std::invalid_argument("Published counts require unsigned integers");
        }

        return value.get<std::uint64_t>();
      };
      const auto sourceFiles = number(manifest["sourceFiles"]);
      const auto sourceBytes = number(manifest["sourceBytes"]);
      const auto outputBytes = number(manifest["outputBytes"]);
      const auto& entries = manifest["modules"];

      if (number(manifest["version"]) != 1 || manifest["format"] != "dot-esm" || sourceFiles == 0
          || sourceFiles > limits.sourceFiles || sourceBytes == 0
          || sourceBytes > limits.sourceBytes || outputBytes == 0
          || outputBytes > limits.outputBytes || !entries.is_array() || entries.empty()
          || entries.size() > sourceFiles) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Invalid published program header/count/size");
      }

      std::vector<std::string> modules;
      std::vector<std::uint64_t> sizes;
      std::unordered_set<std::string> unique;
      std::uint64_t total = 0;

      for (const auto& entry : entries) {
        Serialization::Detail::requireFields(entry, {"path", "bytes"});
        const auto name = entry["path"].get<std::string>();
        const auto size = number(entry["bytes"]);
        auto folded = name;

        for (auto& c : folded) {
          if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
          }
        }

        if (!Core::isPortableRelativePath(name)
            || (!name.ends_with(".js") && !name.ends_with(".mjs")) || !unique.insert(folded).second
            || (!modules.empty() && modules.back() >= name) || size == 0 || size > limits.fileBytes
            || size > limits.outputBytes - total) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Invalid/duplicate/unsorted published module or size");
        }

        total += size;
        modules.push_back(name);
        sizes.push_back(size);
      }

      if (total != outputBytes) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Published byte count disagrees with manifest");
      }

      std::vector<std::string> actual;
      std::unordered_set<std::string> required{"program.json", "modules"};

      for (const auto& name : modules) {
        auto file = Fs::path{"modules"} / Fs::path(std::u8string(name.begin(), name.end()));
        required.insert(utf8(file));

        for (auto parent = file.parent_path(); parent != Fs::path{"modules"};
             parent = parent.parent_path()) {
          required.insert(utf8(parent));
        }
      }

      std::unordered_set<std::string> aliases;

      for (auto name : required) {
        for (auto& c : name) {
          if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
          }
        }

        if (!aliases.insert(std::move(name)).second) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Published module directories collide by ASCII case");
        }
      }

      std::size_t count = 0;

      for (auto iterator = Fs::recursive_directory_iterator(package.root());
           iterator != Fs::recursive_directory_iterator{}; ++iterator) {
        if (++count > static_cast<std::size_t>(limits.directoryEntries) + 2
            || iterator.depth() > 33) {
          return Core::fail(ErrorCode::Unavailable, "Published tree entry/depth limit exceeded");
        }

        const auto relative = utf8(iterator->path().lexically_relative(package.root()));
        const auto entryStatus = iterator->symlink_status();

        if (!required.contains(relative) || Fs::is_symlink(entryStatus)
            || (!Fs::is_directory(entryStatus) && !Fs::is_regular_file(entryStatus))) {
          return Core::fail(
              ErrorCode::InvalidArgument,
              "Published tree contains unexpected files, symlinks or special entries");
        }

        actual.push_back(relative);
      }

      if (actual.size() != required.size()) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Published tree is missing modules/directories");
      }

      auto generation = reserveGeneration();

      if (!generation) {
        return std::unexpected(generation.error());
      }

      auto files = Core::FileSystem::open((*generation)->root).value();

      for (std::size_t index = 0; index < modules.size(); ++index) {
        const Fs::path relative{std::u8string(modules[index].begin(), modules[index].end())};
        auto source = package.readText(Fs::path{"modules"} / relative,
                                       static_cast<std::size_t>(limits.fileBytes));

        if (!source) {
          return std::unexpected(source.error());
        }

        if (source->size() != sizes[index] || !Core::isValidUtf8(*source)
            || source->find('\0') != std::string::npos) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Published module is invalid UTF-8/NUL or has a changed size");
        }

        auto directory = files.createDirectories(
            relative.parent_path().empty() ? Fs::path{"."} : relative.parent_path());

        if (!directory) {
          return std::unexpected(directory.error());
        }

        auto written = files.writeTextAtomic(relative, *source);

        if (!written) {
          return std::unexpected(written.error());
        }
      }

      return CompiledProgram(std::make_shared<Impl>(
          std::move(*generation), std::move(files), std::move(modules), std::string{},
          CompiledStats{static_cast<std::uint32_t>(sourceFiles), sourceBytes, outputBytes}));
    } catch (const nlohmann::json::exception& error) {
      return Core::fail(ErrorCode::InvalidArgument,
                        "Published program JSON: " + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(ErrorCode::InvalidArgument,
                        "Published program: " + std::string(error.what()));
    } catch (const Fs::filesystem_error& error) {
      return Core::fail(ErrorCode::Io, "Load published program: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(ErrorCode::External,
                        "Load published program: " + std::string(error.what()));
    }
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

  auto TypeScriptCompiler::captureProject(const Core::FileSystem& project,
                                          std::string_view directory) const
      -> Core::Result<SourceSnapshot> {
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

      return SourceSnapshot(std::make_shared<SourceSnapshot::Impl>(std::move(*sources)));
    } catch (const std::exception& error) {
      return Core::fail(ErrorCode::External, "TypeScript capture: " + std::string(error.what()));
    }
  }

  auto TypeScriptCompiler::compileProject(const Core::FileSystem& project,
                                          std::string_view directory) const
      -> Core::Result<CompiledProgram> {
    auto snapshot = captureProject(project, directory);

    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }

    return compileSnapshot(*snapshot);
  }

  auto TypeScriptCompiler::compileSnapshot(const SourceSnapshot& snapshot,
                                           std::stop_token stop) const
      -> Core::Result<CompiledProgram> {
    if (!snapshot.m_impl) {
      return Core::fail(ErrorCode::InvalidState, "Source snapshot was moved from");
    }

    if (stop.stop_requested()) {
      return Core::fail(ErrorCode::Unavailable, "TypeScript compilation cancelled");
    }

    const auto* sources = &snapshot.m_impl->sources;
    std::uint64_t sourceBytes = 0;

    if (sources->size() > m_limits.sourceFiles) {
      return Core::fail(ErrorCode::InvalidArgument,
                        "TypeScript snapshot exceeds source file limit");
    }

    for (const auto& source : *sources) {
      if (source.text.size() > m_limits.fileBytes
          || source.text.size() > m_limits.sourceBytes - sourceBytes) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "TypeScript snapshot exceeds source byte limits");
      }

      sourceBytes += source.text.size();
    }

    try {
      auto generation = reserveGeneration();

      if (!generation) {
        return std::unexpected(generation.error());
      }

      auto access = Core::FileSystem::open((*generation)->root);

      if (!access) {
        return std::unexpected(access.error());
      }

      auto written = writeSnapshot(*access, *sources);

      if (!written) {
        return std::unexpected(written.error());
      }

      auto process = Core::runProcess({m_executable,
                                       {"--project", "tsconfig.json", "--pretty", "false"},
                                       access->root(),
                                       m_limits.timeout,
                                       m_limits.diagnosticBytes,
                                       stop});

      if (!process) {
        return Core::fail(process.error().code, "TypeScript compile: " + process.error().message);
      }

      if (stop.stop_requested()) {
        return Core::fail(ErrorCode::Unavailable, "TypeScript compilation cancelled");
      }

      auto diagnostics = process->standardOutput + process->standardError;

      if (process->exitCode != 0) {
        return Core::fail(ErrorCode::External, std::format("TypeScript compile exit {}:\n{}",
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
        if (stop.stop_requested()) {
          return Core::fail(ErrorCode::Unavailable, "TypeScript compilation cancelled");
        }

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

      if (stop.stop_requested()) {
        return Core::fail(ErrorCode::Unavailable, "TypeScript compilation cancelled");
      }

      auto graph = Runtime::validateProgram(*output, modules);

      if (!graph) {
        return Core::fail(graph.error().code, "TypeScript module graph: " + graph.error().message);
      }

      if (stop.stop_requested()) {
        return Core::fail(ErrorCode::Unavailable, "TypeScript compilation cancelled");
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
