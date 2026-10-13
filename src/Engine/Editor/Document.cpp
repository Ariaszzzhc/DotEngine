module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Editor.Document;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Editor {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr auto exhausted = std::numeric_limits<std::uint64_t>::max();

    auto input(std::string_view source) -> Core::Result<void> {
      if (source.empty() || source.size() > 1024 * 1024
          || source.find('\0') != std::string_view::npos || !Core::isValidUtf8(source)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Document JSON requires nonempty UTF-8 without NUL, at most 1 MiB");
      }

      return {};
    }

    auto find(Json& entities, std::uint64_t id) -> Json* {
      auto found = std::ranges::find_if(
          entities, [id](const auto& value) { return Codec::id(value["id"]) == id; });
      return found == entities.end() ? nullptr : &*found;
    }

    auto operation(Json& scene, const Json& command, std::uint64_t& next,
                   std::vector<std::uint64_t>& created) -> Core::Result<void> {
      if (!command.is_object() || !command.contains("op") || !command["op"].is_string()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Edit operation requires an op string");
      }

      const auto op = command["op"].get<std::string>();
      auto& entities = scene["entities"];

      if (op == "create") {
        Codec::requireFields(command, {"op", "name"});

        if (next == exhausted || entities.size() >= 10000) {
          return Core::fail(Core::ErrorCode::Unavailable, "Editor entity capacity exhausted");
        }

        entities.push_back(
            {{"id", std::to_string(next)},
             {"name", command["name"]},
             {"parent", nullptr},
             {"transform",
              {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
             {"camera", nullptr},
             {"mesh", nullptr},
             {"material", nullptr},
             {"light", nullptr},
             {"rigidBody", nullptr},
             {"audioSource", nullptr},
             {"audioListener", nullptr},
             {"script", nullptr}});
        created.push_back(next++);
        return {};
      }

      if (op == "scene-name") {
        Codec::requireFields(command, {"op", "name"});
        scene["name"] = command["name"];
        return {};
      }

      if (op == "remove") {
        Codec::requireFields(command, {"op", "entity"});
      } else if (op == "name") {
        Codec::requireFields(command, {"op", "entity", "name"});
      } else if (op == "parent") {
        Codec::requireFields(command, {"op", "entity", "parent"});
      } else if (op == "transform") {
        Codec::requireFields(command, {"op", "entity", "value"});
      } else if (op == "component") {
        Codec::requireFields(command, {"op", "entity", "component", "value"});
      } else {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown edit operation: " + op);
      }

      const auto id = Codec::id(command["entity"]);
      auto* entity = find(entities, id);

      if (!entity) {
        return Core::fail(Core::ErrorCode::NotFound,
                          "Edit entity is missing: " + std::to_string(id));
      }

      if (op == "remove") {
        std::unordered_set<std::uint64_t> removed{id};
        bool changed = true;

        while (changed) {
          changed = false;

          for (const auto& value : entities) {
            if (!value["parent"].is_null() && removed.contains(Codec::id(value["parent"]))) {
              changed = removed.insert(Codec::id(value["id"])).second || changed;
            }
          }
        }

        auto& values = entities.get_ref<Json::array_t&>();
        std::erase_if(values,
                      [&](const auto& value) { return removed.contains(Codec::id(value["id"])); });
      } else if (op == "name") {
        (*entity)["name"] = command["name"];
      } else if (op == "parent") {
        if (!command["parent"].is_null() && !find(entities, Codec::id(command["parent"]))) {
          return Core::fail(Core::ErrorCode::NotFound, "Edit parent is missing");
        }

        (*entity)["parent"] = command["parent"];
      } else if (op == "transform") {
        (*entity)["transform"] = command["value"];
      } else {
        const auto component = command["component"].get<std::string>();
        constexpr std::array components{"camera",        "mesh",   "rigidBody", "audioSource",
                                        "audioListener", "script", "material",  "light"};

        if (std::ranges::find(components, component) == components.end()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unknown edit component: " + component);
        }

        (*entity)[component] = command["value"];
      }

      return {};
    }
  }  // namespace

  struct Document::Impl {
    struct Entry {
      std::shared_ptr<const std::string> text;
      std::optional<std::uint64_t> selected;
    };

    Project::Project m_project;
    World::Scene m_scene;
    DocumentConfig m_config;
    std::thread::id m_thread{std::this_thread::get_id()};
    std::uint64_t m_revision{1};
    std::uint64_t m_next{1};
    std::vector<Entry> m_history;
    std::size_t m_cursor{};
    std::uint64_t m_bytes{};
    std::shared_ptr<const std::string> m_saved;

    Impl(Project::Project project, World::Scene scene, DocumentConfig config,
         std::shared_ptr<const std::string> text, std::uint64_t next)
        : m_project(std::move(project)),
          m_scene(std::move(scene)),
          m_config(config),
          m_next(next),
          m_history{{text, {}}},
          m_bytes(text->size()),
          m_saved(std::move(text)) {
    }

    auto check() const -> Core::Result<void> {
      if (m_thread != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor document requires its owner thread");
      }

      return {};
    }

    auto check(std::uint64_t revision) const -> Core::Result<void> {
      auto checked = check();

      if (!checked) {
        return checked;
      }

      if (revision != m_revision) {
        return Core::fail(Core::ErrorCode::InvalidState, "Stale editor document revision");
      }

      if (m_revision == exhausted - 1) {
        return Core::fail(Core::ErrorCode::Unavailable, "Editor document revision exhausted");
      }

      return {};
    }

    auto restore(std::size_t cursor) -> Core::Result<void> {
      auto scene = World::decodeScene(*m_history[cursor].text);

      if (!scene) {
        return std::unexpected(scene.error());
      }

      m_scene = std::move(*scene);
      m_cursor = cursor;
      ++m_revision;
      return {};
    }
  };

  Document::Document(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Document::Document(Document&&) noexcept = default;

  auto Document::operator=(Document&&) noexcept -> Document& = default;

  Document::~Document() = default;

  auto Document::open(const Project::Project& project, const DocumentConfig& config)
      -> Core::Result<Document> {
    if (config.historyEntries == 0 || config.historyEntries > 1024 || config.historyBytes == 0
        || config.historyBytes > 512 * 1024 * 1024) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid editor history capacity");
    }

    try {
      auto scene = project.loadStartup();

      if (!scene) {
        return std::unexpected(scene.error());
      }

      auto text = World::encodeScene(*scene);

      if (!text) {
        return std::unexpected(text.error());
      }

      if (text->size() > config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Startup scene exceeds editor history budget");
      }

      std::uint64_t next = 1;
      auto entities = scene->entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      for (const auto& entity : *entities) {
        next = std::max(next, entity.id.value + 1);
      }

      return Document(std::make_unique<Impl>(project, std::move(*scene), config,
                                             std::make_shared<const std::string>(std::move(*text)),
                                             next));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Open editor document: " + std::string(error.what()));
    }
  }

  auto Document::scene() const -> Core::Result<const World::Scene*> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check();
    return checked ? Core::Result<const World::Scene*>{&m_impl->m_scene}
                   : std::unexpected(checked.error());
  }

  auto Document::stats() const -> Core::Result<DocumentStats> {
    auto scene = this->scene();

    if (!scene) {
      return std::unexpected(scene.error());
    }

    const auto& entry = m_impl->m_history[m_impl->m_cursor];
    std::optional<World::EntityId> selected;

    if (entry.selected) {
      selected = World::EntityId{(*scene)->id(), *entry.selected};
    }

    return DocumentStats{m_impl->m_revision,
                         (*scene)->id(),
                         selected,
                         m_impl->m_next == exhausted ? std::nullopt : std::optional{m_impl->m_next},
                         *entry.text != *m_impl->m_saved,
                         m_impl->m_cursor > 0,
                         m_impl->m_cursor + 1 < m_impl->m_history.size(),
                         m_impl->m_history.size(),
                         m_impl->m_bytes};
  }

  auto Document::snapshot() const -> Core::Result<std::string> {
    auto stats = this->stats();

    if (!stats) {
      return std::unexpected(stats.error());
    }

    try {
      return Json{{"revision", std::to_string(stats->revision)},
                  {"scope", std::to_string(stats->scene)},
                  {"selected",
                   stats->selected ? Json(std::to_string(stats->selected->value)) : Json(nullptr)},
                  {"nextEntity",
                   stats->nextEntity ? Json(std::to_string(*stats->nextEntity)) : Json(nullptr)},
                  {"dirty", stats->dirty},
                  {"canUndo", stats->canUndo},
                  {"canRedo", stats->canRedo},
                  {"historyEntries", stats->historyEntries},
                  {"historyBytes", std::to_string(stats->historyBytes)},
                  {"scene", Codec::parseJson(*m_impl->m_history[m_impl->m_cursor].text)}}
          .dump();
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Snapshot editor document: " + std::string(error.what()));
    }
  }

  auto Document::apply(std::string_view operations, std::uint64_t revision)
      -> Core::Result<EditReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto valid = input(operations);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      auto commands = Codec::parseJson(operations);

      if (!commands.is_array() || commands.empty() || commands.size() > 256) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Edit requires 1..256 operations");
      }

      auto candidate = Codec::parseJson(*m_impl->m_history[m_impl->m_cursor].text);
      auto next = m_impl->m_next;
      std::vector<std::uint64_t> created;

      for (std::size_t index = 0; index < commands.size(); ++index) {
        auto result = operation(candidate, commands[index], next, created);

        if (!result) {
          return Core::fail(result.error().code, "Edit operation " + std::to_string(index) + ": "
                                                     + result.error().message);
        }
      }

      auto prepared = World::decodeScene(candidate.dump());

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      auto encoded = World::encodeScene(*prepared);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      if (*encoded == *m_impl->m_history[m_impl->m_cursor].text) {
        return EditReport{m_impl->m_revision, m_impl->m_scene.id(), false, {}};
      }

      if (encoded->size() > m_impl->m_config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Edited scene exceeds editor history budget");
      }

      std::erase_if(created, [&](auto id) { return !prepared->findEntity(id); });
      auto selected = m_impl->m_history[m_impl->m_cursor].selected;

      if (selected && !prepared->findEntity(*selected)) {
        selected.reset();
      }

      auto text = std::make_shared<const std::string>(std::move(*encoded));
      std::vector<Impl::Entry> history(
          m_impl->m_history.begin(),
          m_impl->m_history.begin() + static_cast<std::ptrdiff_t>(m_impl->m_cursor + 1));
      history.push_back({std::move(text), selected});
      std::uint64_t bytes = 0;

      for (const auto& entry : history) {
        bytes += entry.text->size();
      }

      while (history.size() > m_impl->m_config.historyEntries
             || bytes > m_impl->m_config.historyBytes) {
        bytes -= history.front().text->size();
        history.erase(history.begin());
      }

      /** All allocating preparation precedes this nonthrowing commit.
       * 全部分配准备在此无异常提交之前完成。 */
      m_impl->m_scene = std::move(*prepared);
      m_impl->m_history = std::move(history);
      m_impl->m_cursor = m_impl->m_history.size() - 1;
      m_impl->m_bytes = bytes;
      m_impl->m_next = next;
      ++m_impl->m_revision;
      return EditReport{m_impl->m_revision, m_impl->m_scene.id(), true, std::move(created)};
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Edit JSON: " + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Edit command: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare editor edit: " + std::string(error.what()));
    }
  }

  auto Document::select(std::optional<World::EntityId> entity, std::uint64_t revision)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (entity) {
      auto found = m_impl->m_scene.entity(*entity);

      if (!found) {
        return std::unexpected(found.error());
      }
    }

    auto selected = entity ? std::optional{entity->value} : std::nullopt;

    if (selected != m_impl->m_history[m_impl->m_cursor].selected) {
      m_impl->m_history[m_impl->m_cursor].selected = selected;
      ++m_impl->m_revision;
    }

    return {};
  }

  auto Document::undo(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (m_impl->m_cursor == 0) {
      return Core::fail(Core::ErrorCode::Unavailable, "Editor undo history is empty");
    }

    try {
      return m_impl->restore(m_impl->m_cursor - 1);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Restore editor undo: " + std::string(error.what()));
    }
  }

  auto Document::redo(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (m_impl->m_cursor + 1 == m_impl->m_history.size()) {
      return Core::fail(Core::ErrorCode::Unavailable, "Editor redo history is empty");
    }

    try {
      return m_impl->restore(m_impl->m_cursor + 1);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Restore editor redo: " + std::string(error.what()));
    }
  }

  auto Document::save(std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    try {
      auto files = m_impl->m_project.files();

      if (!files) {
        return std::unexpected(files.error());
      }

      auto project = Project::Project::open(files->root());

      if (!project) {
        return std::unexpected(project.error());
      }

      auto expected = m_impl->m_project.description();
      auto actual = project->description();

      if (!expected || !actual) {
        return std::unexpected(expected ? actual.error() : expected.error());
      }

      if (*expected != *actual) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor project metadata changed externally");
      }

      auto disk = project->loadStartup();

      if (!disk) {
        return std::unexpected(disk.error());
      }

      auto baseline = World::encodeScene(*disk);

      if (!baseline) {
        return std::unexpected(baseline.error());
      }

      if (*baseline != *m_impl->m_saved) {
        return Core::fail(Core::ErrorCode::InvalidState, "Editor startup scene changed externally");
      }

      auto saved = m_impl->m_project.saveStartup(m_impl->m_scene);

      if (!saved) {
        return saved;
      }

      m_impl->m_saved = m_impl->m_history[m_impl->m_cursor].text;
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Save editor document: " + std::string(error.what()));
    }
  }

  auto Document::dispatch(std::string_view request) -> Core::Result<std::string> {
    auto current = scene();

    if (!current) {
      return std::unexpected(current.error());
    }

    auto valid = input(request);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    bool committed = false;

    try {
      auto parsed = Codec::parseJson(request);
      const auto command = parsed.at("command").get<std::string>();
      std::vector<std::uint64_t> created;
      Core::Result<void> result;

      if (command == "inspect") {
        Codec::requireFields(parsed, {"command"});
      } else {
        if (command == "edit") {
          Codec::requireFields(parsed, {"command", "revision", "operations"});
        } else if (command == "select") {
          Codec::requireFields(parsed, {"command", "revision", "entity"});
        } else if (command == "undo" || command == "redo" || command == "save") {
          Codec::requireFields(parsed, {"command", "revision"});
        } else {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unknown document command: " + command);
        }

        const auto revision = Codec::id(parsed["revision"]);

        if (command == "edit") {
          auto applied = apply(parsed["operations"].dump(), revision);

          if (!applied) {
            return std::unexpected(applied.error());
          }

          committed = applied->changed;
          created = std::move(applied->created);
        } else if (command == "select") {
          result = select(
              parsed["entity"].is_null()
                  ? std::nullopt
                  : std::optional{World::EntityId{(*current)->id(), Codec::id(parsed["entity"])}},
              revision);
        } else if (command == "undo") {
          result = undo(revision);
        } else if (command == "redo") {
          result = redo(revision);
        } else {
          result = save(revision);
        }

        if (!result) {
          return std::unexpected(result.error());
        }

        committed = committed || revision != m_impl->m_revision;
      }

      auto snapshot = this->snapshot();

      if (!snapshot) {
        return Core::fail(
            snapshot.error().code,
            std::string(committed ? "Document committed; " : "") + snapshot.error().message);
      }

      auto response = Codec::parseJson(*snapshot);
      response["ok"] = true;
      response["created"] = Json::array();

      for (auto id : created) {
        response["created"].push_back(std::to_string(id));
      }

      return response.dump();
    } catch (const Json::exception& error) {
      return Core::fail(
          committed ? Core::ErrorCode::External : Core::ErrorCode::InvalidArgument,
          std::string(committed ? "Document committed; response JSON: " : "Document request JSON: ")
              + error.what());
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Document request: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(
          Core::ErrorCode::External,
          std::string(committed ? "Document committed; response: " : "Document request: ")
              + error.what());
    }
  }
}  // namespace Dot::Editor
