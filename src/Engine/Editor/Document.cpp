module;

#include <algorithm>
#include <array>
#include <exception>
#include <expected>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Editor.Document;

import Dot.Core.Result;
import Dot.Core.Text;
import Dot.Core.Paths;
import Dot.Core.Files;
import Dot.Editor.Resources;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneContent;

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

    auto sceneFile(std::string_view value) -> std::filesystem::path {
      return std::filesystem::path(std::u8string(value.begin(), value.end()));
    }

    auto sceneAssetPath(std::string_view value) -> bool {
      return Core::isPortableRelativePath(value) && value.ends_with(".dotscene")
             && value.find("/.dot-stage-") == std::string_view::npos
             && (value.starts_with("scenes/") || value.starts_with("assets/"));
    }

    auto find(Json& entities, std::uint64_t id) -> Json* {
      auto found = std::ranges::find_if(
          entities, [id](const auto& value) { return Codec::id(value["id"]) == id; });
      return found == entities.end() ? nullptr : &*found;
    }

    /** Expand subtrees once, remap internal parents and publish only through apply.
     * 子树只扩展一次，重映射内部父引用，最终仅通过 apply 事务发布。 */
    auto groupOperation(Json& scene, const Json& command, std::uint64_t& next,
                        std::vector<std::uint64_t>& created, std::vector<std::uint64_t>& selection)
        -> Core::Result<void> {
      Codec::requireFields(command, {"op", "entities"});
      const auto& requested = command["entities"];

      if (!requested.is_array() || requested.empty() || requested.size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Group edit requires 1..10000 entities");
      }

      auto& entities = scene["entities"];
      std::map<std::uint64_t, std::vector<std::uint64_t>> children;
      std::unordered_set<std::uint64_t> live, included, requestedIds;
      std::vector<std::uint64_t> roots, pending;

      for (const auto& entity : entities) {
        const auto id = Codec::id(entity["id"]);
        live.insert(id);

        if (!entity["parent"].is_null()) {
          children[Codec::id(entity["parent"])].push_back(id);
        }
      }

      for (const auto& value : requested) {
        const auto id = Codec::id(value);

        if (!live.contains(id) || !requestedIds.insert(id).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Group edit requires unique existing entities");
        }

        roots.push_back(id);
        pending.push_back(id);
      }

      while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();

        if (!included.insert(id).second) {
          continue;
        }

        if (auto found = children.find(id); found != children.end()) {
          pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
      }

      if (command["op"] == "remove-many") {
        auto& values = entities.get_ref<Json::array_t&>();
        std::erase_if(values,
                      [&](const auto& value) { return included.contains(Codec::id(value["id"])); });
        return {};
      }

      if (included.size() > 10000 - entities.size() || included.size() > exhausted - next) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Duplicate exceeds entity/identity capacity");
      }

      std::map<std::uint64_t, std::uint64_t> mapping;
      Json copies = Json::array();

      for (const auto& entity : entities) {
        const auto id = Codec::id(entity["id"]);

        if (included.contains(id)) {
          mapping.emplace(id, next);
          created.push_back(next++);
          copies.push_back(entity);
        }
      }

      for (auto& entity : copies) {
        entity["id"] = std::to_string(mapping.at(Codec::id(entity["id"])));

        if (entity.contains("skin") && !entity["skin"].is_null()) {
          for (auto& joint : entity["skin"]["joints"]) {
            const auto mapped = mapping.find(Codec::id(joint));

            if (mapped == mapping.end()) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Duplicate must include every skin joint");
            }

            joint = std::to_string(mapped->second);
          }
        }

        if (entity.contains("animation") && !entity["animation"].is_null()) {
          for (auto& target : entity["animation"]["targets"]) {
            const auto mapped = mapping.find(Codec::id(target["entity"]));

            if (mapped == mapping.end()) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Duplicate must include all animation player targets");
            }

            target["entity"] = std::to_string(mapped->second);
          }
        }

        if (!entity["parent"].is_null()) {
          if (auto parent = mapping.find(Codec::id(entity["parent"])); parent != mapping.end()) {
            entity["parent"] = std::to_string(parent->second);
          }
        }

        auto name = entity["name"].get<std::string>();
        name.resize(std::min(name.size(), std::size_t{251}));

        while (!Core::isValidUtf8(name)) {
          name.pop_back();
        }

        entity["name"] = name + " Copy";

        for (const auto* component : {"camera", "audioListener"}) {
          if (!entity[component].is_null()) {
            entity[component]["primary"] = false;
          }
        }
      }

      for (auto& entity : copies) {
        entities.push_back(std::move(entity));
      }

      selection.clear();

      for (auto id : roots) {
        selection.push_back(mapping.at(id));
      }

      return {};
    }

    auto operation(Json& scene, const Json& command, std::uint64_t& next,
                   std::vector<std::uint64_t>& created, std::vector<std::uint64_t>& selection,
                   const Project::Project& project) -> Core::Result<void> {
      if (!command.is_object() || !command.contains("op") || !command["op"].is_string()) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Edit operation requires an op string");
      }

      const auto op = command["op"].get<std::string>();
      auto& entities = scene["entities"];

      if (op == "duplicate" || op == "remove-many") {
        return groupOperation(scene, command, next, created, selection);
      }

      if (op == "group" || op == "ungroup" || op == "reparent") {
        if (op == "group") {
          Codec::requireFields(command, {"op", "entities", "name"});
        } else if (op == "ungroup") {
          Codec::requireFields(command, {"op", "entities"});
        } else {
          Codec::requireFields(command, {"op", "entities", "parent", "space"});
        }

        if (!command["entities"].is_array() || command["entities"].empty()
            || command["entities"].size() > 10000) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Hierarchy edit requires 1..10000 entities");
        }

        auto prepared = World::decodeScene(scene.dump());

        if (!prepared) {
          return std::unexpected(prepared.error());
        }

        std::vector<World::EntityId> requested;

        for (const auto& value : command["entities"]) {
          requested.push_back({prepared->id(), Codec::id(value)});
        }

        if (op == "group") {
          if (next == exhausted) {
            return Core::fail(Core::ErrorCode::Unavailable, "Group identity capacity exhausted");
          }

          auto group = prepared->groupEntities(requested, command["name"].get<std::string>(), next);

          if (!group) {
            return std::unexpected(group.error());
          }

          created.push_back(group->value);
          next = group->value + 1;
          selection = {group->value};
        } else if (op == "ungroup") {
          auto children = prepared->ungroupEntities(requested);

          if (!children) {
            return std::unexpected(children.error());
          }

          selection.clear();

          for (const auto child : *children) {
            selection.push_back(child.value);
          }
        } else {
          const auto space = command["space"].get<std::string>();

          if (space != "local" && space != "world") {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown parent space");
          }

          std::optional<World::EntityId> parent;

          if (!command["parent"].is_null()) {
            parent = World::EntityId{prepared->id(), Codec::id(command["parent"])};
          }

          std::unordered_set<std::uint64_t> selected;

          for (const auto entity : requested) {
            auto found = prepared->entity(entity);

            if (!found) {
              return std::unexpected(found.error());
            }

            if (!selected.insert(entity.value).second) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate reparent entity");
            }
          }

          std::vector<World::ParentUpdate> roots;

          for (const auto entity : requested) {
            auto ancestor = prepared->entity(entity)->parent;

            while (ancestor && !selected.contains(ancestor->value)) {
              ancestor = prepared->entity(*ancestor)->parent;
            }

            if (!ancestor) {
              roots.push_back({entity, parent});
            }
          }

          auto changed = prepared->setParents(
              roots, space == "world" ? World::ParentSpace::World : World::ParentSpace::Local);

          if (!changed) {
            return std::unexpected(changed.error());
          }
        }

        auto encoded = World::encodeScene(*prepared);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        scene = Codec::parseJson(*encoded);
        return {};
      }

      if (op == "instantiate-scene") {
        Codec::requireFields(command, {"op", "path", "parent", "transform"});
        const auto path = command["path"].get<std::string>();

        if (!sceneAssetPath(path)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene content requires a portable scene/assets path");
        }

        auto store = ResourceStore::open(project, {.textBytes = 64 * 1024 * 1024});
        auto readable = store ? store->externalTextPath(path)
                              : Core::Result<std::filesystem::path>{std::unexpected(store.error())};

        if (!readable) {
          return std::unexpected(readable.error());
        }

        auto content = World::loadScene(project.files().value(), sceneFile(path));
        auto target = World::decodeScene(scene.dump());

        if (!content || !target) {
          return std::unexpected(!content ? content.error() : target.error());
        }

        const auto& pose = command["transform"];
        Codec::requireFields(pose, {"position", "rotation", "scale"});
        const auto position = Codec::numbers<3>(pose["position"]);
        const auto rotation = Codec::numbers<4>(pose["rotation"]);
        const auto scale = Codec::numbers<3>(pose["scale"]);
        const World::Transform placement{{position[0], position[1], position[2]},
                                         {rotation[0], rotation[1], rotation[2], rotation[3]},
                                         {scale[0], scale[1], scale[2]}};
        const auto parent
            = command["parent"].is_null()
                  ? std::optional<World::EntityId>{}
                  : std::optional{World::EntityId{target->id(), Codec::id(command["parent"])}};
        auto instance = World::prepareSceneInstance(*target, *content, next, placement, parent);

        if (!instance) {
          return std::unexpected(instance.error());
        }

        auto encoded = World::encodeScene(instance->scene);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        created.push_back(instance->root.value);

        for (const auto& mapping : instance->entities) {
          created.push_back(mapping.target.value);
        }

        next += instance->entities.size() + 1;
        selection = {instance->root.value};
        scene = Codec::parseJson(*encoded);
        return {};
      }

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
             {"character", nullptr},
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

      if (op == "occlusion") {
        Codec::requireFields(command, {"op", "value"});
        scene["occlusion"] = command["value"];
        return {};
      }

      if (op == "environment") {
        Codec::requireFields(command, {"op", "value"});
        scene["environment"] = command["value"];
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
                                        "audioListener", "script", "material",  "light",
                                        "animation",     "skin",   "character"};

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
      std::vector<std::uint64_t> selection;

      auto bytes() const -> std::uint64_t {
        return text->size() + selection.size() * sizeof(std::uint64_t);
      }
    };

    Project::Project m_project;
    std::string m_path;
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
          m_path(m_project.description().value().startScene),
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

    /** Validate metadata without reloading startup content, so Save as can rescue
     * current authoring even when that external scene has become unreadable.
     * 只校验项目描述、不重载启动内容，使外部启动场景已无法读取时仍可另存救回编辑内容。 */
    auto projectFiles() const -> Core::Result<Core::FileSystem> {
      auto files = m_project.files();

      if (!files) {
        return std::unexpected(files.error());
      }

      auto metadata = files->readText("project.dotproject", 64 * 1024);

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      auto actual = Project::decodeDescription(*metadata);

      if (!actual) {
        return std::unexpected(actual.error());
      }

      if (*actual != m_project.description().value()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor project metadata changed externally");
      }

      return files;
    }

    auto readableScene(std::string_view value) const -> Core::Result<void> {
      if (!sceneAssetPath(value)) {
        if (value == m_project.description().value().startScene) {
          return {};
        }

        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Scene requires a portable .dotscene path under scenes or assets");
      }

      auto store = ResourceStore::open(m_project, {.textBytes = 64 * 1024 * 1024});

      if (!store) {
        return std::unexpected(store.error());
      }

      auto resolved = store->externalTextPath(value);
      return resolved ? Core::Result<void>{} : std::unexpected(resolved.error());
    }

    auto publishScene(std::string_view value, std::string_view text) const -> Core::Result<void> {
      if (!sceneAssetPath(value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "New scene requires a portable .dotscene path under scenes or assets");
      }

      auto store = ResourceStore::open(m_project, {.textBytes = 64 * 1024 * 1024});
      return store ? store->createText(value, text) : std::unexpected(store.error());
    }

    /** All allocations and optional publication precede the nonthrowing scene swap.
     * 全部内存准备及可选文件发布先于不抛异常的场景替换。 */
    auto replaceScene(World::Scene prepared, std::string path, bool publish) -> Core::Result<void> {
      auto text = World::encodeScene(prepared);

      if (!text) {
        return std::unexpected(text.error());
      }

      if (text->size() > m_config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene exceeds editor history budget");
      }

      auto next = m_next;
      auto entities = prepared.entities();

      if (!entities) {
        return std::unexpected(entities.error());
      }

      for (const auto& entity : *entities) {
        next = std::max(next, entity.id.value + 1);
      }

      auto saved = std::make_shared<const std::string>(std::move(*text));
      std::vector<Entry> history{{saved, {}}};

      if (publish) {
        auto written = publishScene(path, *saved);

        if (!written) {
          return written;
        }
      }

      m_scene = std::move(prepared);
      m_path.swap(path);
      m_history = std::move(history);
      m_saved = std::move(saved);
      m_bytes = m_saved->size();
      m_cursor = 0;
      m_next = next;
      ++m_revision;
      return {};
    }

    /** Prepare every allocating history change before replacing the stable scene.
     * 替换稳定场景前准备所有需要分配的历史变更。 */
    auto commit(World::Scene prepared, std::uint64_t next, std::vector<std::uint64_t> created,
                std::vector<std::uint64_t> selection) -> Core::Result<EditReport> {
      auto encoded = World::encodeScene(prepared);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      if (*encoded == *m_history[m_cursor].text) {
        return EditReport{m_revision, m_scene.id(), false, {}};
      }

      if (encoded->size() > m_config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Edited scene exceeds editor history budget");
      }

      std::erase_if(created, [&](auto id) { return !prepared.findEntity(id); });
      std::erase_if(selection, [&](auto id) { return !prepared.findEntity(id); });

      if (encoded->size() + selection.size() * sizeof(std::uint64_t) > m_config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Edited selection exceeds history budget");
      }

      auto text = std::make_shared<const std::string>(std::move(*encoded));
      std::vector<Entry> history(m_history.begin(),
                                 m_history.begin() + static_cast<std::ptrdiff_t>(m_cursor + 1));
      history.push_back({std::move(text), std::move(selection)});
      std::uint64_t bytes = 0;

      for (const auto& entry : history) {
        bytes += entry.bytes();
      }

      while (history.size() > m_config.historyEntries || bytes > m_config.historyBytes) {
        bytes -= history.front().bytes();
        history.erase(history.begin());
      }

      /** All allocating preparation precedes this nonthrowing commit.
       * 全部分配准备在此无异常提交之前完成。 */
      m_scene = std::move(prepared);
      m_history = std::move(history);
      m_cursor = m_history.size() - 1;
      m_bytes = bytes;
      m_next = next;
      ++m_revision;
      return EditReport{m_revision, m_scene.id(), true, std::move(created)};
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

    try {
      const auto& entry = m_impl->m_history[m_impl->m_cursor];
      std::optional<World::EntityId> selected;

      std::vector<World::EntityId> selection;

      for (auto id : entry.selection) {
        selection.push_back({(*scene)->id(), id});
      }

      if (!selection.empty()) {
        selected = selection.back();
      }

      return DocumentStats{
          m_impl->m_revision,
          (*scene)->id(),
          selected,
          std::move(selection),
          m_impl->m_next == exhausted ? std::nullopt : std::optional{m_impl->m_next},
          *entry.text != *m_impl->m_saved,
          m_impl->m_cursor > 0,
          m_impl->m_cursor + 1 < m_impl->m_history.size(),
          m_impl->m_history.size(),
          m_impl->m_bytes,
          m_impl->m_path};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect document selection: " + std::string(error.what()));
    }
  }

  auto Document::snapshot() const -> Core::Result<std::string> {
    auto stats = this->stats();

    if (!stats) {
      return std::unexpected(stats.error());
    }

    try {
      auto selection = Json::array();

      for (const auto& entity : stats->selection) {
        selection.push_back(std::to_string(entity.value));
      }

      return Json{{"scenePath", stats->scenePath},
                  {"selection", std::move(selection)},
                  {"revision", std::to_string(stats->revision)},
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

  auto Document::prepareEntityEdit(std::string_view entity, std::uint64_t revision) const
      -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto valid = input(entity);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      const auto draft = Codec::parseJson(entity);
      Codec::requireFields(draft, {"id", "name", "parent", "transform", "camera", "mesh",
                                   "rigidBody", "character", "audioSource", "audioListener",
                                   "script", "material", "light", "animation", "skin"});
      const auto id = Codec::id(draft["id"]);
      auto candidate = Codec::parseJson(*m_impl->m_history[m_impl->m_cursor].text);
      const auto* original = find(candidate["entities"], id);

      if (!original) {
        return Core::fail(Core::ErrorCode::NotFound, "Inspector entity no longer exists");
      }

      Json operations = Json::array();

      for (const auto* field : {"name", "parent"}) {
        if (draft[field] != (*original)[field]) {
          operations.push_back({{"op", field}, {"entity", draft["id"]}, {field, draft[field]}});
        }
      }

      if (draft["transform"] != (*original)["transform"]) {
        operations.push_back(
            {{"op", "transform"}, {"entity", draft["id"]}, {"value", draft["transform"]}});
      }

      for (const auto* component :
           {"camera", "mesh", "rigidBody", "character", "audioSource", "audioListener", "script",
            "material", "light", "animation", "skin"}) {
        if (draft[component] != (*original)[component]) {
          operations.push_back({{"op", "component"},
                                {"entity", draft["id"]},
                                {"component", component},
                                {"value", draft[component]}});
        }
      }

      for (const auto* component : {"camera", "audioListener"}) {
        if (!draft[component].is_object() || !draft[component].contains("primary")
            || draft[component]["primary"] != true) {
          continue;
        }

        for (const auto& other : candidate["entities"]) {
          if (other["id"] != draft["id"] && other[component].is_object()
              && other[component]["primary"] == true) {
            auto description = other[component];
            description["primary"] = false;
            operations.push_back({{"op", "component"},
                                  {"entity", other["id"]},
                                  {"component", component},
                                  {"value", std::move(description)}});
          }
        }
      }

      auto next = m_impl->m_next;
      std::vector<std::uint64_t> created;
      auto selection = m_impl->m_history[m_impl->m_cursor].selection;

      for (const auto& operationValue : operations) {
        auto applied
            = operation(candidate, operationValue, next, created, selection, m_impl->m_project);

        if (!applied) {
          return std::unexpected(applied.error());
        }
      }

      auto prepared = World::decodeScene(candidate.dump());

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      auto result = operations.dump();
      auto bounded = input(result);
      return bounded ? Core::Result<std::string>{std::move(result)}
                     : std::unexpected(bounded.error());
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Inspector entity JSON: " + std::string(error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Inspector entity: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare inspector entity: " + std::string(error.what()));
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
      auto selection = m_impl->m_history[m_impl->m_cursor].selection;

      for (std::size_t index = 0; index < commands.size(); ++index) {
        auto result
            = operation(candidate, commands[index], next, created, selection, m_impl->m_project);

        if (!result) {
          return Core::fail(result.error().code, "Edit operation " + std::to_string(index) + ": "
                                                     + result.error().message);
        }
      }

      auto prepared = World::decodeScene(candidate.dump());

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      return m_impl->commit(std::move(*prepared), next, std::move(created), std::move(selection));
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

  auto Document::applyTransforms(std::span<const World::TransformUpdate> updates,
                                 std::uint64_t revision) -> Core::Result<EditReport> {
    auto current = scene();

    if (!current) {
      return std::unexpected(current.error());
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (updates.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Transform edit exceeds 10000 entities");
    }

    if (updates.empty()) {
      return EditReport{revision, (*current)->id(), false, {}};
    }

    try {
      for (const auto& update : updates) {
        auto entity = (*current)->entity(update.entity);

        if (!entity) {
          return std::unexpected(entity.error());
        }
      }

      auto prepared = World::decodeScene(*m_impl->m_history[m_impl->m_cursor].text);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      std::vector<World::TransformUpdate> mapped;
      mapped.reserve(updates.size());

      for (const auto& update : updates) {
        mapped.push_back({{prepared->id(), update.entity.value}, update.transform});
      }

      auto applied = prepared->setTransforms(mapped);

      if (!applied) {
        return std::unexpected(applied.error());
      }

      return m_impl->commit(std::move(*prepared), m_impl->m_next, {},
                            m_impl->m_history[m_impl->m_cursor].selection);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Prepare transform edit: " + std::string(error.what()));
    }
  }

  auto Document::select(std::optional<World::EntityId> entity, std::uint64_t revision)
      -> Core::Result<void> {
    const std::array single{entity.value_or(World::EntityId{})};
    return selectMany(
        entity ? std::span<const World::EntityId>{single} : std::span<const World::EntityId>{},
        revision);
  }

  auto Document::selectMany(std::span<const World::EntityId> entities, std::uint64_t revision)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (entities.size() > 10000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Selection exceeds 10000 entities");
    }

    try {
      std::vector<std::uint64_t> selection;
      std::unordered_set<std::uint64_t> seen;
      selection.reserve(entities.size());

      for (auto entity : entities) {
        auto found = m_impl->m_scene.entity(entity);

        if (!found) {
          return std::unexpected(found.error());
        }

        if (!seen.insert(entity.value).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Selection contains duplicate entities");
        }

        selection.push_back(entity.value);
      }

      auto& entry = m_impl->m_history[m_impl->m_cursor];

      if (selection != entry.selection) {
        const auto bytes = m_impl->m_bytes - entry.selection.size() * sizeof(std::uint64_t)
                           + selection.size() * sizeof(std::uint64_t);

        if (bytes > m_impl->m_config.historyBytes) {
          return Core::fail(Core::ErrorCode::Unavailable, "Selection exceeds history budget");
        }

        entry.selection = std::move(selection);
        m_impl->m_bytes = bytes;
        ++m_impl->m_revision;
      }

      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Select entities: " + std::string(error.what()));
    }
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

  auto Document::openScene(std::string_view path, std::uint64_t revision, bool discard)
      -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (!discard && *m_impl->m_history[m_impl->m_cursor].text != *m_impl->m_saved) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Save the current scene or explicitly discard it first");
    }

    try {
      auto files = m_impl->projectFiles();
      auto readable = m_impl->readableScene(path);

      if (!files || !readable) {
        return std::unexpected(!files ? files.error() : readable.error());
      }

      auto loaded = World::loadScene(*files, sceneFile(path));
      return loaded ? m_impl->replaceScene(std::move(*loaded), std::string(path), false)
                    : std::unexpected(loaded.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Open scene document: " + std::string(error.what()));
    }
  }

  auto Document::createScene(std::string_view path, std::string_view name, std::uint64_t revision,
                             bool discard) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (!discard && *m_impl->m_history[m_impl->m_cursor].text != *m_impl->m_saved) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Save the current scene or explicitly discard it first");
    }

    try {
      auto files = m_impl->projectFiles();

      if (!files) {
        return std::unexpected(files.error());
      }

      auto scene = World::Scene::create(name);
      return scene ? m_impl->replaceScene(std::move(*scene), std::string(path), true)
                   : std::unexpected(scene.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create scene document: " + std::string(error.what()));
    }
  }

  auto Document::saveAs(std::string_view path, std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    try {
      auto files = m_impl->projectFiles();

      if (!files) {
        return std::unexpected(files.error());
      }

      auto candidate = std::string(path);
      const auto& text = m_impl->m_history[m_impl->m_cursor].text;
      auto written = m_impl->publishScene(candidate, *text);

      if (!written) {
        return written;
      }

      m_impl->m_path.swap(candidate);
      m_impl->m_saved = text;
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Save scene as: " + std::string(error.what()));
    }
  }

  auto Document::saveSelectionAs(std::string_view path, std::string_view name,
                                 std::uint64_t revision) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return checked;
    }

    if (!sceneAssetPath(path) || !path.starts_with("assets/")) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Saved scene content requires an assets .dotscene path");
    }

    auto files = m_impl->projectFiles();

    if (!files) {
      return std::unexpected(files.error());
    }

    try {
      std::vector<World::EntityId> selected;

      for (const auto id : m_impl->m_history[m_impl->m_cursor].selection) {
        selected.push_back({m_impl->m_scene.id(), id});
      }

      auto content = World::extractSceneContent(m_impl->m_scene, selected, name);

      if (!content) {
        return std::unexpected(content.error());
      }

      auto encoded = World::encodeScene(content->scene);
      return encoded ? m_impl->publishScene(path, *encoded)
                     : Core::Result<void>{std::unexpected(encoded.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Save scene content: " + std::string(error.what()));
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
      auto files = m_impl->projectFiles();
      auto readable = m_impl->readableScene(m_impl->m_path);

      if (!files || !readable) {
        return std::unexpected(!files ? files.error() : readable.error());
      }

      auto disk = World::loadScene(*files, sceneFile(m_impl->m_path));

      if (!disk) {
        return std::unexpected(disk.error());
      }

      auto baseline = World::encodeScene(*disk);

      if (!baseline) {
        return std::unexpected(baseline.error());
      }

      if (*baseline != *m_impl->m_saved) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor scene changed externally: " + m_impl->m_path);
      }

      auto saved = World::saveScene(*files, sceneFile(m_impl->m_path), m_impl->m_scene);

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
        } else if (command == "select-many") {
          Codec::requireFields(parsed, {"command", "revision", "entities"});
        } else if (command == "group-selection") {
          Codec::requireFields(parsed, {"command", "revision", "name"});
        } else if (command == "duplicate-selection" || command == "delete-selection"
                   || command == "ungroup-selection") {
          Codec::requireFields(parsed, {"command", "revision"});
        } else if (command == "open-scene") {
          Codec::requireFields(parsed, {"command", "revision", "path", "discard"});
        } else if (command == "create-scene") {
          Codec::requireFields(parsed, {"command", "revision", "path", "name", "discard"});
        } else if (command == "save-scene-as") {
          Codec::requireFields(parsed, {"command", "revision", "path"});
        } else if (command == "save-selection-scene") {
          Codec::requireFields(parsed, {"command", "revision", "path", "name"});
        } else if (command == "undo" || command == "redo" || command == "save") {
          Codec::requireFields(parsed, {"command", "revision"});
        } else {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Unknown document command: " + command);
        }

        const auto revision = Codec::id(parsed["revision"]);

        if (command == "edit" || command == "duplicate-selection" || command == "delete-selection"
            || command == "group-selection" || command == "ungroup-selection") {
          auto operations = parsed.value("operations", Json::array());

          if (command != "edit") {
            auto entities = Json::array();

            for (auto id : m_impl->m_history[m_impl->m_cursor].selection) {
              entities.push_back(std::to_string(id));
            }

            const auto op = command == "duplicate-selection" ? "duplicate"
                            : command == "delete-selection"  ? "remove-many"
                            : command == "group-selection"   ? "group"
                                                             : "ungroup";
            Json operation{{"op", op}, {"entities", std::move(entities)}};

            if (command == "group-selection") {
              operation["name"] = parsed["name"];
            }

            operations = Json::array({std::move(operation)});
          }

          auto applied = apply(operations.dump(), revision);

          if (!applied) {
            return std::unexpected(applied.error());
          }

          committed = applied->changed;
          created = std::move(applied->created);
        } else if (command == "select-many") {
          if (!parsed["entities"].is_array() || parsed["entities"].size() > 10000) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Selection requires an array of at most 10000 IDs");
          }

          std::vector<World::EntityId> entities;

          for (const auto& id : parsed["entities"]) {
            entities.push_back({(*current)->id(), Codec::id(id)});
          }

          result = selectMany(entities, revision);
        } else if (command == "select") {
          result = select(
              parsed["entity"].is_null()
                  ? std::nullopt
                  : std::optional{World::EntityId{(*current)->id(), Codec::id(parsed["entity"])}},
              revision);
        } else if (command == "open-scene") {
          result = openScene(parsed["path"].get<std::string>(), revision,
                             parsed["discard"].get<bool>());
        } else if (command == "create-scene") {
          result = createScene(parsed["path"].get<std::string>(), parsed["name"].get<std::string>(),
                               revision, parsed["discard"].get<bool>());
        } else if (command == "save-scene-as") {
          result = saveAs(parsed["path"].get<std::string>(), revision);
        } else if (command == "save-selection-scene") {
          result = saveSelectionAs(parsed["path"].get<std::string>(),
                                   parsed["name"].get<std::string>(), revision);
          committed = result.has_value();
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

      if (command == "save-selection-scene") {
        response["contentPath"] = parsed["path"];
      }

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
