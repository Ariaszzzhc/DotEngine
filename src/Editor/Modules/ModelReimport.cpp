module;

#include <algorithm>
#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../../Engine/Serialization/Json.hpp"

module Dot.Editor.ModelReimport;

import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Core.StagedDirectory;
import Dot.Core.Text;
import Dot.Editor.Resources;
import Dot.Assets.Model;
import Dot.Project.ScenePreparation;

namespace Dot::Editor {
  namespace {
    namespace Fs = std::filesystem;
    using Json = nlohmann::json;
    namespace Codec = Serialization::Detail;
    using Bytes = std::vector<std::byte>;
    using Snapshot = std::map<std::string, std::optional<Bytes>>;
    constexpr std::size_t snapshotLimit = 512ULL * 1024 * 1024;

    auto path(const std::string& name) -> Fs::path {
      return Fs::path(std::u8string(name.begin(), name.end()));
    }

    auto utf(const Fs::path& name) -> std::string {
      const auto bytes = name.generic_u8string();
      return {bytes.begin(), bytes.end()};
    }

    auto cancelled(std::stop_token stop) -> Core::Result<void> {
      return stop.stop_requested()
                 ? Core::fail(Core::ErrorCode::Unavailable, "Model reimport cancelled")
                 : Core::Result<void>{};
    }

    auto capture(const Project::Project& project, std::stop_token stop) -> Core::Result<Snapshot> {
      auto store = ResourceStore::open(project, {64 * 1024 * 1024});

      if (!store) {
        return std::unexpected(store.error());
      }

      auto entries = store->list();
      auto files = project.files();

      if (!entries || !files) {
        return std::unexpected(!entries ? entries.error() : files.error());
      }

      entries->push_back({"project.dotproject", 0, false, true, false});
      Snapshot result;
      std::size_t total{};

      for (const auto& entry : *entries) {
        auto active = cancelled(stop);

        if (!active) {
          return std::unexpected(active.error());
        }

        if (entry.directory) {
          result.emplace(entry.path, std::nullopt);
          continue;
        }

        auto bytes = files->readBytes(path(entry.path));

        if (!bytes) {
          return std::unexpected(bytes.error());
        }

        if (bytes->size() > snapshotLimit - total) {
          return Core::fail(Core::ErrorCode::Unavailable, "Reimport project exceeds 512 MiB");
        }

        total += bytes->size();
        result.emplace(entry.path, std::move(*bytes));
      }

      return result;
    }

    auto write(const Core::FileSystem& files, const std::string& name, const Bytes& bytes)
        -> Core::Result<void> {
      const auto relative = path(name);
      auto parents = relative.parent_path().empty()
                         ? Core::Result<void>{}
                         : files.createDirectories(relative.parent_path());
      return parents ? files.writeAtomic(relative, bytes) : parents;
    }

    auto signature(const Core::FileSystem& files, const std::string& destination)
        -> Core::Result<Json> {
      auto descriptor = files.readText(path(destination + "/model.dotmodel"), 1024 * 1024);
      auto scene = files.readText(path(destination + "/model.dotscene"));

      if (!descriptor || !scene) {
        return std::unexpected(!descriptor ? descriptor.error() : scene.error());
      }

      auto result = Json{{"model", Codec::parseJson(*descriptor)}, {"entities", Json::array()}};
      const auto document = Codec::parseJson(*scene);

      for (const auto& entity : document.at("entities")) {
        Json node;

        for (const auto* key : {"id", "name", "parent", "material", "skin", "animation"}) {
          node[key] = entity.at(key);
        }

        const auto& mesh = entity.at("mesh");
        node["mesh"] = mesh.is_null()
                           ? Json(nullptr)
                           : Json{{"kind", mesh.at("kind")}, {"asset", mesh.at("asset")}};
        result["entities"].push_back(std::move(node));
      }

      return result;
    }
  }  // namespace

  struct ModelReimport::Impl {
    struct Change {
      ReimportChange info;
      Bytes source;
    };

    std::thread::id m_owner{std::this_thread::get_id()};
    Project::Project m_project;
    Fs::path m_source;
    std::string m_destination;
    std::optional<std::uint32_t> m_scene;
    ReimportState m_state{ReimportState::Preparing};
    std::uint64_t m_revision{1};
    std::string m_error;
    std::vector<std::string> m_warnings;
    Snapshot m_baseline;
    Snapshot m_applied;
    std::vector<Change> m_changes;
    std::optional<Core::StagedDirectory> m_candidate;
    std::optional<Core::StagedDirectory> m_previous;
    std::optional<Core::StagedDirectory> m_publication;
    std::mutex m_mutex;
    bool m_done{};
    std::optional<Core::Error> m_workerError;
    std::jthread m_worker;

    Impl(Project::Project project, Fs::path source, std::string destination,
         std::optional<std::uint32_t> scene)
        : m_project(std::move(project)),
          m_source(std::move(source)),
          m_destination(std::move(destination)),
          m_scene(scene) {
    }

    auto poll() -> Core::Result<void> {
      if (m_owner != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Reimport requires owner thread");
      }

      std::unique_lock lock(m_mutex);

      if (!m_done) {
        return {};
      }

      lock.unlock();
      m_worker.join();
      m_done = false;
      m_error = m_workerError ? m_workerError->message : "";
      m_state = m_workerError ? (m_state == ReimportState::Preparing ? ReimportState::Failed
                                                                     : ReimportState::Review)
                              : (m_state == ReimportState::Preparing ? ReimportState::Review
                                                                     : ReimportState::Ready);
      ++m_revision;
      return {};
    }

    auto start(bool prepare) -> void {
      m_done = false;
      m_workerError.reset();
      m_worker = std::jthread([this, prepare](std::stop_token stop) {
        auto result = [&]() -> Core::Result<void> {
          try {
            return prepare ? build(stop) : preflight(stop);
          } catch (const std::exception& error) {
            return Core::fail(Core::ErrorCode::External,
                              "Model reimport worker: " + std::string(error.what()));
          }
        }();
        std::lock_guard lock(m_mutex);

        if (!result) {
          m_workerError = std::move(result.error());
        }

        m_done = true;
      });
    }

    auto build(std::stop_token stop) -> Core::Result<void> {
      auto current = Project::Project::open(m_project.files()->root());

      if (!current) {
        return std::unexpected(current.error());
      }

      if (current->description().value() != m_project.description().value()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Reimport project metadata changed");
      }

      auto original = capture(*current, stop);

      if (!original) {
        return std::unexpected(original.error());
      }

      m_baseline = std::move(*original);
      const auto sceneName = m_destination + "/model.dotscene";

      if (!m_baseline.contains(m_destination) || m_baseline.at(m_destination)
          || !m_baseline.contains(sceneName)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Reimport requires an existing native model bundle directory");
      }

      const auto sourceName = utf(m_source);

      if (sourceName.empty() || sourceName.find('\0') != std::string::npos
          || !Core::isValidUtf8(sourceName)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid reimport source path");
      }

      const auto absolute = Fs::absolute(m_source);
      auto component = absolute.root_path();

      for (const auto& part : absolute.relative_path()) {
        component /= part;

        if (Fs::is_symlink(Fs::symlink_status(component))) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Reimport source contains a symlink");
        }
      }

      auto input = Core::FileSystem::open(absolute.parent_path());

      if (!input) {
        return std::unexpected(input.error());
      }

      auto model = Assets::loadModel(*input, absolute.filename(), m_destination, m_scene);

      if (!model) {
        return std::unexpected(model.error());
      }

      auto active = cancelled(stop);

      if (!active) {
        return active;
      }

      m_warnings = model->warnings;
      auto storage = Core::StagedDirectory::create(m_project.files().value());

      if (!storage) {
        return std::unexpected(storage.error());
      }

      m_candidate.emplace(std::move(*storage));
      auto candidate = m_candidate->files().value();

      for (const auto& [name, bytes] : m_baseline) {
        active = cancelled(stop);

        if (!active) {
          return active;
        }

        auto copied
            = bytes ? write(candidate, name, *bytes) : candidate.createDirectories(path(name));

        if (!copied) {
          return copied;
        }
      }

      auto before = signature(candidate, m_destination);

      if (!before) {
        return std::unexpected(before.error());
      }

      auto generated = Core::StagedDirectory::create(candidate);

      if (!generated) {
        return std::unexpected(generated.error());
      }

      auto converted = Assets::writeModel(generated->files().value(), *model);

      if (!converted) {
        return converted;
      }

      const auto native = generated->files().value();
      std::size_t total{};
      std::size_t count{};

      for (const auto& entry : Fs::recursive_directory_iterator(native.root())) {
        active = cancelled(stop);

        if (!active) {
          return active;
        }

        if (++count > 8192) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Generated reimport exceeds 8192 entries");
        }

        if (entry.is_directory()) {
          continue;
        }

        const auto relative = entry.path().lexically_relative(native.root());
        const auto name = m_destination + "/" + utf(relative);
        auto bytes = native.readBytes(relative);

        if (!bytes) {
          return std::unexpected(bytes.error());
        }

        if (bytes->size() > snapshotLimit - total) {
          return Core::fail(Core::ErrorCode::Unavailable, "Generated reimport exceeds 512 MiB");
        }

        total += bytes->size();
        const auto old = m_baseline.find(name);

        if (old != m_baseline.end() && (!old->second || *old->second == *bytes)) {
          if (!old->second) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Reimport file collides with directory: " + name);
          }

          continue;
        }

        auto copied = write(candidate, name, *bytes);

        if (!copied) {
          return copied;
        }

        const bool added = old == m_baseline.end();
        m_changes.push_back({{name, added ? 0 : old->second->size(), bytes->size(), added,
                              added ? ReimportChoice::Source : ReimportChoice::Unresolved},
                             std::move(*bytes)});
      }

      std::ranges::sort(m_changes, {}, [](const Change& change) { return change.info.path; });
      auto after = signature(candidate, m_destination);

      if (!before || !after) {
        return std::unexpected(!before ? before.error() : after.error());
      }

      if (*before != *after) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Model node identity, hierarchy or bindings changed; stable-layout reimport rejected");
      }

      return {};
    }

    auto preflight(std::stop_token stop) -> Core::Result<void> {
      const auto candidate = m_candidate->files().value();

      for (const auto& change : m_changes) {
        auto active = cancelled(stop);

        if (!active) {
          return active;
        }

        const auto& bytes = change.info.choice == ReimportChoice::Source
                                ? change.source
                                : *m_baseline.at(change.info.path);
        auto written = write(candidate, change.info.path, bytes);

        if (!written) {
          return written;
        }
      }

      auto project = Project::Project::open(candidate.root());

      if (!project) {
        return std::unexpected(project.error());
      }

      auto snapshot = capture(*project, stop);

      if (!snapshot) {
        return std::unexpected(snapshot.error());
      }

      for (const auto& [name, bytes] : *snapshot) {
        if (bytes && name.ends_with(".dotscene")) {
          auto proof = Project::PreparedScene::create(candidate, name, {}, stop);

          if (!proof) {
            auto error = proof.error();
            error.message = "Reimport candidate scene '" + name + "': " + error.message;
            return std::unexpected(std::move(error));
          }
        }
      }

      auto publication = Core::StagedDirectory::create(m_project.files().value());

      if (!publication) {
        return std::unexpected(publication.error());
      }

      const auto output = publication->files().value();
      const auto prefix = m_destination + "/";

      for (const auto& [name, bytes] : *snapshot) {
        if (!name.starts_with(prefix)) {
          continue;
        }

        auto active = cancelled(stop);

        if (!active) {
          return active;
        }

        const auto relative = name.substr(prefix.size());
        auto copied
            = bytes ? write(output, relative, *bytes) : output.createDirectories(path(relative));

        if (!copied) {
          return copied;
        }
      }

      m_publication.emplace(std::move(*publication));
      m_applied = std::move(*snapshot);
      return {};
    }

    auto check(std::uint64_t revision, ReimportState state) -> Core::Result<void> {
      auto polled = poll();

      if (!polled) {
        return polled;
      }

      if (revision != m_revision || state != m_state) {
        return Core::fail(Core::ErrorCode::InvalidState, "Reimport state or revision changed");
      }

      return {};
    }

    auto baseline(const Snapshot& expected) -> Core::Result<void> {
      auto current = Project::Project::open(m_project.files()->root());

      if (!current) {
        return std::unexpected(current.error());
      }

      auto actual = capture(*current, {});

      if (!actual) {
        return std::unexpected(actual.error());
      }

      return *actual == expected
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Project files changed since reimport review; prepare a new review");
    }
  };

  ModelReimport::ModelReimport(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  ModelReimport::ModelReimport(ModelReimport&&) noexcept = default;

  auto ModelReimport::operator=(ModelReimport&&) noexcept -> ModelReimport& = default;

  ModelReimport::~ModelReimport() = default;

  auto ModelReimport::begin(const Project::Project& project, const Fs::path& source,
                            std::string destination, std::optional<std::uint32_t> scene)
      -> Core::Result<ModelReimport> {
    if (!Core::isPortableRelativePath(destination) || !destination.starts_with("assets/")
        || (scene && *scene > 1023)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid reimport destination or source scene");
    }

    auto files = project.files();

    if (!files) {
      return std::unexpected(files.error());
    }

    try {
      auto impl = std::make_unique<Impl>(project, source, std::move(destination), scene);
      impl->start(true);
      return ModelReimport(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Begin reimport: " + std::string(error.what()));
    }
  }

  auto ModelReimport::status(std::size_t offset, std::size_t limit)
      -> Core::Result<ReimportStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Reimport review was moved");
    }

    auto polled = m_impl->poll();

    if (!polled) {
      return std::unexpected(polled.error());
    }

    if (limit == 0 || limit > 256 || offset > 8192) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Reimport page requires offset <=8192 and limit 1..256");
    }

    ReimportStatus result{
        m_impl->m_state, m_impl->m_revision, m_impl->m_destination, m_impl->m_error, 0, {}, {}};

    if (result.state == ReimportState::Preparing || result.state == ReimportState::Validating) {
      return result;
    }

    result.count = m_impl->m_changes.size();
    result.warnings = m_impl->m_warnings;

    for (auto index = offset; index < std::min(offset + limit, result.count); ++index) {
      result.changes.push_back(m_impl->m_changes[index].info);
    }

    return result;
  }

  auto ModelReimport::file(std::string name, std::uint64_t revision) -> Core::Result<ReimportFile> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Reimport review was moved");
    }

    auto polled = m_impl->poll();

    if (!polled) {
      return std::unexpected(polled.error());
    }

    if (revision != m_impl->m_revision || m_impl->m_state == ReimportState::Preparing
        || m_impl->m_state == ReimportState::Validating) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Reimport file requires completed current review");
    }

    const auto found = std::ranges::find(
        m_impl->m_changes, name, [](const Impl::Change& change) { return change.info.path; });

    if (found == m_impl->m_changes.end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Unknown reimport file");
    }

    const auto old = m_impl->m_baseline.find(name);
    const auto view = [](const Bytes& bytes) {
      return bytes.empty()
                 ? std::string_view{}
                 : std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    const auto before = old == m_impl->m_baseline.end() ? std::string_view{} : view(*old->second);
    const auto after = view(found->source);
    ReimportFile result;
    result.text = before.find('\0') == std::string::npos && after.find('\0') == std::string::npos
                  && Core::isValidUtf8(before) && Core::isValidUtf8(after);

    if (result.text) {
      result.truncated = before.size() > 16384 || after.size() > 16384;
      const auto bounded = [](std::string_view value) {
        std::string text(value.substr(0, 16384));

        while (!Core::isValidUtf8(text)) {
          text.pop_back();
        }

        return text;
      };
      result.before = bounded(before);
      result.after = bounded(after);
    }

    return result;
  }

  auto ModelReimport::choose(std::string name, ReimportChoice choice, std::uint64_t revision)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Reimport review was moved");
    }

    auto checked = m_impl->check(revision, ReimportState::Review);

    if (!checked) {
      return checked;
    }

    auto found = std::ranges::find(m_impl->m_changes, name,
                                   [](const Impl::Change& change) { return change.info.path; });

    if (found == m_impl->m_changes.end() || choice == ReimportChoice::Unresolved
        || (choice != ReimportChoice::Source && choice != ReimportChoice::Project)
        || (found->info.added && choice == ReimportChoice::Project)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Unknown reimport file or invalid choice");
    }

    if (found->info.choice != choice) {
      found->info.choice = choice;
      m_impl->m_error.clear();
      ++m_impl->m_revision;
    }

    return {};
  }

  auto ModelReimport::validate(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Reimport review was moved");
    }

    auto checked = m_impl->check(revision, ReimportState::Review);

    if (!checked) {
      return checked;
    }

    if (std::ranges::any_of(m_impl->m_changes, [](const Impl::Change& change) {
          return change.info.choice == ReimportChoice::Unresolved;
        })) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Resolve every changed file before validation");
    }

    try {
      m_impl->start(false);
      m_impl->m_state = ReimportState::Validating;
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Start reimport validation: " + std::string(error.what()));
    }
  }

  auto ModelReimport::apply(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Reimport review was moved");
    }

    auto checked = m_impl->check(revision, ReimportState::Ready);

    if (!checked) {
      return checked;
    }

    try {
      checked = m_impl->baseline(m_impl->m_baseline);

      if (!checked) {
        return checked;
      }

      const auto target = m_impl->m_project.files()->root() / path(m_impl->m_destination);
      auto parent = Core::FileSystem::open(target.parent_path());

      if (!parent) {
        return std::unexpected(parent.error());
      }

      auto previous = m_impl->m_publication->replace(*parent, target.filename());

      if (!previous) {
        return std::unexpected(previous.error());
      }

      m_impl->m_previous.emplace(std::move(*previous));
      m_impl->m_state = ReimportState::Applied;
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Apply reimport: " + std::string(error.what()));
    }
  }

  auto ModelReimport::revert(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Reimport review was moved");
    }

    auto checked = m_impl->check(revision, ReimportState::Applied);

    if (!checked) {
      return checked;
    }

    try {
      checked = m_impl->baseline(m_impl->m_applied);

      if (!checked) {
        return checked;
      }

      const auto target = m_impl->m_project.files()->root() / path(m_impl->m_destination);
      auto parent = Core::FileSystem::open(target.parent_path());

      if (!parent) {
        return std::unexpected(parent.error());
      }

      auto displaced = m_impl->m_previous->replace(*parent, target.filename());

      if (!displaced) {
        return std::unexpected(displaced.error());
      }

      m_impl->m_previous.reset();
      m_impl->m_state = ReimportState::Reverted;
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Revert reimport: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Editor
