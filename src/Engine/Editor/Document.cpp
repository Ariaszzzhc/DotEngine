module;

#include <algorithm>
#include <array>
#include <condition_variable>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
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
import Dot.Animation.TwoBone;
import Dot.Editor.Resources;
import Dot.Project;
import Dot.World.Scene;
import Dot.World.SceneFiles;
import Dot.World.SceneContent;
import Dot.World.SceneLinks;
import Dot.World.SceneResolver;
import Dot.World.SceneSources;

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

    auto counterState(std::uint64_t next) -> std::optional<std::uint64_t> {
      return next == exhausted ? std::optional<std::uint64_t>{} : std::optional{next};
    }

    auto counterJson(std::uint64_t next) -> Json {
      return next == exhausted ? Json(nullptr) : Json(std::to_string(next));
    }

    auto allocationFloor(const World::Scene& scene) -> Core::Result<std::uint64_t> {
      auto next = scene.nextEntityValue();
      return next ? Core::Result<std::uint64_t>{next->value_or(exhausted)}
                  : Core::Result<std::uint64_t>{std::unexpected(next.error())};
    }

    /** Allocation is non-undoable metadata; mementos retain authored content only.
     * Restore/publish injects the current floor through the strict scene codec.
     * 分配是不可撤销的元数据，历史仅保留编辑内容；恢复/发布经严格场景编解码注入当前上界。 */
    auto authoringText(const World::Scene& scene) -> Core::Result<std::string> {
      auto encoded = World::encodeScene(scene);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto content = Codec::parseJson(*encoded);
      content.erase("nextEntity");
      return content.dump(2) + '\n';
    }

    auto decodeCandidate(Json& content, std::uint64_t next) -> Core::Result<World::Scene> {
      content["nextEntity"] = counterJson(next);
      return World::decodeScene(content.dump());
    }

    auto decodeHistory(std::string_view text, std::uint64_t next) -> Core::Result<World::Scene> {
      auto content = Codec::parseJson(text);
      return decodeCandidate(content, next);
    }

    auto removeLinks(Json& scene, const std::unordered_set<std::uint64_t>& removed) -> void {
      if (scene.contains("links")) {
        std::erase_if(scene["links"].get_ref<Json::array_t&>(), [&](const auto& value) {
          return removed.contains(Codec::id(value["root"]));
        });

        if (scene["links"].empty()) {
          scene.erase("links");
        }
      }
    }

    struct SourceReads {
      std::map<std::string, std::string> files;
      std::uint64_t bytes{};
      std::stop_token stop;

      auto check() const -> Core::Result<void> {
        return stop.stop_requested() ? Core::Result<void>{Core::fail(Core::ErrorCode::Unavailable,
                                                                     "Source update cancelled")}
                                     : Core::Result<void>{};
      }

      auto observe(std::string_view path, const std::string& text) -> Core::Result<void> {
        auto checked = check();

        if (!checked) {
          return checked;
        }

        const auto found = files.find(std::string(path));

        if (found != files.end()) {
          return found->second == text
                     ? Core::Result<void>{}
                     : Core::fail(Core::ErrorCode::InvalidState,
                                  "Source changed during preparation: " + std::string(path));
        }

        if (files.size() == 4096 || text.size() > 128 * 1024 * 1024 - bytes) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Source update exceeds 4096 files/128 MiB observation budget");
        }

        files.emplace(path, text);
        bytes += text.size();
        return {};
      }
    };

    auto loadContent(const Project::Project& project, std::string_view path,
                     std::string_view activePath = {}, SourceReads* reads = nullptr)
        -> Core::Result<World::SceneSourceSnapshot> {
      if (!sceneAssetPath(path) || path == activePath) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Linked content requires a distinct portable scene/assets path");
      }

      auto store = ResourceStore::open(project, {.textBytes = 64 * 1024 * 1024});
      auto files = project.files();

      if (!store || !files) {
        return std::unexpected(!store ? store.error() : files.error());
      }

      auto resolved = World::resolveSceneSource(
          path,
          [&](std::string_view asset, std::size_t maximum) -> Core::Result<std::string> {
            if (reads) {
              auto checked = reads->check();

              if (!checked) {
                return std::unexpected(checked.error());
              }
            }

            auto readable = store->externalTextPath(asset);
            auto text = readable ? files->readText(sceneFile(asset), maximum)
                                 : Core::Result<std::string>{std::unexpected(readable.error())};

            if (text && reads) {
              auto observed = reads->observe(asset, *text);

              if (!observed) {
                return std::unexpected(observed.error());
              }
            }

            return text;
          },
          {}, activePath);
      return resolved ? Core::Result<World::SceneSourceSnapshot>{std::move(resolved->source)}
                      : Core::Result<World::SceneSourceSnapshot>{std::unexpected(resolved.error())};
    }

    constexpr std::array overrideKinds{"property", "deleted-node", "added-node"};

    auto overrideKey(const Json& value) -> World::SceneOverrideKey {
      Codec::requireFields(value, {"kind", "entity", "path"});
      const auto kind = value["kind"].get<std::string>();
      const auto found = std::ranges::find(overrideKinds, kind);

      if (found == overrideKinds.end()) {
        throw std::invalid_argument("Unknown scene override kind");
      }

      return {static_cast<World::SceneOverrideKind>(found - overrideKinds.begin()),
              Codec::id(value["entity"]), value["path"].get<std::string>()};
    }

    auto overridePage(const World::SceneOverridePage& page, std::string_view scope) -> Json {
      Json changes = Json::array();
      const auto stored = [](const std::optional<std::string>& value) {
        return Json{{"present", value.has_value()},
                    {"value", value ? Codec::parseJson(*value) : Json(nullptr)}};
      };

      for (const auto& change : page.changes) {
        Json sourceKey = nullptr;

        if (change.sourceKey) {
          sourceKey = Json::array();

          for (auto component : *change.sourceKey) {
            sourceKey.push_back(std::to_string(component));
          }
        }

        changes.push_back(
            {{"kind", overrideKinds.at(static_cast<std::size_t>(change.key.kind))},
             {"entity", std::to_string(change.key.entity)},
             {"path", change.key.path},
             {"source", change.source ? Json(std::to_string(*change.source)) : Json(nullptr)},
             {"sourceKey", std::move(sourceKey)},
             {"baseline", stored(change.baseline)},
             {"local", stored(change.local)}});
      }

      return {{"linked", true},
              {"root", std::to_string(page.root.value)},
              {"asset", page.asset},
              {"node", page.node ? Json(std::to_string(*page.node)) : Json(nullptr)},
              {"scope", scope},
              {"offset", page.offset},
              {"limit", page.limit},
              {"total", page.total},
              {"fields", page.fields},
              {"nextOffset", page.offset + page.changes.size() < page.total
                                 ? Json(page.offset + page.changes.size())
                                 : Json(nullptr)},
              {"changes", std::move(changes)}};
    }

    auto sourceUpdate(const Json& command) -> bool {
      if (!command.is_object()) {
        return false;
      }

      const auto found = command.find("op");
      return found != command.end() && found->is_string()
             && (*found == "refresh-scene-instance" || *found == "revert-scene-instance"
                 || *found == "retarget-scene-instance");
    }

    /** Consecutive source updates share one owned candidate and one final native validation.
     * Keep only the current source snapshot; repeated adjacent paths reuse it within this
     * caller-serialized edit, with no cache across edits or unbounded retained source graphs.
     * 连续来源更新共用一个拥有候选及最终原生校验；仅保留当前来源快照，同一路径相邻请求在
     * 调用者串行编辑内复用，不跨编辑缓存或无界保留依赖图。 */
    auto sourceUpdates(Json& scene, std::span<const Json> commands, std::size_t& index,
                       std::uint64_t& next, std::vector<std::uint64_t>& created,
                       std::vector<std::uint64_t>& selection, const Project::Project& project,
                       std::string_view activePath, SourceReads* reads = nullptr)
        -> Core::Result<void> {
      auto target = decodeCandidate(scene, next);

      if (!target) {
        return std::unexpected(target.error());
      }

      auto batch = World::SceneLinkBatch::begin(*target, next);

      if (!batch) {
        return std::unexpected(batch.error());
      }

      const auto begin = index;
      std::optional<World::SceneSourceSnapshot> current;
      std::string currentPath;

      for (std::size_t offset = 0; offset < commands.size(); ++offset) {
        index = begin + offset;

        if (reads) {
          auto checked = reads->check();

          if (!checked) {
            return checked;
          }
        }

        const auto& command = commands[offset];
        const auto op = command["op"].get<std::string>();
        const bool retarget = op == "retarget-scene-instance";

        if (retarget) {
          Codec::requireFields(command, {"op", "entity", "path"});
        } else {
          Codec::requireFields(command, {"op", "entity"});
        }

        const World::EntityId root{target->id(), Codec::id(command["entity"])};
        auto path = retarget ? Core::Result<std::string>{command["path"].get<std::string>()}
                             : batch->sourcePath(root);

        if (!path) {
          return std::unexpected(path.error());
        }

        if (!current || currentPath != *path) {
          current.reset();
          auto source = loadContent(project, *path, activePath, reads);

          if (!source) {
            return std::unexpected(source.error());
          }

          current.emplace(std::move(*source));
          currentPath = *path;
        }

        const auto action = retarget                        ? World::SceneLinkAction::Retarget
                            : op == "revert-scene-instance" ? World::SceneLinkAction::RestoreSource
                                                            : World::SceneLinkAction::Refresh;
        auto appended = batch->append(*current, root, action);

        if (!appended) {
          return appended;
        }
      }

      auto prepared = batch->finish();

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      auto encoded = World::encodeScene(prepared->scene);
      auto floor = prepared->scene.nextEntityValue();

      if (!encoded || !floor) {
        return std::unexpected(!encoded ? encoded.error() : floor.error());
      }

      next = floor->value_or(exhausted);
      created.insert(created.end(), prepared->created.begin(), prepared->created.end());
      selection = {prepared->root.value};
      scene = Codec::parseJson(*encoded);
      return {};
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
        removeLinks(scene, included);
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
                   const Project::Project& project, std::string_view activePath)
        -> Core::Result<void> {
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

        auto prepared = decodeCandidate(scene, next);

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

      if (op == "revert-scene-overrides" || op == "revert-scene-baseline"
          || op == "revert-scene-field") {
        if (op == "revert-scene-overrides") {
          Codec::requireFields(command, {"op", "root", "keys"});
        } else if (op == "revert-scene-field") {
          Codec::requireFields(command, {"op", "root", "entity", "field"});
        } else {
          Codec::requireFields(command, {"op", "root"});
        }

        auto target = decodeCandidate(scene, next);

        if (!target) {
          return std::unexpected(target.error());
        }

        std::vector<World::SceneOverrideKey> keys;

        if (op == "revert-scene-overrides") {
          if (!command["keys"].is_array() || command["keys"].empty()
              || command["keys"].size() > 256) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Override revert requires 1..256 keys");
          }

          for (const auto& value : command["keys"]) {
            keys.push_back(overrideKey(value));
          }
        }

        const World::EntityId root{target->id(), Codec::id(command["root"])};
        auto updated = op == "revert-scene-overrides"
                           ? World::prepareSceneOverrideRevert(*target, root, keys)
                       : op == "revert-scene-field"
                           ? World::prepareSceneFieldRevert(
                                 *target, root, {target->id(), Codec::id(command["entity"])},
                                 command["field"].get<std::string>())
                           : World::prepareSceneBaselineRevert(*target, root);

        if (!updated) {
          return std::unexpected(updated.error());
        }

        auto encoded = World::encodeScene(updated->scene);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        created.insert(created.end(), updated->created.begin(), updated->created.end());
        std::erase_if(selection, [&](auto id) { return !updated->scene.findEntity(id); });

        if (selection.empty()) {
          selection = {root.value};
        }

        scene = Codec::parseJson(*encoded);
        return {};
      }

      if (op == "unlink-scene-instance") {
        Codec::requireFields(command, {"op", "entity"});
        const auto id = Codec::id(command["entity"]);
        auto target = decodeCandidate(scene, next);

        if (!target) {
          return std::unexpected(target.error());
        }

        auto links = target->sceneLinks().value();
        const auto link = std::ranges::find(links, id, &World::SceneLink::root);

        if (link == links.end()) {
          return Core::fail(Core::ErrorCode::NotFound, "Entity is not a linked scene root");
        }

        links.erase(link);
        auto changed = target->setSceneLinks(links);

        if (!changed) {
          return changed;
        }

        auto encoded = World::encodeScene(*target);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        scene = Codec::parseJson(*encoded);
        selection = {id};
        return {};
      }

      if (op == "instantiate-scene" || op == "link-scene") {
        Codec::requireFields(command, {"op", "path", "parent", "transform"});
        const auto path = command["path"].get<std::string>();

        if (!sceneAssetPath(path)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene content requires a portable scene/assets path");
        }

        auto content
            = loadContent(project, path, op == "link-scene" ? activePath : std::string_view{});
        auto target = decodeCandidate(scene, next);

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
        auto instance
            = op == "link-scene"
                  ? World::prepareLinkedScene(*target, *content, next, placement, parent)
                  : World::prepareSceneInstance(*target, content->scene, next, placement, parent);

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

      if (op == "ui" || op == "navigation") {
        Codec::requireFields(command, {"op", "value"});
        scene[op] = command["value"];
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
        removeLinks(scene, removed);
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

  struct SourceUpdate::Impl {
    struct Candidate {
      std::optional<World::DetachedScene> scene;
      std::shared_ptr<const std::string> authored;
      std::uint64_t next;
      std::vector<std::uint64_t> created;
      std::vector<std::uint64_t> selection;
    };

    std::thread::id m_owner{std::this_thread::get_id()};
    Project::Project m_project;
    std::shared_ptr<const std::uint8_t> m_identity;
    std::uint64_t m_documentRevision;
    std::string m_path;
    std::uint64_t m_historyBudget;
    mutable std::mutex m_mutex;
    std::condition_variable m_changed;
    SourceUpdateState m_state{SourceUpdateState::Preparing};
    std::uint64_t m_revision{1};
    std::optional<Core::Error> m_error;
    std::optional<Candidate> m_candidate;
    std::optional<World::DetachedScene> m_retired;
    std::jthread m_worker;

    Impl(Project::Project project, std::shared_ptr<const std::uint8_t> identity,
         std::uint64_t revision, std::string path, std::shared_ptr<const std::string> text,
         std::uint64_t next, std::vector<std::uint64_t> selection, Json commands,
         std::uint64_t historyBudget)
        : m_project(std::move(project)),
          m_identity(std::move(identity)),
          m_documentRevision(revision),
          m_path(std::move(path)),
          m_historyBudget(historyBudget),
          m_worker([this, text = std::move(text), next, selection = std::move(selection),
                    commands = std::move(commands)](std::stop_token stop) mutable {
            run(*text, next, std::move(selection), commands, stop);
          }) {
    }

    ~Impl() {
      {
        std::lock_guard lock(m_mutex);
        m_worker.request_stop();
      }

      m_changed.notify_one();

      if (m_worker.joinable()) {
        m_worker.join();
      }
    }

    auto owner() const -> Core::Result<void> {
      return m_owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Source update requires its owner thread");
    }

    auto complete(SourceUpdateState state, std::optional<Core::Error> error = {}) -> void {
      std::lock_guard lock(m_mutex);
      m_candidate.reset();
      m_retired.reset();
      m_state = state;
      m_error = std::move(error);
      ++m_revision;
    }

    auto prepare(std::string_view text, std::uint64_t next, std::vector<std::uint64_t> selection,
                 const Json& commands, SourceReads& reads) -> Core::Result<Candidate> {
      auto files = m_project.files();
      auto metadata = files ? files->readText("project.dotproject", 64 * 1024)
                            : Core::Result<std::string>{std::unexpected(files.error())};

      if (!metadata) {
        return std::unexpected(metadata.error());
      }

      auto actual = Project::decodeDescription(*metadata);

      if (!actual || *actual != m_project.description().value()) {
        return actual ? Core::fail(Core::ErrorCode::InvalidState,
                                   "Editor project metadata changed externally")
                      : std::unexpected(actual.error());
      }

      auto observed = reads.observe("project.dotproject", *metadata);

      if (!observed) {
        return std::unexpected(observed.error());
      }

      auto candidate = Codec::parseJson(text);
      std::size_t index = 0;
      std::vector<std::uint64_t> created;
      auto updated = sourceUpdates(candidate, commands.get_ref<const Json::array_t&>(), index, next,
                                   created, selection, m_project, m_path, &reads);

      if (!updated) {
        return Core::fail(updated.error().code, "Edit operation " + std::to_string(index) + ": "
                                                    + updated.error().message);
      }

      auto scene = decodeCandidate(candidate, next);
      auto encoded = scene ? authoringText(*scene)
                           : Core::Result<std::string>{std::unexpected(scene.error())};

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      std::erase_if(created, [&](auto id) { return !scene->findEntity(id); });
      std::erase_if(selection, [&](auto id) { return !scene->findEntity(id); });
      std::optional<World::DetachedScene> detached;

      if (*encoded != text) {
        if (encoded->size() + selection.size() * sizeof(std::uint64_t) > m_historyBudget) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Edited scene/selection exceeds editor history budget");
        }

        auto released = scene->detach();

        if (!released) {
          return std::unexpected(released.error());
        }

        detached.emplace(std::move(*released));
      }

      return Candidate{std::move(detached),
                       std::make_shared<const std::string>(std::move(*encoded)), next,
                       std::move(created), std::move(selection)};
    }

    auto validate(const SourceReads& reads) -> Core::Result<void> {
      auto files = m_project.files();
      auto store = ResourceStore::open(m_project, {.textBytes = 64 * 1024 * 1024});

      if (!files || !store) {
        return std::unexpected(!files ? files.error() : store.error());
      }

      for (const auto& [path, observed] : reads.files) {
        auto checked = reads.check();

        if (!checked) {
          return checked;
        }

        if (path != "project.dotproject") {
          auto readable = store->externalTextPath(path);

          if (!readable) {
            return std::unexpected(readable.error());
          }
        }

        auto text = files->readText(sceneFile(path), observed.size() + 1);

        if (!text) {
          return std::unexpected(text.error());
        }

        if (*text != observed) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Source changed before application: " + path);
        }
      }

      return {};
    }

    auto run(std::string_view text, std::uint64_t next, std::vector<std::uint64_t> selection,
             const Json& commands, std::stop_token stop) -> void {
      try {
        SourceReads reads{{}, 0, stop};
        std::optional<Candidate> prepared;

        {
          auto result = prepare(text, next, std::move(selection), commands, reads);

          if (!result) {
            reads.files.clear();
            complete(
                stop.stop_requested() ? SourceUpdateState::Cancelled : SourceUpdateState::Failed,
                stop.stop_requested() ? std::optional<Core::Error>{} : result.error());
            return;
          }

          prepared.emplace(std::move(*result));
        }

        std::unique_lock lock(m_mutex);

        if (!stop.stop_requested()) {
          m_state = SourceUpdateState::Ready;
          ++m_revision;
          m_changed.wait(lock, [&] {
            return stop.stop_requested() || m_state == SourceUpdateState::Validating;
          });
        }

        lock.unlock();
        auto validated = reads.check();

        if (validated) {
          validated = validate(reads);
        }

        lock.lock();

        if (!stop.stop_requested() && validated) {
          m_candidate.emplace(std::move(*prepared));
          prepared.reset();
          m_state = SourceUpdateState::Validated;
          ++m_revision;
          m_changed.wait(lock, [&] {
            return stop.stop_requested() || m_state != SourceUpdateState::Validated;
          });
        } else if (!stop.stop_requested()) {
          m_state = SourceUpdateState::Failed;
          m_error = validated.error();
          ++m_revision;
        }

        const bool cancelled = stop.stop_requested() && m_state != SourceUpdateState::Applied
                               && m_state != SourceUpdateState::Failed;
        auto discarded = std::move(m_candidate);
        auto retired = std::move(m_retired);
        m_candidate.reset();
        m_retired.reset();
        lock.unlock();
        prepared.reset();
        discarded.reset();
        retired.reset();
        reads.files.clear();

        if (cancelled) {
          complete(SourceUpdateState::Cancelled);
        }
      } catch (const Json::exception& error) {
        complete(SourceUpdateState::Failed,
                 Core::fail(Core::ErrorCode::InvalidArgument,
                            "Source update JSON: " + std::string(error.what()))
                     .error());
      } catch (const std::invalid_argument& error) {
        complete(SourceUpdateState::Failed,
                 Core::fail(Core::ErrorCode::InvalidArgument,
                            "Source update command: " + std::string(error.what()))
                     .error());
      } catch (const std::exception& error) {
        complete(SourceUpdateState::Failed,
                 Core::fail(Core::ErrorCode::External,
                            "Source update worker: " + std::string(error.what()))
                     .error());
      }
    }
  };

  SourceUpdate::SourceUpdate(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SourceUpdate::SourceUpdate(SourceUpdate&&) noexcept = default;

  auto SourceUpdate::operator=(SourceUpdate&&) noexcept -> SourceUpdate& = default;

  SourceUpdate::~SourceUpdate() = default;

  auto SourceUpdate::status() const -> Core::Result<SourceUpdateStatus> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Source update was moved");
    }

    auto owner = m_impl->owner();

    if (!owner) {
      return std::unexpected(owner.error());
    }

    std::lock_guard lock(m_impl->m_mutex);
    return SourceUpdateStatus{m_impl->m_state, m_impl->m_revision, m_impl->m_documentRevision,
                              m_impl->m_error};
  }

  auto SourceUpdate::requestApply(std::uint64_t revision) -> Core::Result<void> {
    auto observed = status();

    if (!observed) {
      return std::unexpected(observed.error());
    }

    std::lock_guard lock(m_impl->m_mutex);

    if (revision != m_impl->m_revision || m_impl->m_state != SourceUpdateState::Ready) {
      return Core::fail(Core::ErrorCode::InvalidState, "Apply the current ready source update");
    }

    m_impl->m_state = SourceUpdateState::Validating;
    ++m_impl->m_revision;
    m_impl->m_changed.notify_one();
    return {};
  }

  auto SourceUpdate::cancel(std::uint64_t revision) -> Core::Result<void> {
    auto observed = status();

    if (!observed) {
      return std::unexpected(observed.error());
    }

    std::lock_guard lock(m_impl->m_mutex);
    const auto state = m_impl->m_state;

    if (revision != m_impl->m_revision
        || (state != SourceUpdateState::Preparing && state != SourceUpdateState::Ready
            && state != SourceUpdateState::Validating && state != SourceUpdateState::Validated)) {
      return Core::fail(Core::ErrorCode::InvalidState, "Cancel a current unfinished source update");
    }

    m_impl->m_worker.request_stop();
    m_impl->m_state = SourceUpdateState::Cancelling;
    ++m_impl->m_revision;
    m_impl->m_changed.notify_one();
    return {};
  }

  struct Document::Impl {
    struct Entry {
      std::shared_ptr<const std::string> text;
      std::vector<std::uint64_t> selection;

      auto bytes() const -> std::uint64_t {
        return text->size() + selection.size() * sizeof(std::uint64_t);
      }
    };

    std::shared_ptr<const std::uint8_t> m_identity{std::make_shared<const std::uint8_t>(0)};
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
    std::uint64_t m_savedNext{1};

    Impl(Project::Project project, World::Scene scene, DocumentConfig config,
         std::shared_ptr<const std::string> text, std::uint64_t next)
        : m_project(std::move(project)),
          m_path(m_project.description().value().startScene),
          m_scene(std::move(scene)),
          m_config(config),
          m_next(next),
          m_history{{text, {}}},
          m_bytes(text->size()),
          m_saved(std::move(text)),
          m_savedNext(next) {
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
      auto loadedNext = allocationFloor(prepared);

      if (!loadedNext) {
        return std::unexpected(loadedNext.error());
      }

      const auto next = std::max(m_next, *loadedNext);
      auto reserved = prepared.reserveEntityValuesBefore(counterState(next));

      if (!reserved) {
        return reserved;
      }

      auto text = authoringText(prepared);

      if (!text) {
        return std::unexpected(text.error());
      }

      if (text->size() > m_config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable, "Scene exceeds editor history budget");
      }

      auto saved = std::make_shared<const std::string>(std::move(*text));
      std::vector<Entry> history{{saved, {}}};

      if (publish) {
        auto encoded = World::encodeScene(prepared);
        auto written = encoded ? publishScene(path, *encoded)
                               : Core::Result<void>{std::unexpected(encoded.error())};

        if (!written) {
          return written;
        }
      }

      m_scene = std::move(prepared);
      m_path.swap(path);
      m_history = std::move(history);
      m_saved = std::move(saved);
      m_savedNext = publish ? next : *loadedNext;
      m_bytes = m_saved->size();
      m_cursor = 0;
      m_next = next;
      ++m_revision;
      return {};
    }

    /** Prepare every allocating history change before replacing the stable scene.
     * 替换稳定场景前准备所有需要分配的历史变更。 */
    auto commit(World::Scene prepared, std::uint64_t next, std::vector<std::uint64_t> created,
                std::vector<std::uint64_t> selection,
                const std::function<Core::Result<void>()>& publish = {})
        -> Core::Result<EditReport> {
      auto reserved = prepared.reserveEntityValuesBefore(counterState(next));

      if (!reserved) {
        return std::unexpected(reserved.error());
      }

      auto encoded = authoringText(prepared);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      return commitPrepared(prepared, next,
                            std::make_shared<const std::string>(std::move(*encoded)),
                            std::move(created), std::move(selection), publish);
    }

    /** Canonical text and scene are sealed together internally; all allocating history work
     * precedes publication. Background callers hand the retired CPU scene back to their worker.
     * 规范文本与场景在内部一起封装，全部历史分配先于发布；后台调用者把退役 CPU 场景交回工作线程。
     */
    auto commitPrepared(World::Scene& prepared, std::uint64_t next,
                        std::shared_ptr<const std::string> encoded,
                        std::vector<std::uint64_t> created, std::vector<std::uint64_t> selection,
                        const std::function<Core::Result<void>()>& publish = {},
                        std::optional<World::DetachedScene>* retired = nullptr)
        -> Core::Result<EditReport> {
      if (*encoded == *m_history[m_cursor].text) {
        if (publish) {
          auto published = publish();

          if (!published) {
            return std::unexpected(published.error());
          }
        }

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

      std::vector<Entry> history(m_history.begin(),
                                 m_history.begin() + static_cast<std::ptrdiff_t>(m_cursor + 1));
      history.push_back({std::move(encoded), std::move(selection)});
      std::uint64_t bytes = 0;

      for (const auto& entry : history) {
        bytes += entry.bytes();
      }

      while (history.size() > m_config.historyEntries || bytes > m_config.historyBytes) {
        bytes -= history.front().bytes();
        history.erase(history.begin());
      }

      /** Publish external resources only after allocating scene/history state;
       * successful publication is followed by ownership replacement without allocation.
       * 场景/历史分配完成后才发布外部资源，成功后无分配地替换所有权。 */
      if (publish) {
        auto published = publish();

        if (!published) {
          return std::unexpected(published.error());
        }
      }

      if (retired) {
        auto detached = m_scene.detach();

        if (!detached) {
          return std::unexpected(detached.error());
        }

        retired->emplace(std::move(*detached));
      }

      m_scene = std::move(prepared);
      m_history = std::move(history);
      m_cursor = m_history.size() - 1;
      m_bytes = bytes;
      m_next = next;
      ++m_revision;
      return EditReport{m_revision, m_scene.id(), true, std::move(created)};
    }

    auto restore(std::size_t cursor) -> Core::Result<void> {
      auto scene = decodeHistory(*m_history[cursor].text, m_next);

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

      auto text = authoringText(*scene);

      if (!text) {
        return std::unexpected(text.error());
      }

      if (text->size() > config.historyBytes) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Startup scene exceeds editor history budget");
      }

      auto next = allocationFloor(*scene);

      if (!next) {
        return std::unexpected(next.error());
      }

      return Document(std::make_unique<Impl>(project, std::move(*scene), config,
                                             std::make_shared<const std::string>(std::move(*text)),
                                             *next));
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

      auto sceneData = Codec::parseJson(*m_impl->m_history[m_impl->m_cursor].text);
      sceneData["nextEntity"] = counterJson(m_impl->m_next);
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
                  {"scene", std::move(sceneData)}}
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
        auto applied = operation(candidate, operationValue, next, created, selection,
                                 m_impl->m_project, m_impl->m_path);

        if (!applied) {
          return std::unexpected(applied.error());
        }
      }

      auto prepared = decodeCandidate(candidate, next);

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
        Core::Result<void> result;

        if (sourceUpdate(commands[index])) {
          std::size_t end = index + 1;

          while (end < commands.size() && sourceUpdate(commands[end])) {
            ++end;
          }

          const auto segment
              = std::span<const Json>{commands.get_ref<const Json::array_t&>()}.subspan(
                  index, end - index);
          result = sourceUpdates(candidate, segment, index, next, created, selection,
                                 m_impl->m_project, m_impl->m_path);
        } else {
          result = operation(candidate, commands[index], next, created, selection,
                             m_impl->m_project, m_impl->m_path);
        }

        if (!result) {
          return Core::fail(result.error().code, "Edit operation " + std::to_string(index) + ": "
                                                     + result.error().message);
        }
      }

      auto prepared = decodeCandidate(candidate, next);

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

  auto Document::beginSourceUpdate(std::string_view operations, std::uint64_t revision) const
      -> Core::Result<SourceUpdate> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);
    auto valid = input(operations);

    if (!checked || !valid) {
      return std::unexpected(!checked ? checked.error() : valid.error());
    }

    try {
      auto commands = Codec::parseJson(operations);

      if (!commands.is_array() || commands.empty() || commands.size() > 256
          || !std::ranges::all_of(commands, sourceUpdate)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Source update requires 1..256 refresh/restore/retarget operations");
      }

      const auto& entry = m_impl->m_history[m_impl->m_cursor];
      return SourceUpdate(std::make_unique<SourceUpdate::Impl>(
          m_impl->m_project, m_impl->m_identity, revision, m_impl->m_path, entry.text,
          m_impl->m_next, entry.selection, std::move(commands), m_impl->m_config.historyBytes));
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Begin source update JSON: " + std::string(error.what()));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin source update: " + std::string(error.what()));
    }
  }

  auto Document::applySourceUpdate(SourceUpdate& update, std::uint64_t revision)
      -> Core::Result<EditReport> {
    auto observed = update.status();

    if (!observed) {
      return std::unexpected(observed.error());
    }

    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::lock_guard lock(update.m_impl->m_mutex);
    auto& job = *update.m_impl;

    if (job.m_state != SourceUpdateState::Validated || !job.m_candidate) {
      return Core::fail(Core::ErrorCode::InvalidState, "Source update is not validated");
    }

    if (job.m_identity != m_impl->m_identity) {
      return Core::fail(Core::ErrorCode::InvalidState, "Source update belongs to another document");
    }

    Core::Result<EditReport> result
        = Core::fail(Core::ErrorCode::InvalidState, "Source update document revision/path changed");

    try {
      if (job.m_documentRevision == revision && job.m_path == m_impl->m_path) {
        auto& candidate = *job.m_candidate;

        if (!candidate.scene) {
          result = EditReport{m_impl->m_revision, m_impl->m_scene.id(), false, {}};
        } else {
          auto scene = World::Scene::adopt(*candidate.scene);

          if (scene) {
            result = m_impl->commitPrepared(*scene, candidate.next, candidate.authored,
                                            std::move(candidate.created),
                                            std::move(candidate.selection), {}, &job.m_retired);

            if (scene->id() != 0) {
              auto detached = scene->detach();

              if (detached) {
                job.m_retired.emplace(std::move(*detached));
              } else {
                result = std::unexpected(detached.error());
              }
            }
          } else {
            result = std::unexpected(scene.error());
          }
        }
      }
    } catch (const std::exception& error) {
      result = Core::fail(Core::ErrorCode::External,
                          "Publish source update: " + std::string(error.what()));
    }

    job.m_state = result ? SourceUpdateState::Applied : SourceUpdateState::Failed;
    job.m_error = result ? std::optional<Core::Error>{} : result.error();
    ++job.m_revision;
    job.m_changed.notify_one();
    return result;
  }

  auto Document::applySceneField(World::EntityId root, World::EntityId entity,
                                 std::string_view field, std::uint64_t revision)
      -> Core::Result<SceneSourceApplyReport> {
    return applySceneSource(root, entity, field, revision);
  }

  auto Document::applySceneInstance(World::EntityId root, std::uint64_t revision)
      -> Core::Result<SceneSourceApplyReport> {
    return applySceneSource(root, {}, {}, revision);
  }

  auto Document::applySceneSource(World::EntityId root, std::optional<World::EntityId> entity,
                                  std::string_view field, std::uint64_t revision)
      -> Core::Result<SceneSourceApplyReport> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Editor document was moved");
    }

    auto checked = m_impl->check(revision);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      auto live = m_impl->m_scene.entity(root);
      auto links = m_impl->m_scene.sceneLinks();
      auto files = m_impl->projectFiles();

      if (!live || !links || !files) {
        return std::unexpected(!live ? live.error() : !links ? links.error() : files.error());
      }

      const auto link = std::ranges::find(*links, root.value, &World::SceneLink::root);

      if (link == links->end() || !sceneAssetPath(link->asset) || link->asset == m_impl->m_path) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Apply requires a distinct linked scene source");
      }

      std::string asset = link->asset;
      auto store = ResourceStore::open(m_impl->m_project, {.textBytes = 64 * 1024 * 1024});

      if (!store) {
        return std::unexpected(store.error());
      }

      auto text = store->openText(asset);
      auto baseline
          = text ? text->snapshot() : Core::Result<TextSnapshot>{std::unexpected(text.error())};

      if (!baseline) {
        return std::unexpected(baseline.error());
      }

      std::map<std::string, std::string> observed;
      auto resolved = World::resolveSceneSource(
          asset,
          [&](std::string_view path, std::size_t maximum) -> Core::Result<std::string> {
            auto readable = store->externalTextPath(path);

            if (!readable) {
              return std::unexpected(readable.error());
            }

            auto value = path == asset ? Core::Result<std::string>{baseline->text}
                                       : files->readText(sceneFile(path), maximum);

            if (value) {
              observed.emplace(path, *value);
            }

            return value;
          },
          {}, m_impl->m_path);

      if (!resolved) {
        return std::unexpected(resolved.error());
      }

      auto prepared = entity ? World::prepareSceneFieldApply(m_impl->m_scene, resolved->source,
                                                             root, *entity, field, m_impl->m_next)
                             : World::prepareSceneInstanceApply(m_impl->m_scene, resolved->source,
                                                                root, m_impl->m_next);

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      /** Resolve the proposed saved graph before publication, including newly copied nested
       * links. Rebase any fresh nested updates before persisting their materialized values.
       * 发布前解析候选保存图，包含新复制的嵌套关联；写入展开物理值前先合并嵌套最新更新。 */
      if (prepared->sourceChanged) {
        auto proposed = World::encodeScene(prepared->source);

        if (!proposed) {
          return std::unexpected(proposed.error());
        }

        auto checkedGraph = World::resolveSceneSource(
            asset,
            [&](std::string_view path, std::size_t maximum) -> Core::Result<std::string> {
              if (path == asset) {
                return *proposed;
              }

              auto readable = store->externalTextPath(path);
              auto value = readable ? files->readText(sceneFile(path), maximum)
                                    : Core::Result<std::string>{std::unexpected(readable.error())};

              if (value) {
                const auto found = observed.find(std::string(path));

                if (found != observed.end() && found->second != *value) {
                  return Core::fail(Core::ErrorCode::InvalidState,
                                    "Scene dependency changed during apply: " + std::string(path));
                }

                observed.emplace(path, *value);
              }

              return value;
            },
            {}, m_impl->m_path);

        if (!checkedGraph) {
          return std::unexpected(checkedGraph.error());
        }

        auto updatedText = World::encodeScene(checkedGraph->source.scene);

        if (!updatedText) {
          return std::unexpected(updatedText.error());
        }

        if (*updatedText != *proposed) {
          auto persisted = World::captureSceneSource(checkedGraph->source.scene, asset);
          auto floor = allocationFloor(prepared->instance.scene);

          if (!persisted || !floor) {
            return std::unexpected(!persisted ? persisted.error() : floor.error());
          }

          auto rebased
              = World::prepareSceneLinkUpdate(prepared->instance.scene, *persisted,
                                              {prepared->instance.scene.id(), root.value}, *floor);

          if (!rebased) {
            return std::unexpected(rebased.error());
          }

          rebased->created.insert(rebased->created.begin(), prepared->instance.created.begin(),
                                  prepared->instance.created.end());
          prepared->instance = std::move(*rebased);
          prepared->source = std::move(checkedGraph->source.scene);
        }
      }

      auto next = allocationFloor(prepared->instance.scene);

      if (!next) {
        return std::unexpected(next.error());
      }

      if (prepared->sourceChanged) {
        auto encoded = World::encodeScene(prepared->source);
        auto replaced = encoded ? text->replace(*encoded, baseline->revision)
                                : Core::Result<void>{std::unexpected(encoded.error())};

        if (!replaced) {
          return std::unexpected(replaced.error());
        }
      }

      bool written = false;
      auto committed = m_impl->commit(
          std::move(prepared->instance.scene), *next, std::move(prepared->instance.created),
          m_impl->m_history[m_impl->m_cursor].selection, [&]() -> Core::Result<void> {
            for (const auto& [path, previous] : observed) {
              auto readable = store->externalTextPath(path);
              auto current = readable
                                 ? files->readText(sceneFile(path), 64 * 1024 * 1024)
                                 : Core::Result<std::string>{std::unexpected(readable.error())};

              if (!current) {
                return std::unexpected(current.error());
              }

              if (*current != previous) {
                return Core::fail(Core::ErrorCode::InvalidState,
                                  "Scene source changed during apply: " + path);
              }
            }

            if (!prepared->sourceChanged) {
              return {};
            }

            auto snapshot = text->snapshot();

            if (!snapshot) {
              return std::unexpected(snapshot.error());
            }

            auto saved = text->save(snapshot->revision);

            if (!saved) {
              return Core::fail(saved.error().code,
                                "Source write failed and may have committed; document retained: "
                                    + saved.error().message);
            }

            written = true;
            return {};
          });

      return committed ? Core::Result<SceneSourceApplyReport>{SceneSourceApplyReport{
                             std::move(*committed), std::move(asset), written}}
                       : std::unexpected(committed.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Apply scene source: " + std::string(error.what()));
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

      auto prepared = decodeHistory(*m_impl->m_history[m_impl->m_cursor].text, m_impl->m_next);

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
      auto encoded = World::encodeScene(m_impl->m_scene);
      auto written = encoded ? m_impl->publishScene(candidate, *encoded)
                             : Core::Result<void>{std::unexpected(encoded.error())};

      if (!written) {
        return written;
      }

      m_impl->m_path.swap(candidate);
      m_impl->m_saved = text;
      m_impl->m_savedNext = m_impl->m_next;
      ++m_impl->m_revision;
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Save scene as: " + std::string(error.what()));
    }
  }

  auto Document::saveContentAs(const World::Scene& content, std::string_view path,
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
      auto encoded = World::encodeScene(content);
      return encoded ? m_impl->publishScene(path, *encoded)
                     : Core::Result<void>{std::unexpected(encoded.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Publish scene content: " + std::string(error.what()));
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

    try {
      std::vector<World::EntityId> selected;

      for (const auto id : m_impl->m_history[m_impl->m_cursor].selection) {
        selected.push_back({m_impl->m_scene.id(), id});
      }

      auto content = World::extractSceneContent(m_impl->m_scene, selected, name);
      return content ? saveContentAs(content->scene, path, revision)
                     : Core::Result<void>{std::unexpected(content.error())};
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

      auto baseline = authoringText(*disk);
      auto savedNext = allocationFloor(*disk);

      if (!savedNext) {
        return std::unexpected(savedNext.error());
      }

      if (!baseline) {
        return std::unexpected(baseline.error());
      }

      if (*baseline != *m_impl->m_saved || *savedNext != m_impl->m_savedNext) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Editor scene changed externally: " + m_impl->m_path);
      }

      auto saved = World::saveScene(*files, sceneFile(m_impl->m_path), m_impl->m_scene);

      if (!saved) {
        return saved;
      }

      m_impl->m_saved = m_impl->m_history[m_impl->m_cursor].text;
      m_impl->m_savedNext = m_impl->m_next;
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
      Json overrides;
      Json appliedSource;
      Json twoBone;
      Core::Result<void> result;

      if (command == "inspect") {
        Codec::requireFields(parsed, {"command"});
      } else if (command == "inspect-scene-overrides") {
        Codec::requireFields(parsed, {"command", "entity", "scope", "offset", "limit"});
        const auto mode = parsed["scope"].get<std::string>();

        if ((mode != "auto" && mode != "entity" && mode != "instance")
            || !parsed["offset"].is_number_unsigned() || !parsed["limit"].is_number_unsigned()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid override scope/page integers");
        }

        const auto offset = parsed["offset"].get<std::size_t>();
        const auto limit = parsed["limit"].get<std::size_t>();

        if (limit == 0 || limit > 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Override page limit must be 1..256");
        }

        const World::EntityId entity{(*current)->id(), Codec::id(parsed["entity"])};
        auto owner = World::findSceneLinkRoot(**current, entity);

        if (!owner) {
          return std::unexpected(owner.error());
        }

        if (*owner) {
          const bool all = mode == "instance" || (mode == "auto" && **owner == entity);
          auto page = World::inspectSceneOverrides(
              **current, **owner,
              all ? std::optional<std::uint64_t>{} : std::optional{entity.value}, offset, limit);

          if (!page) {
            return std::unexpected(page.error());
          }

          overrides = overridePage(*page, all ? "instance" : "entity");
        } else {
          if (offset != 0) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Override page offset exceeds total");
          }

          overrides = {{"linked", false},          {"root", nullptr},
                       {"asset", nullptr},         {"node", std::to_string(entity.value)},
                       {"scope", "none"},          {"offset", 0},
                       {"limit", limit},           {"total", 0},
                       {"fields", Json::object()}, {"nextOffset", nullptr},
                       {"changes", Json::array()}};
        }

        const auto& selection = m_impl->m_history[m_impl->m_cursor].selection;
        return Json{{"ok", true},
                    {"revision", std::to_string(m_impl->m_revision)},
                    {"scope", std::to_string((*current)->id())},
                    {"scenePath", m_impl->m_path},
                    {"selected",
                     selection.empty() ? Json(nullptr) : Json(std::to_string(selection.back()))},
                    {"created", Json::array()},
                    {"sceneOverrides", std::move(overrides)}}
            .dump();
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
        } else if (command == "apply-scene-field") {
          Codec::requireFields(parsed, {"command", "revision", "root", "entity", "field"});
        } else if (command == "apply-scene-instance") {
          Codec::requireFields(parsed, {"command", "revision", "root"});
        } else if (command == "pose-two-bone") {
          Codec::requireFields(parsed,
                               {"command", "revision", "root", "middle", "end", "target", "pole"});
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
        } else if (command == "apply-scene-field" || command == "apply-scene-instance") {
          const World::EntityId root{(*current)->id(), Codec::id(parsed["root"])};
          auto applied
              = command == "apply-scene-field"
                    ? applySceneField(root, {(*current)->id(), Codec::id(parsed["entity"])},
                                      parsed["field"].get<std::string>(), revision)
                    : applySceneInstance(root, revision);

          if (!applied) {
            return std::unexpected(applied.error());
          }

          committed = applied->document.changed || applied->sourceWritten;
          created = std::move(applied->document.created);
          appliedSource = {{"path", std::move(applied->sourcePath)},
                           {"written", applied->sourceWritten},
                           {"sceneChanged", applied->document.changed}};
        } else if (command == "pose-two-bone") {
          auto checked = m_impl->check(revision);

          if (!checked) {
            return std::unexpected(checked.error());
          }

          const auto target = Codec::numbers<3>(parsed["target"]);
          const auto pole = Codec::numbers<3>(parsed["pole"]);
          auto solved
              = Animation::solveTwoBone(**current, {{(*current)->id(), Codec::id(parsed["root"])},
                                                    {(*current)->id(), Codec::id(parsed["middle"])},
                                                    {(*current)->id(), Codec::id(parsed["end"])},
                                                    {target[0], target[1], target[2]},
                                                    {pole[0], pole[1], pole[2]}});

          if (!solved) {
            return std::unexpected(solved.error());
          }

          twoBone = {{"root", parsed["root"]},
                     {"middle", parsed["middle"]},
                     {"end", parsed["end"]},
                     {"target", parsed["target"]},
                     {"pole", parsed["pole"]},
                     {"reached", {solved->reached.x, solved->reached.y, solved->reached.z}},
                     {"residual", solved->residual},
                     {"clamped", solved->clamped}};
          auto applied = applyTransforms(solved->updates, revision);

          if (!applied) {
            return std::unexpected(applied.error());
          }

          committed = applied->changed;
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

      if (!appliedSource.is_null()) {
        response["sourceApplied"] = std::move(appliedSource);
      }

      if (!twoBone.is_null()) {
        response["twoBone"] = std::move(twoBone);
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
