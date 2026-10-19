module;

#include <algorithm>
#include <exception>
#include <expected>
#include <filesystem>
#include <iterator>
#include <limits>
#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

module Dot.Editor.Resources;

import Dot.Core.Files;
import Dot.Core.StagedDirectory;
import Dot.Assets.Model;
import Dot.Core.Paths;
import Dot.Core.Text;

namespace Dot::Editor {
  namespace {
    namespace Fs = std::filesystem;

    auto path(std::string_view text) -> Fs::path {
      return Fs::path(std::u8string(text.begin(), text.end()));
    }

    auto utf(const Fs::path& value) -> std::string {
      const auto bytes = value.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto folded(std::string_view text) -> std::string {
      std::string result(text);

      for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }

      return result;
    }

    auto inside(std::string_view value, std::string_view root) -> bool {
      return value == root
             || (value.starts_with(root) && value.size() > root.size()
                 && value[root.size()] == '/');
    }

    auto textPath(std::string_view value) -> bool {
      if (!Core::isPortableRelativePath(value)) {
        return false;
      }

      const auto extension = path(value).extension();
      return extension == ".ts" || extension == ".mts" || extension == ".json" || extension == ".md"
             || extension == ".txt" || extension == ".obj" || extension == ".wgsl"
             || extension == ".dotmaterial" || extension == ".dotscene" || extension == ".gltf"
             || extension == ".dotmodel" || extension == ".dotanimation"
             || extension == ".dotcontroller" || extension == ".dotretarget"
             || extension == ".dotui";
    }

    auto validText(std::string_view text, std::size_t limit) -> Core::Result<void> {
      if (text.size() > limit || text.find('\0') != std::string_view::npos
          || !Core::isValidUtf8(text)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Resource text must be bounded UTF-8 without NUL");
      }

      return {};
    }

    auto external(std::string_view operation, const std::exception& error)
        -> std::unexpected<Core::Error> {
      return Core::fail(dynamic_cast<const Fs::filesystem_error*>(&error)
                            ? Core::ErrorCode::Io
                            : Core::ErrorCode::External,
                        std::string(operation) + ": " + error.what());
    }
  }  // namespace

  struct ResourceStore::Impl {
    std::thread::id m_thread{std::this_thread::get_id()};
    Core::FileSystem m_files;
    Project::Description m_description;
    ResourceLimits m_limits;

    Impl(Core::FileSystem files, Project::Description description, ResourceLimits limits)
        : m_files(std::move(files)), m_description(std::move(description)), m_limits(limits) {
    }

    auto owner() const -> Core::Result<void> {
      return m_thread == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Resource operation requires owner thread");
    }

    auto check() const -> Core::Result<void> {
      auto checked = owner();

      if (!checked) {
        return checked;
      }

      auto metadata = m_files.readText("project.dotproject", 64 * 1024);

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      auto description = Project::decodeDescription(*metadata);

      if (!description) {
        return std::unexpected(description.error());
      }

      return *description == m_description
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Resource project metadata changed; reopen the project");
    }

    auto readOnly(std::string_view value) const -> bool {
      if (inside(folded(value), folded(m_description.scriptsDirectory + "/dot"))) {
        return true;
      }

      const auto sdk = m_files.root() / path(m_description.scriptsDirectory) / "dot";
      auto current = m_files.root();

      for (const auto& part : path(value)) {
        current /= part;
        std::error_code error;

        if (Fs::equivalent(current, sdk, error) && !error) {
          return true;
        }
      }

      return false;
    }

    auto resolve(std::string_view value, bool write = false, bool absent = false) const
        -> Core::Result<Fs::path> {
      auto checked = check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      if (!Core::isPortableRelativePath(value)
          || value.find("/.dot-stage-") != std::string_view::npos
          || (!inside(value, "assets") && !inside(value, "scenes")
              && !inside(value, m_description.scriptsDirectory))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Resource path must be portable, outside engine staging and inside "
                          "assets, scenes or scripts");
      }

      if (write && readOnly(value)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Project SDK is read-only");
      }

      auto current = m_files.root();
      const auto relative = path(value);
      const auto sdk = m_files.root() / path(m_description.scriptsDirectory) / "dot";

      for (const auto& part : relative) {
        std::size_t scanned = 0;

        for (const auto& entry : Fs::directory_iterator(current)) {
          if (++scanned > m_limits.entries) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Resource path lookup entry capacity exceeded");
          }

          const auto actual = utf(entry.path().filename());

          if (folded(actual) == folded(utf(part)) && actual != utf(part)) {
            return Core::fail(
                Core::ErrorCode::InvalidArgument,
                "Use exact resource path spelling; ASCII case aliases are unsupported");
          }
        }

        current /= part;
        std::error_code error;
        const auto status = Fs::symlink_status(current, error);

        if (status.type() == Fs::file_type::not_found) {
          if (absent && current == m_files.root() / relative) {
            return current;
          }

          return Core::fail(Core::ErrorCode::NotFound,
                            "Resource path is missing: " + std::string(value));
        }

        if (error || Fs::is_symlink(status)
            || (!Fs::is_directory(status) && !Fs::is_regular_file(status))) {
          return Core::fail(
              Core::ErrorCode::Io,
              "Resource path is inaccessible, a symlink or a special file: " + std::string(value));
        }

        if (current != m_files.root() / relative && !Fs::is_directory(status)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Resource parent must be a directory");
        }

        if (write && Fs::is_directory(status) && Fs::equivalent(current, sdk, error) && !error) {
          return Core::fail(Core::ErrorCode::InvalidState, "Project SDK directory is read-only");
        }
      }

      return current;
    }
  };

  struct PreparedModelImport::Impl {
    Core::StagedDirectory staging;
    Fs::path root;
    std::string destination;
    std::string metadata;
    ModelImportReport report;
    std::size_t entries;
    bool published{};
  };

  PreparedModelImport::PreparedModelImport(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  PreparedModelImport::PreparedModelImport(PreparedModelImport&&) noexcept = default;

  auto PreparedModelImport::operator=(PreparedModelImport&&) noexcept
      -> PreparedModelImport& = default;

  PreparedModelImport::~PreparedModelImport() = default;

  auto PreparedModelImport::report() const -> Core::Result<ModelImportReport> {
    return m_impl
               ? Core::Result<ModelImportReport>{m_impl->report}
               : Core::fail(Core::ErrorCode::InvalidState, "Prepared model was moved or discarded");
  }

  auto PreparedModelImport::discard() -> Core::Result<void> {
    if (!m_impl) {
      return {};
    }

    auto discarded = m_impl->staging.discard();

    if (discarded) {
      m_impl.reset();
    }

    return discarded;
  }

  struct TextDocument::Impl {
    std::shared_ptr<ResourceStore::Impl> m_store;
    std::string m_path;
    std::string m_baseline;
    std::string m_text;
    std::uint64_t m_revision{1};
    bool m_readOnly;

    Impl(std::shared_ptr<ResourceStore::Impl> store, std::string path, std::string text)
        : m_store(std::move(store)),
          m_path(std::move(path)),
          m_baseline(text),
          m_text(std::move(text)),
          m_readOnly(m_store->readOnly(m_path)) {
    }

    auto check(std::uint64_t revision) const -> Core::Result<void> {
      auto checked = m_store->owner();

      if (!checked) {
        return checked;
      }

      if (revision != m_revision || m_readOnly) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Resource text revision is stale or the file is read-only");
      }

      return {};
    }
  };

  ResourceStore::ResourceStore(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  ResourceStore::ResourceStore(ResourceStore&&) noexcept = default;

  auto ResourceStore::operator=(ResourceStore&&) noexcept -> ResourceStore& = default;

  ResourceStore::~ResourceStore() = default;

  auto ResourceStore::open(const Project::Project& project, const ResourceLimits& limits)
      -> Core::Result<ResourceStore> {
    if (limits.textBytes == 0 || limits.textBytes > 64 * 1024 * 1024 || limits.binaryBytes == 0
        || limits.binaryBytes > 64 * 1024 * 1024 || limits.entries == 0 || limits.entries > 65536
        || limits.depth == 0 || limits.depth > 128) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid resource limits");
    }

    try {
      auto files = project.files();
      auto description = project.description();

      if (!files || !description) {
        return std::unexpected(files ? description.error() : files.error());
      }

      auto impl = std::make_shared<Impl>(*files, *description, limits);
      auto checked = impl->check();
      return checked ? Core::Result<ResourceStore>{ResourceStore(std::move(impl))}
                     : std::unexpected(checked.error());
    } catch (const std::exception& error) {
      return external("Open resources", error);
    }
  }

  auto ResourceStore::list() const -> Core::Result<std::vector<ResourceEntry>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      auto checked = m_impl->check();

      if (!checked) {
        return std::unexpected(checked.error());
      }

      std::vector<std::string> roots{"assets", "scenes"};

      if (!inside(m_impl->m_description.scriptsDirectory, "assets")
          && !inside(m_impl->m_description.scriptsDirectory, "scenes")) {
        roots.push_back(m_impl->m_description.scriptsDirectory);
      }

      std::vector<ResourceEntry> result;
      std::unordered_set<std::string> names;
      const auto append = [&](const Fs::path& file) -> Core::Result<void> {
        const auto name = utf(file.lexically_relative(m_impl->m_files.root()));

        if (!Core::isPortableRelativePath(name) || !names.insert(folded(name)).second) {
          return Core::fail(
              Core::ErrorCode::InvalidArgument,
              "Resource listing contains a non-portable path or ASCII case collision");
        }

        const auto status = Fs::symlink_status(file);

        if (Fs::is_symlink(status) || (!Fs::is_directory(status) && !Fs::is_regular_file(status))) {
          return Core::fail(Core::ErrorCode::Io,
                            "Resource listing contains a symlink or special file");
        }

        if (result.size() == m_impl->m_limits.entries) {
          return Core::fail(Core::ErrorCode::Unavailable, "Resource entry capacity exceeded");
        }

        const bool directory = Fs::is_directory(status);
        result.push_back({name, directory ? 0 : Fs::file_size(file), directory,
                          !directory && textPath(name), m_impl->readOnly(name)});
        return {};
      };

      for (const auto& root : roots) {
        const bool optional = (root == "assets" || root == "scenes")
                              && !inside(m_impl->m_description.scriptsDirectory, root);
        auto resolved = m_impl->resolve(root, false, optional);

        if (!resolved) {
          return std::unexpected(resolved.error());
        }

        if (!Fs::exists(*resolved) && optional) {
          continue;
        }

        if (!Fs::is_directory(*resolved)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Resource root must be a directory");
        }

        auto added = append(*resolved);

        if (!added) {
          return std::unexpected(added.error());
        }

        for (auto iterator = Fs::recursive_directory_iterator(*resolved);
             iterator != Fs::recursive_directory_iterator{}; ++iterator) {
          if (utf(iterator->path().filename()).starts_with(".dot-stage-")) {
            iterator.disable_recursion_pending();
            continue;
          }

          if (static_cast<std::size_t>(iterator.depth()) >= m_impl->m_limits.depth) {
            return Core::fail(Core::ErrorCode::Unavailable, "Resource hierarchy depth exceeded");
          }

          auto entry = append(iterator->path());

          if (!entry) {
            return std::unexpected(entry.error());
          }
        }
      }

      std::ranges::sort(result, {}, &ResourceEntry::path);
      return result;
    } catch (const std::exception& error) {
      return external("List resources", error);
    }
  }

  auto ResourceStore::createDirectory(std::string_view name) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      auto target = m_impl->resolve(name, true, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      if (Fs::exists(*target)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Resource destination already exists");
      }

      return Fs::create_directory(*target)
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::Io, "Create resource directory failed");
    } catch (const std::exception& error) {
      return external("Create resource directory", error);
    }
  }

  auto ResourceStore::createText(std::string_view name, std::string_view text)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      auto valid = validText(text, m_impl->m_limits.textBytes);

      if (!valid || !textPath(name)) {
        return valid ? Core::fail(Core::ErrorCode::InvalidArgument,
                                  "Unsupported resource text extension")
                     : valid;
      }

      auto target = m_impl->resolve(name, true, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      if (Fs::exists(*target)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Resource destination already exists");
      }

      auto created = m_impl->m_files.createTextAtomic(path(name), text);
      return created ? created
                     : Core::Result<void>{
                           Core::fail(created.error().code, "Create resource " + std::string(name)
                                                                + ": " + created.error().message)};
    } catch (const std::exception& error) {
      return external("Create resource text", error);
    }
  }

  auto ResourceStore::prepareModel(const Fs::path& source, std::string_view destination,
                                   std::optional<std::uint32_t> scene, std::stop_token stop)
      -> Core::Result<PreparedModelImport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      auto target = m_impl->resolve(destination, true, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      if (!destination.starts_with("assets/") || Fs::exists(*target)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Model destination must be a new assets directory");
      }

      const auto relative = path(destination);

      if (static_cast<std::size_t>(std::distance(relative.begin(), relative.end()))
          >= m_impl->m_limits.depth) {
        return Core::fail(Core::ErrorCode::Unavailable, "Generated model exceeds resource depth");
      }

      auto metadata = m_impl->m_files.readText("project.dotproject", 64 * 1024);

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      if (stop.stop_requested()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Model import cancelled before reading");
      }

      const auto sourceText = utf(source);

      if (sourceText.empty() || !Core::isValidUtf8(sourceText)
          || sourceText.find('\0') != std::string::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid model source path");
      }

      const auto absolute = Fs::absolute(source);
      auto current = absolute.root_path();

      for (const auto& component : absolute.relative_path()) {
        current /= component;

        if (Fs::is_symlink(Fs::symlink_status(current))) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Model source cannot contain symlinks");
        }
      }

      auto input = Core::FileSystem::open(absolute.parent_path());

      if (!input) {
        return std::unexpected(input.error());
      }

      auto model = Assets::loadModel(*input, absolute.filename(), destination, scene);

      if (!model) {
        return std::unexpected(model.error());
      }

      auto entries = list();

      if (!entries) {
        return std::unexpected(entries.error());
      }

      const auto added = model->primitives.size() + model->materials.size() + model->images.size()
                         + 6 + model->animations.size() + (model->animations.empty() ? 0 : 1)
                         + model->skins.size() + (model->skins.empty() ? 0 : 1);

      if (added > m_impl->m_limits.entries - entries->size()) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Model bundle exceeds resource catalog capacity");
      }

      if (stop.stop_requested()) {
        return Core::fail(Core::ErrorCode::Unavailable, "Model import cancelled after decoding");
      }

      auto staging = Core::StagedDirectory::create(m_impl->m_files);

      if (!staging) {
        return std::unexpected(staging.error());
      }

      auto prepared = Assets::writeModel(staging->files().value(), *model);

      if (!prepared) {
        auto error = prepared.error();
        auto discarded = staging->discard();

        if (!discarded) {
          error.message += "; cleanup failed: " + discarded.error().message;
        }

        return std::unexpected(std::move(error));
      }

      ModelImportReport report{std::string(destination) + "/model.dotscene",
                               model->primitives.size(),
                               model->materials.size(),
                               model->images.size(),
                               model->nodes.size(),
                               model->scene,
                               std::move(model->warnings),
                               std::string(destination) + "/model.dotmodel",
                               model->animations.size(),
                               model->skins.size()};

      for (const auto& node : model->nodes) {
        report.skinnedMeshes += static_cast<std::size_t>(std::ranges::count_if(
            node.parts, [](const Assets::ModelPart& part) { return part.skin.has_value(); }));
      }

      if (stop.stop_requested()) {
        auto discarded = staging->discard();

        if (!discarded) {
          return std::unexpected(discarded.error());
        }

        return Core::fail(Core::ErrorCode::Unavailable, "Model import cancelled after writing");
      }

      return PreparedModelImport(std::make_unique<PreparedModelImport::Impl>(
          std::move(*staging), m_impl->m_files.root(), std::string(destination),
          std::move(*metadata), std::move(report), added));
    } catch (const std::exception& error) {
      return external("Import model resource", error);
    }
  }

  auto ResourceStore::publishModel(PreparedModelImport& prepared)
      -> Core::Result<ModelImportReport> {
    if (!m_impl || !prepared.m_impl || prepared.m_impl->published) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Model store/preparation was moved, discarded or published");
    }

    try {
      auto& candidate = *prepared.m_impl;
      auto target = m_impl->resolve(candidate.destination, true, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      auto metadata = m_impl->m_files.readText("project.dotproject", 64 * 1024);

      if (candidate.root != m_impl->m_files.root() || !metadata
          || *metadata != candidate.metadata) {
        return Core::fail(Core::ErrorCode::InvalidState, "Prepared model project/metadata changed");
      }

      if (Fs::exists(*target)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Model destination already exists");
      }

      auto entries = list();

      if (!entries) {
        return std::unexpected(entries.error());
      }

      if (candidate.entries > m_impl->m_limits.entries - entries->size()) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Model bundle exceeds current resource capacity");
      }

      const auto relative = path(candidate.destination);

      if (static_cast<std::size_t>(std::distance(relative.begin(), relative.end()))
          >= m_impl->m_limits.depth) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Generated model exceeds current resource depth");
      }

      auto parent = Core::FileSystem::open(target->parent_path());

      if (!parent) {
        return std::unexpected(parent.error());
      }

      auto report = candidate.report;
      auto published = candidate.staging.publish(*parent, target->filename());

      if (!published) {
        return std::unexpected(published.error());
      }

      candidate.published = true;
      return report;
    } catch (const std::exception& error) {
      return external("Publish model resource", error);
    }
  }

  auto ResourceStore::createBinary(std::string_view name, std::span<const std::byte> bytes)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      if (!name.starts_with("assets/") || textPath(name) || bytes.empty()
          || bytes.size() > m_impl->m_limits.binaryBytes) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid binary asset path/size");
      }

      auto target = m_impl->resolve(name, true, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      return m_impl->m_files.createAtomic(path(name), bytes);
    } catch (const std::exception& error) {
      return external("Create binary asset", error);
    }
  }

  auto ResourceStore::importModel(const Fs::path& source, std::string_view destination,
                                  std::optional<std::uint32_t> scene)
      -> Core::Result<ModelImportReport> {
    auto prepared = prepareModel(source, destination, scene);
    return prepared ? publishModel(*prepared) : std::unexpected(prepared.error());
  }

  auto ResourceStore::importFile(const Fs::path& source, std::string_view destination)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      auto target = m_impl->resolve(destination, true, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      if (Fs::exists(*target)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Resource destination already exists");
      }

      const auto sourceText = utf(source);

      if (sourceText.empty() || sourceText.find('\0') != std::string::npos
          || !Core::isValidUtf8(sourceText)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Resource import source must be a nonempty UTF-8 path without NUL");
      }

      const auto absolute = Fs::absolute(source);
      auto current = absolute.root_path();

      for (const auto& component : absolute.relative_path()) {
        current /= component;
        const auto status = Fs::symlink_status(current);

        if (Fs::is_symlink(status)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Resource import source cannot contain symlinks");
        }
      }

      if (!Fs::is_regular_file(Fs::symlink_status(absolute))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Resource import source must be a regular file");
      }

      auto files = Core::FileSystem::open(absolute.parent_path());

      if (!files) {
        return std::unexpected(files.error());
      }

      const bool text = textPath(destination);
      auto bytes = files->readBytes(
          absolute.filename(), text ? m_impl->m_limits.textBytes : m_impl->m_limits.binaryBytes);

      if (!bytes) {
        return std::unexpected(bytes.error());
      }

      if (text) {
        auto valid = validText(
            bytes->empty()
                ? std::string_view{}
                : std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()),
            m_impl->m_limits.textBytes);

        if (!valid) {
          return valid;
        }
      }

      auto created = m_impl->m_files.createAtomic(path(destination), *bytes);
      return created ? created
                     : Core::Result<void>{Core::fail(created.error().code,
                                                     "Import resource " + std::string(destination)
                                                         + ": " + created.error().message)};
    } catch (const std::exception& error) {
      return external("Import resource", error);
    }
  }

  auto ResourceStore::openText(std::string_view name) const -> Core::Result<TextDocument> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    try {
      auto target = m_impl->resolve(name);

      if (!target) {
        return std::unexpected(target.error());
      }

      if (!textPath(name) || !Fs::is_regular_file(*target)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Resource must be a supported regular text file");
      }

      auto text = m_impl->m_files.readText(path(name), m_impl->m_limits.textBytes);

      if (!text) {
        return std::unexpected(text.error());
      }

      auto valid = validText(*text, m_impl->m_limits.textBytes);
      return valid ? Core::Result<TextDocument>{TextDocument(std::make_unique<TextDocument::Impl>(
                         m_impl, std::string(name), std::move(*text)))}
                   : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return external("Open resource text", error);
    }
  }

  auto ResourceStore::externalTextPath(std::string_view name) const -> Core::Result<Fs::path> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource store was moved");
    }

    if (!textPath(name)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "External resource must be text");
    }

    try {
      auto target = m_impl->resolve(name, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      std::error_code error;

      if (!Fs::is_regular_file(*target, error) || error) {
        return Core::fail(Core::ErrorCode::Io, "External resource must be a regular file");
      }

      return target;
    } catch (const std::exception& error) {
      return external("Resolve external resource", error);
    }
  }

  TextDocument::TextDocument(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  TextDocument::TextDocument(TextDocument&&) noexcept = default;

  auto TextDocument::operator=(TextDocument&&) noexcept -> TextDocument& = default;

  TextDocument::~TextDocument() = default;

  auto TextDocument::snapshot() const -> Core::Result<TextSnapshot> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource text document was moved");
    }

    auto checked = m_impl->m_store->owner();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      return TextSnapshot{m_impl->m_path, m_impl->m_text, m_impl->m_revision,
                          m_impl->m_text != m_impl->m_baseline, m_impl->m_readOnly};
    } catch (const std::exception& error) {
      return external("Snapshot resource text", error);
    }
  }

  auto TextDocument::replace(std::string_view text, std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource text document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    auto valid = validText(text, m_impl->m_store->m_limits.textBytes);

    if (!valid) {
      return valid;
    }

    if (text == m_impl->m_text) {
      return {};
    }

    if (m_impl->m_revision == std::numeric_limits<std::uint64_t>::max()) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource revision exhausted");
    }

    try {
      std::string owned(text);
      m_impl->m_text.swap(owned);
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return external("Replace resource text", error);
    }
  }

  auto TextDocument::save(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Resource text document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    try {
      auto target = m_impl->m_store->resolve(m_impl->m_path, true);

      if (!target) {
        return std::unexpected(target.error());
      }

      auto current = m_impl->m_store->m_files.readText(path(m_impl->m_path),
                                                       m_impl->m_store->m_limits.textBytes);

      if (!current) {
        return std::unexpected(current.error());
      }

      if (*current != m_impl->m_baseline) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Resource file changed externally; save conflict");
      }

      if (m_impl->m_text == m_impl->m_baseline) {
        return {};
      }

      if (m_impl->m_revision == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Resource revision exhausted");
      }

      auto baseline = m_impl->m_text;
      auto written = m_impl->m_store->m_files.writeTextAtomic(path(m_impl->m_path), baseline);

      if (!written) {
        return written;
      }

      m_impl->m_baseline.swap(baseline);
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return external("Save resource text", error);
    }
  }
}  // namespace Dot::Editor
