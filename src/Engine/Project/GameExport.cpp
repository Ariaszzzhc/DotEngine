module;

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Project.GameExport;

import Dot.Core.Paths;
import Dot.Core.StagedDirectory;
import Dot.Project.GamePackage;

namespace Dot::Project {
  namespace {
    namespace Fs = std::filesystem;
    using Json = Serialization::Detail::Json;
    constexpr std::uint64_t fileLimit = 512ULL * 1024 * 1024;
    constexpr std::uint64_t totalLimit = 1024ULL * 1024 * 1024;
#if defined(_WIN32)
    constexpr std::string_view platform = "windows-x64";
    constexpr std::string_view executable = "DotGame.exe";
#elif defined(__APPLE__)
    constexpr std::string_view platform = "macos-arm64";
    constexpr std::string_view executable = "DotGame";
#else
    constexpr std::string_view platform = "linux-x64";
    constexpr std::string_view executable = "DotGame";
#endif

    auto path(std::string_view text) -> Fs::path {
      return Fs::path(std::u8string(text.begin(), text.end()));
    }

    auto utf(const Fs::path& value) -> std::string {
      auto bytes = value.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto fold(std::string name) -> std::string {
      for (auto& c : name) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }

      return name;
    }

    auto failure(std::string_view operation, const std::exception& error)
        -> std::unexpected<Core::Error> {
      return Core::fail(dynamic_cast<const Fs::filesystem_error*>(&error)
                            ? Core::ErrorCode::Io
                            : Core::ErrorCode::External,
                        std::string(operation) + ": " + error.what());
    }
  }  // namespace

  struct RuntimeKit::Impl {
    Core::FileSystem m_files;
    std::string m_manifest;
    std::vector<std::string> m_records;
  };

  RuntimeKit::RuntimeKit(std::shared_ptr<const Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  RuntimeKit::RuntimeKit(const RuntimeKit&) noexcept = default;

  auto RuntimeKit::operator=(const RuntimeKit&) noexcept -> RuntimeKit& = default;

  RuntimeKit::RuntimeKit(RuntimeKit&&) noexcept = default;

  auto RuntimeKit::operator=(RuntimeKit&&) noexcept -> RuntimeKit& = default;

  RuntimeKit::~RuntimeKit() = default;

  auto RuntimeKit::open(const Core::FileSystem& files) -> Core::Result<RuntimeKit> {
    try {
      if (!Fs::is_regular_file(Fs::symlink_status(files.root() / "runtime.json"))) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Runtime manifest must be regular");
      }

      auto source = files.readText("runtime.json", 1024 * 1024);

      if (!source) {
        return std::unexpected(source.error());
      }

      auto manifest = Serialization::Detail::parseJson(*source);
      Serialization::Detail::requireFields(manifest,
                                           {"version", "platform", "executable", "files"});

      if (!manifest["version"].is_number_unsigned() || manifest["version"] != 1
          || manifest["platform"] != platform || manifest["executable"] != executable
          || !manifest["files"].is_array() || manifest["files"].empty()
          || manifest["files"].size() > 512) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Runtime kit version/platform/executable/count mismatch");
      }

      std::vector<std::string> records;
      std::unordered_set<std::string> required{"runtime.json"};
      std::uint64_t total = 0;

      for (const auto& record : manifest["files"]) {
        Serialization::Detail::requireFields(record, {"path", "bytes"});
        auto name = record["path"].get<std::string>();

        if (!Core::isPortableRelativePath(name) || name == "runtime.json"
            || !(name == executable || name == "EngineSans.ttf" || name.starts_with("lib/")
                 || name.starts_with("licenses/")
#if defined(_WIN32)
                 || (path(name).filename() == path(name) && fold(name).ends_with(".dll"))
#endif
                     )
            || (!records.empty() && records.back() >= name)
            || !record["bytes"].is_number_unsigned()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid runtime file record");
        }

        const auto size = record["bytes"].get<std::uint64_t>();

        if (size == 0 || size > fileLimit || size > totalLimit - total
            || !Fs::is_regular_file(Fs::symlink_status(files.root() / path(name)))
            || Fs::file_size(files.root() / path(name)) != size) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Runtime file type/size mismatch: " + name);
        }

        total += size;
        records.push_back(name);
        required.insert(name);

        for (auto parent = path(name).parent_path(); !parent.empty();
             parent = parent.parent_path()) {
          required.insert(utf(parent));
        }
      }

      if (!required.contains(std::string(executable)) || !required.contains("EngineSans.ttf")) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Runtime executable or UI font is missing");
      }

      std::unordered_set<std::string> aliases;

      for (const auto& name : required) {
        if (!aliases.insert(fold(name)).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Runtime paths collide by ASCII case");
        }
      }

      std::size_t count = 0;
      std::size_t visited = 0;

      for (auto iterator = Fs::recursive_directory_iterator(files.root());
           iterator != Fs::recursive_directory_iterator{}; ++iterator) {
        const auto relative = utf(iterator->path().lexically_relative(files.root()));
        const auto status = iterator->symlink_status();

        if (++visited > 1024 || iterator.depth() > 16) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Excessive runtime tree");
        }

#if defined(__APPLE__)
        if (iterator->path().filename() == ".DS_Store" && Fs::is_regular_file(status)
            && iterator->file_size() <= 64 * 1024) {
          continue;
        }
#endif

        ++count;

        if (!required.contains(relative) || Fs::is_symlink(status)
            || (!Fs::is_directory(status) && !Fs::is_regular_file(status))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unexpected runtime tree entry: " + relative);
        }
      }

      if (count != required.size()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Runtime tree is missing entries");
      }

#if !defined(_WIN32)
      const auto permissions = Fs::status(files.root() / path(executable)).permissions();

      if ((permissions & (Fs::perms::owner_exec | Fs::perms::group_exec | Fs::perms::others_exec))
          == Fs::perms::none) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Runtime has no executable permission");
      }
#endif

      return RuntimeKit(std::make_shared<Impl>(files, std::move(*source), std::move(records)));
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Runtime manifest: " + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Runtime manifest: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return failure("Open runtime kit", error);
    }
  }

  auto RuntimeKit::publish(const Core::FileSystem& parent, const Fs::path& folder) const
      -> Core::Result<Core::FileSystem> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Runtime kit was moved");
    }

    if (folder.filename() != folder || !Core::isPortableRelativePath(utf(folder))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Runtime destination requires one name");
    }

    try {
      for (auto current = parent.root();;) {
        std::error_code error;

        if (Fs::equivalent(current, m_impl->m_files.root(), error) && !error) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Cannot publish inside the source runtime kit");
        }

        const auto next = current.parent_path();

        if (next == current) {
          break;
        }

        current = next;
      }

      auto checked = open(m_impl->m_files);

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (checked->m_impl->m_manifest != m_impl->m_manifest) {
        return Core::fail(Core::ErrorCode::InvalidState, "Runtime kit manifest changed; reopen it");
      }

      auto staging = Core::StagedDirectory::create(parent);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto output = staging->files().value();

      for (const auto& name : m_impl->m_records) {
        const auto relative = path(name);
        auto made = output.createDirectories(
            relative.parent_path().empty() ? Fs::path{"."} : relative.parent_path());

        if (!made) {
          return std::unexpected(made.error());
        }

        Fs::copy_file(m_impl->m_files.root() / relative, output.root() / relative);
        Fs::permissions(output.root() / relative,
                        Fs::status(m_impl->m_files.root() / relative).permissions());
      }

      auto written = output.writeTextAtomic("runtime.json", m_impl->m_manifest);

      if (!written) {
        return std::unexpected(written.error());
      }

      auto verified = open(output);

      if (!verified) {
        return std::unexpected(verified.error());
      }

      return staging->publish(folder);
    } catch (const std::exception& error) {
      return failure("Copy runtime kit", error);
    }
  }

  auto exportGame(const Project& project, const World::Scene& scene,
                  const Script::TypeScriptCompiler& compiler, const RuntimeKit& runtime,
                  const Core::FileSystem& parent, const Fs::path& folder,
                  std::optional<std::string> scenePath) -> Core::Result<Core::FileSystem> {
    if (folder.filename() != folder || !Core::isPortableRelativePath(utf(folder))) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Game destination requires one name");
    }

    auto staging = Core::StagedDirectory::create(parent);

    if (!staging) {
      return std::unexpected(staging.error());
    }

    auto files = staging->files().value();
    auto copied = runtime.publish(files, "native");

    if (!copied) {
      return std::unexpected(copied.error());
    }

    auto data
        = GamePackage::publish(project, scene, compiler, *copied, "game", {}, std::move(scenePath));

    if (!data) {
      return std::unexpected(data.error());
    }

    try {
      std::vector<Fs::path> entries;

      for (const auto& entry : Fs::directory_iterator(copied->root())) {
        entries.push_back(entry.path().filename());
      }

      for (const auto& name : entries) {
        Fs::rename(copied->root() / name, files.root() / name);
      }

      Fs::remove(copied->root());
      return staging->publish(folder);
    } catch (const std::exception& error) {
      return failure("Publish game distribution", error);
    }
  }
}  // namespace Dot::Project
