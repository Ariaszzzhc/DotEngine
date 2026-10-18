module;

#include <algorithm>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.World.SceneLinks;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneContent;
import Dot.World.SceneFiles;
import Dot.World.SceneSources;
import Dot.World.SceneIdentity;

namespace Dot::World {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Values = std::map<std::uint64_t, std::uint64_t>;
    using Nodes = std::map<std::uint64_t, Json>;

    auto baseline(const Scene& source) -> Core::Result<std::string> {
      auto encoded = encodeScene(source);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      auto data = Codec::parseJson(*encoded);
      data.erase("links");
      data["environment"] = nullptr;
      data["occlusion"] = nullptr;
      return data.dump(2) + '\n';
    }

    auto nodes(const Json& source, const Values& mapping, std::uint64_t root) -> Nodes {
      Nodes result;

      for (auto entity : source["entities"]) {
        const auto id = Codec::id(entity["id"]);
        entity["id"] = std::to_string(mapping.at(id));
        entity["parent"] = entity["parent"].is_null()
                               ? (root == 0 ? Json(nullptr) : Json(std::to_string(root)))
                               : Json(std::to_string(mapping.at(Codec::id(entity["parent"]))));

        for (const auto* field : {"camera", "audioListener"}) {
          if (!entity[field].is_null()) {
            entity[field]["primary"] = false;
          }
        }

        if (!entity["skin"].is_null()) {
          for (auto& joint : entity["skin"]["joints"]) {
            joint = std::to_string(mapping.at(Codec::id(joint)));
          }
        }

        if (!entity["animation"].is_null()) {
          for (auto& target : entity["animation"]["targets"]) {
            target["entity"] = std::to_string(mapping.at(Codec::id(target["entity"])));
          }
        }

        result.emplace(id, std::move(entity));
      }

      return result;
    }

    struct SourceBaseline {
      std::string text;
      std::vector<SceneSourceOrigin> origins;
    };

    /** Canonical baseline values follow logical keys, never working IDs/floors.
     * 基线值按逻辑键规范化，不依赖工作 ID/上界，避免无逻辑变化的重排污染历史。 */
    auto canonicalSource(const SceneSourceSnapshot& source) -> Core::Result<SourceBaseline> {
      auto text = baseline(source.scene);

      if (!text) {
        return std::unexpected(text.error());
      }

      std::map<SceneSourceKey, std::uint64_t> ordered;

      for (const auto& origin : source.origins) {
        ordered.emplace(origin.key, origin.entity);
      }

      Values values;
      SourceBaseline prepared;
      std::uint64_t next{1};

      for (const auto& [key, entity] : ordered) {
        values.emplace(entity, next);
        prepared.origins.push_back({next++, key});
      }

      auto document = Codec::parseJson(*text);
      const auto remapped = nodes(document, values, 0);
      document["entities"] = Json::array();

      for (const auto& [key, entity] : ordered) {
        document["entities"].push_back(remapped.at(entity));
      }

      document["nextEntity"] = std::to_string(next);
      prepared.text = document.dump(2) + '\n';
      return prepared;
    }

    auto member(const Json& object, const std::string& key) -> std::optional<Json> {
      const auto found = object.find(key);
      return found == object.end() ? std::optional<Json>{} : std::optional<Json>{*found};
    }

    /** Missing and null are distinct: deleting a property is an override too.
     * 缺失与 null 不同，删除属性也是覆盖。 */
    auto merge(const std::optional<Json>& old, const std::optional<Json>& local,
               const std::optional<Json>& incoming) -> std::optional<Json> {
      if (local == old) {
        return incoming;
      }

      if (!old || !local || !incoming || !old->is_object() || !local->is_object()
          || !incoming->is_object()) {
        return local;
      }

      std::set<std::string> keys;

      for (const auto* value : {&*old, &*local, &*incoming}) {
        for (const auto& item : value->items()) {
          keys.insert(item.key());
        }
      }

      Json result = Json::object();

      for (const auto& key : keys) {
        auto value = merge(member(*old, key), member(*local, key), member(*incoming, key));

        if (value) {
          result[key] = std::move(*value);
        }
      }

      return result;
    }

    using Reference = std::function<Core::Result<Json>(std::uint64_t)>;

    auto remapField(Json value, std::string_view field, const Reference& reference)
        -> Core::Result<Json> {
      const auto replace = [&](Json& id) -> Core::Result<void> {
        auto mapped = reference(Codec::id(id));

        if (!mapped) {
          return std::unexpected(mapped.error());
        }

        id = std::move(*mapped);
        return {};
      };

      if (field == "parent" && !value.is_null()) {
        auto changed = replace(value);

        if (!changed) {
          return std::unexpected(changed.error());
        }
      } else if (field == "skin" && !value.is_null()) {
        for (auto& joint : value["joints"]) {
          auto changed = replace(joint);

          if (!changed) {
            return std::unexpected(changed.error());
          }
        }
      } else if (field == "animation" && !value.is_null()) {
        for (auto& binding : value["targets"]) {
          auto changed = replace(binding["entity"]);

          if (!changed) {
            return std::unexpected(changed.error());
          }
        }
      }

      return value;
    }

    /** Apply only the local delta; keep independent source edits and reject competing leaves.
     * 仅应用本地差异，保留来源独立修改，对竞争叶值拒绝而不覆盖。 */
    auto applyMerge(const std::optional<Json>& old, const std::optional<Json>& local,
                    const std::optional<Json>& incoming) -> Core::Result<std::optional<Json>> {
      if (local == old) {
        return incoming;
      }

      if (incoming == old || incoming == local) {
        return local;
      }

      if (!old || !local || !incoming || !old->is_object() || !local->is_object()
          || !incoming->is_object()) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Source changed an applied value; refresh before applying");
      }

      std::set<std::string> keys;

      for (const auto* object : {&*old, &*local, &*incoming}) {
        for (const auto& item : object->items()) {
          keys.insert(item.key());
        }
      }

      Json result = Json::object();

      for (const auto& key : keys) {
        auto value = applyMerge(member(*old, key), member(*local, key), member(*incoming, key));

        if (!value) {
          return std::unexpected(value.error());
        }

        if (*value) {
          result[key] = std::move(**value);
        }
      }

      return std::optional<Json>{std::move(result)};
    }

    struct OverrideData {
      Json document;
      SceneLink link;
      Nodes expected;
      Values sources;
      std::set<std::uint64_t> added;
    };

    auto overrideData(const Scene& target, EntityId root) -> Core::Result<OverrideData> {
      auto owner = target.entity(root);
      auto links = target.sceneLinks();
      auto encoded = encodeScene(target);

      if (!owner || !links || !encoded) {
        return std::unexpected(!owner ? owner.error() : !links ? links.error() : encoded.error());
      }

      const auto found = std::ranges::find(*links, root.value, &SceneLink::root);

      if (found == links->end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Entity is not a linked scene root");
      }

      OverrideData data{Codec::parseJson(*encoded), *found, {}, {}, {}};
      Values mapping;
      std::vector<std::uint64_t> pending{root.value};

      for (const auto& pair : found->mapping) {
        mapping.emplace(pair.source, pair.target);
        data.sources.emplace(pair.target, pair.source);
        pending.push_back(pair.target);
      }

      data.expected = nodes(Codec::parseJson(found->baseline), mapping, root.value);
      std::map<std::uint64_t, std::vector<std::uint64_t>> children;

      for (const auto& entity : data.document["entities"]) {
        if (!entity["parent"].is_null()) {
          children[Codec::id(entity["parent"])].push_back(Codec::id(entity["id"]));
        }
      }

      std::set<std::uint64_t> reached;

      while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();

        if (!reached.insert(id).second) {
          continue;
        }

        if (const auto child = children.find(id); child != children.end()) {
          pending.insert(pending.end(), child->second.begin(), child->second.end());
        }

        if (id != root.value && !data.sources.contains(id)) {
          data.added.insert(id);
        }
      }

      return data;
    }

    auto escapedToken(std::string_view key) -> std::string {
      std::string result;

      for (const auto character : key) {
        if (character == '~') {
          result += "~0";
        } else if (character == '/') {
          result += "~1";
        } else {
          result += character;
        }
      }

      return result;
    }

    using DifferenceVisitor = std::function<void(std::string, const Json*, const Json*)>;
    using OverrideVisitor = std::function<void(
        const SceneOverrideKey&, std::optional<std::uint64_t>, const Json*, const Json*)>;

    auto propertyDifferences(const Json* before, const Json* after, const std::string& path,
                             const DifferenceVisitor& visit) -> void {
      if (before && after && *before == *after) {
        return;
      }

      if (!before || !after || !before->is_object() || !after->is_object()) {
        visit(path, before, after);
        return;
      }

      std::set<std::string> keys;

      for (const auto* value : {before, after}) {
        for (const auto& item : value->items()) {
          keys.insert(item.key());
        }
      }

      for (const auto& key : keys) {
        if (path.empty() && key == "id") {
          continue;
        }

        const auto old = before->find(key), local = after->find(key);
        propertyDifferences(old == before->end() ? nullptr : &*old,
                            local == after->end() ? nullptr : &*local,
                            path + '/' + escapedToken(key), visit);
      }
    }

    auto visitOverrides(const OverrideData& data, std::optional<std::uint64_t> node,
                        const OverrideVisitor& visit) -> void {
      std::map<std::uint64_t, const Json*> local;

      for (const auto& value : data.document["entities"]) {
        local.emplace(Codec::id(value["id"]), &value);
      }

      for (const auto& [source, expected] : data.expected) {
        const auto id = Codec::id(expected["id"]);

        if (node && *node != id) {
          continue;
        }

        const auto found = local.find(id);

        if (found == local.end()) {
          visit({SceneOverrideKind::DeletedNode, id, {}}, source, &expected, nullptr);
        } else {
          propertyDifferences(&expected, found->second, {},
                              [&](std::string path, const Json* before, const Json* after) {
                                visit({SceneOverrideKind::Property, id, std::move(path)}, source,
                                      before, after);
                              });
        }
      }

      for (const auto id : data.added) {
        if (!node || *node == id) {
          visit({SceneOverrideKind::AddedNode, id, {}}, {}, nullptr, local.at(id));
        }
      }
    }

    struct OverrideKeyLess {
      auto operator()(const SceneOverrideKey& left, const SceneOverrideKey& right) const -> bool {
        return std::tie(left.kind, left.entity, left.path)
               < std::tie(right.kind, right.entity, right.path);
      }
    };

    auto revertOverrides(const Scene& target, EntityId root,
                         std::optional<std::span<const SceneOverrideKey>> keys)
        -> Core::Result<SceneLinkUpdate> {
      auto data = overrideData(target, root);

      if (!data) {
        return std::unexpected(data.error());
      }

      struct Selected {
        SceneOverrideKey key;
        std::optional<Json> baseline;
      };

      std::vector<Selected> selected;
      std::set<std::uint64_t> removed;
      auto& entities = data->document["entities"];
      std::vector<std::uint64_t> created;

      if (keys) {
        if (keys->empty() || keys->size() > 256) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Override revert requires 1..256 keys");
        }

        std::map<SceneOverrideKey, bool, OverrideKeyLess> requested;

        for (const auto& key : *keys) {
          if (!requested.emplace(key, false).second) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate override key");
          }
        }

        visitOverrides(
            *data, {}, [&](const SceneOverrideKey& key, auto, const Json* before, const Json*) {
              auto found = requested.find(key);

              if (found != requested.end()) {
                found->second = true;
                selected.push_back(
                    {key, before ? std::optional<Json>{*before} : std::optional<Json>{}});
              }
            });

        if (std::ranges::any_of(requested, [](const auto& entry) { return !entry.second; })) {
          return Core::fail(Core::ErrorCode::NotFound,
                            "Override key is unknown or no longer active");
        }

        for (const auto& entry : selected) {
          if (entry.key.kind == SceneOverrideKind::AddedNode) {
            removed.insert(entry.key.entity);
          }
        }

        for (const auto& entry : selected) {
          if (entry.key.kind == SceneOverrideKind::DeletedNode) {
            entities.push_back(*entry.baseline);
            created.push_back(entry.key.entity);
          } else if (entry.key.kind == SceneOverrideKind::Property) {
            auto entity = std::ranges::find_if(entities, [&](const auto& value) {
              return Codec::id(value["id"]) == entry.key.entity;
            });
            const Json::json_pointer path(entry.key.path);

            if (entry.baseline) {
              (*entity)[path] = *entry.baseline;
            } else {
              entity->at(path.parent_pointer()).erase(path.back());
            }
          }
        }
        bool changed = true;

        while (changed) {
          changed = false;

          for (const auto& entity : entities) {
            if (!entity["parent"].is_null() && removed.contains(Codec::id(entity["parent"]))) {
              changed = removed.insert(Codec::id(entity["id"])).second || changed;
            }
          }
        }

        if (std::ranges::any_of(removed, [&](auto id) { return data->sources.contains(id); })) {
          return Core::fail(Core::ErrorCode::InvalidState,
                            "Move inherited descendants before removing a local subtree");
        }

      } else {
        removed = data->added;
        std::set<std::uint64_t> live;

        for (const auto& entity : entities) {
          live.insert(Codec::id(entity["id"]));
        }

        std::erase_if(entities.get_ref<Json::array_t&>(), [&](const auto& entity) {
          return data->sources.contains(Codec::id(entity["id"]));
        });

        for (const auto& [source, entity] : data->expected) {
          entities.push_back(entity);
          const auto id = Codec::id(entity["id"]);

          if (!live.contains(id)) {
            created.push_back(id);
          }
        }
      }

      std::erase_if(entities.get_ref<Json::array_t&>(),
                    [&](const auto& entity) { return removed.contains(Codec::id(entity["id"])); });
      auto& links = data->document["links"].get_ref<Json::array_t&>();
      std::erase_if(links,
                    [&](const auto& link) { return removed.contains(Codec::id(link["root"])); });
      auto prepared = decodeScene(data->document.dump());

      if (!prepared) {
        return Core::fail(prepared.error().code,
                          "Revert scene overrides: " + prepared.error().message);
      }

      auto bounded = encodeScene(*prepared);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      const EntityId restoredRoot{prepared->id(), root.value};
      return SceneLinkUpdate{std::move(*prepared), restoredRoot, std::move(created)};
    }
  }  // namespace

  auto findSceneLinkRoot(const Scene& target, EntityId entity)
      -> Core::Result<std::optional<EntityId>> {
    try {
      auto current = target.entity(entity);
      auto links = target.sceneLinks();

      if (!current || !links) {
        return std::unexpected(!current ? current.error() : links.error());
      }

      while (true) {
        for (const auto& link : *links) {
          if (current->id.value == link.root
              || std::ranges::any_of(link.mapping, [&](const auto& pair) {
                   return pair.target == current->id.value;
                 })) {
            return std::optional{EntityId{target.id(), link.root}};
          }
        }

        if (!current->parent) {
          return std::optional<EntityId>{};
        }

        current = target.entity(*current->parent);

        if (!current) {
          return std::unexpected(current.error());
        }
      }
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Find linked scene: " + std::string(error.what()));
    }
  }

  auto inspectSceneOverrides(const Scene& target, EntityId root, std::optional<std::uint64_t> node,
                             std::size_t offset, std::size_t limit)
      -> Core::Result<SceneOverridePage> {
    try {
      if (limit == 0 || limit > 256) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Override page limit must be 1..256");
      }

      auto data = overrideData(target, root);

      if (!data) {
        return std::unexpected(data.error());
      }

      if (node && *node != root.value && !data->sources.contains(*node)
          && !data->added.contains(*node)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Override filter is outside this instance");
      }

      SceneOverridePage page{root, data->link.asset, node, offset, limit, 0, {}, {}};
      visitOverrides(
          *data, node,
          [&](const SceneOverrideKey& key, auto source, const Json* before, const Json* after) {
            const auto index = page.total++;

            if (key.kind == SceneOverrideKind::Property) {
              ++page.fields[key.path.substr(1, key.path.find('/', 1) - 1)];
            }

            if (index >= offset && page.changes.size() < limit) {
              std::optional<SceneSourceKey> sourceKey;

              if (source) {
                sourceKey
                    = std::ranges::find(data->link.mapping, *source, &SceneLinkMapping::source)
                          ->key;
              }

              page.changes.push_back(
                  {key, source, before ? std::optional{before->dump()} : std::nullopt,
                   after ? std::optional{after->dump()} : std::nullopt, std::move(sourceKey)});
            }
          });

      if (offset > page.total) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Override page offset exceeds total");
      }

      return page;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect scene overrides: " + std::string(error.what()));
    }
  }

  auto prepareSceneFieldRevert(const Scene& target, EntityId root, EntityId entity,
                               std::string_view field) -> Core::Result<SceneLinkUpdate> {
    try {
      auto live = target.entity(entity);

      if (!live) {
        return std::unexpected(live.error());
      }

      auto data = overrideData(target, root);

      if (!data) {
        return std::unexpected(data.error());
      }

      const auto source = data->sources.find(entity.value);

      if (source == data->sources.end()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Field revert requires an inherited node of this instance");
      }

      const auto& expected = data->expected.at(source->second);

      if (field == "id" || !expected.contains(field)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown or immutable scene field");
      }

      auto& entities = data->document["entities"];
      auto local = std::ranges::find_if(
          entities, [&](const auto& value) { return Codec::id(value["id"]) == entity.value; });
      (*local)[field] = expected[field];
      auto prepared = decodeScene(data->document.dump());

      if (!prepared) {
        return Core::fail(prepared.error().code, "Revert scene field: " + prepared.error().message);
      }

      auto bounded = encodeScene(*prepared);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      const EntityId restoredRoot{prepared->id(), root.value};
      return SceneLinkUpdate{std::move(*prepared), restoredRoot, {}};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Revert scene field: " + std::string(error.what()));
    }
  }

  auto prepareSceneFieldApply(const Scene& target, const SceneSourceSnapshot& source, EntityId root,
                              EntityId entity, std::string_view field, std::uint64_t firstValue)
      -> Core::Result<SceneSourceApply> {
    try {
      auto valid = validateSceneSourceSnapshot(source);
      auto live = target.entity(entity);
      auto data = overrideData(target, root);
      auto canonical = canonicalSource(source);
      auto floor = target.nextEntityValue();
      auto sourceText = encodeScene(source.scene);

      if (!valid || !live || !data || !canonical || !floor || !sourceText) {
        return std::unexpected(!valid       ? valid.error()
                               : !live      ? live.error()
                               : !data      ? data.error()
                               : !canonical ? canonical.error()
                               : !floor     ? floor.error()
                                            : sourceText.error());
      }

      if (data->link.asset != source.asset || firstValue == 0
          || firstValue < floor->value_or(std::numeric_limits<std::uint64_t>::max())) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Field apply requires the matching source and reserved allocation floor");
      }

      const auto owned = data->sources.find(entity.value);

      if (owned == data->sources.end()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Field apply requires a live inherited node of this instance");
      }

      const auto& expected = data->expected.at(owned->second);

      if (field == "id" || !expected.contains(field)) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown or immutable scene field");
      }

      std::map<std::uint64_t, SceneSourceKey> targetKeys, sourceKeys;
      std::map<SceneSourceKey, std::uint64_t> sourceValues;
      std::vector<SceneSourceOrigin> previous;

      for (const auto& pair : data->link.mapping) {
        targetKeys.emplace(pair.target, pair.key);
        previous.push_back({pair.source, pair.key});
      }

      for (const auto& origin : source.origins) {
        sourceKeys.emplace(origin.entity, origin.key);
        sourceValues.emplace(origin.key, origin.entity);
      }

      valid = validateSceneSourceUpdate(source.asset, previous, data->link.namespaces,
                                        canonical->origins, source.namespaces);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      const auto& key = targetKeys.at(entity.value);
      const auto incomingValue = sourceValues.find(key);

      if (incomingValue == sourceValues.end()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Source removed the applied node");
      }

      auto sourceDocument = Codec::parseJson(*sourceText);
      auto incoming = std::ranges::find_if(sourceDocument["entities"], [&](const auto& value) {
        return Codec::id(value["id"]) == incomingValue->second;
      });
      auto local = std::ranges::find_if(data->document["entities"], [&](const auto& value) {
        return Codec::id(value["id"]) == entity.value;
      });
      const auto originalField = (*incoming)[field];
      const auto localField = (*local)[field];
      const bool primary = field == "camera" || field == "audioListener";

      if (primary && !localField.is_null() && localField["primary"].get<bool>()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Primary flags are placement-local; edit the source scene directly");
      }

      if (field == "parent" && localField.is_null()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "A parent outside the placement cannot be applied to its source");
      }

      const auto remap = [&](Json value, const Reference& reference) -> Core::Result<Json> {
        return remapField(std::move(value), field, reference);
      };
      const auto logical = [&](const auto& keys, bool targetSpace) -> Reference {
        return [&, targetSpace, keys = &keys](std::uint64_t id) -> Core::Result<Json> {
          if (targetSpace && field == "parent" && id == root.value) {
            return Json(nullptr);
          }

          const auto found = keys->find(id);

          if (found == keys->end()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Applied native reference is outside this source instance");
          }

          return Json(found->second);
        };
      };
      auto baselineField = remap(expected[field], logical(targetKeys, true));
      auto normalizedLocal = remap(localField, logical(targetKeys, true));
      auto normalizedSource = remap(originalField, logical(sourceKeys, false));

      if (!baselineField || !normalizedLocal || !normalizedSource) {
        return std::unexpected(!baselineField     ? baselineField.error()
                               : !normalizedLocal ? normalizedLocal.error()
                                                  : normalizedSource.error());
      }

      if (primary && !normalizedSource->is_null()) {
        (*normalizedSource)["primary"] = false;
      }

      if (*normalizedLocal == *baselineField) {
        auto unchanged = decodeScene(data->document.dump());
        auto sourceCopy = decodeScene(*sourceText);

        if (!unchanged || !sourceCopy) {
          return std::unexpected(!unchanged ? unchanged.error() : sourceCopy.error());
        }

        auto reserved = unchanged->reserveEntityValuesBefore(
            firstValue == std::numeric_limits<std::uint64_t>::max() ? std::optional<std::uint64_t>{}
                                                                    : std::optional{firstValue});

        if (!reserved) {
          return std::unexpected(reserved.error());
        }

        const EntityId copiedRoot{unchanged->id(), root.value};
        return SceneSourceApply{
            std::move(*sourceCopy), {std::move(*unchanged), copiedRoot, {}}, false};
      }

      if (*normalizedSource != *baselineField && *normalizedSource != *normalizedLocal) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Source field changed since baseline; refresh before applying");
      }

      auto appliedField = remap(localField, [&](std::uint64_t id) -> Core::Result<Json> {
        if (field == "parent" && id == root.value) {
          return Json(nullptr);
        }

        const auto origin = targetKeys.find(id);
        const auto value
            = origin == targetKeys.end() ? sourceValues.end() : sourceValues.find(origin->second);

        if (value == sourceValues.end()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Applied native reference has no current source node");
        }

        return Json(std::to_string(value->second));
      });

      if (!appliedField) {
        return std::unexpected(appliedField.error());
      }

      if (primary && !appliedField->is_null() && !originalField.is_null()) {
        (*appliedField)["primary"] = originalField["primary"];
      }

      const bool sourceChanged = *appliedField != originalField;
      (*incoming)[field] = std::move(*appliedField);
      auto preparedSource = decodeScene(sourceDocument.dump());

      if (!preparedSource) {
        return Core::fail(preparedSource.error().code,
                          "Applied source field: " + preparedSource.error().message);
      }

      if (sourceChanged) {
        const auto space = std::ranges::find_if(
            source.namespaces, [](const auto& value) { return value.prefix.empty(); });
        const auto sourceFloor = preparedSource->nextEntityValue()->value_or(
            std::numeric_limits<std::uint64_t>::max());
        const auto next = std::max(
            sourceFloor, space->nextEntity.value_or(std::numeric_limits<std::uint64_t>::max()));
        auto reserved = preparedSource->reserveEntityValuesBefore(
            next == std::numeric_limits<std::uint64_t>::max() ? std::optional<std::uint64_t>{}
                                                              : std::optional{next});

        if (!reserved) {
          return std::unexpected(reserved.error());
        }
      }

      auto bounded = encodeScene(*preparedSource);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      auto updated = [&]() -> Core::Result<SceneLinkUpdate> {
        if (!sourceChanged) {
          return prepareSceneLinkUpdate(target, source, root, firstValue);
        }

        auto persisted = captureSceneSource(*preparedSource, source.asset);

        if (!persisted) {
          return std::unexpected(persisted.error());
        }

        auto checkedIdentity
            = validateSceneSourceUpdate(source.asset, source.origins, source.namespaces,
                                        persisted->origins, persisted->namespaces);
        return checkedIdentity
                   ? prepareSceneLinkUpdate(target, *persisted, root, firstValue)
                   : Core::Result<SceneLinkUpdate>{std::unexpected(checkedIdentity.error())};
      }();

      if (!updated) {
        return std::unexpected(updated.error());
      }

      return SceneSourceApply{std::move(*preparedSource), std::move(*updated), sourceChanged};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Apply scene field to source: " + std::string(error.what()));
    }
  }

  auto prepareSceneInstanceApply(const Scene& target, const SceneSourceSnapshot& source,
                                 EntityId root, std::uint64_t firstValue)
      -> Core::Result<SceneSourceApply> {
    try {
      auto valid = validateSceneSourceSnapshot(source);
      auto data = overrideData(target, root);
      auto canonical = canonicalSource(source);
      auto sourceText = encodeScene(source.scene);
      auto floor = target.nextEntityValue();

      if (!valid || !data || !canonical || !sourceText || !floor) {
        return std::unexpected(!valid        ? valid.error()
                               : !data       ? data.error()
                               : !canonical  ? canonical.error()
                               : !sourceText ? sourceText.error()
                                             : floor.error());
      }

      const auto maximum = std::numeric_limits<std::uint64_t>::max();

      if (data->link.asset != source.asset || firstValue == 0
          || firstValue < floor->value_or(maximum)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Instance apply requires the matching source and allocation floor");
      }

      std::vector<SceneSourceOrigin> previous;
      std::map<std::uint64_t, SceneSourceKey> targetKeys, sourceKeys;
      std::map<SceneSourceKey, std::uint64_t> oldTargets, sourceValues;

      for (const auto& pair : data->link.mapping) {
        previous.push_back({pair.source, pair.key});
        targetKeys.emplace(pair.target, pair.key);
        oldTargets.emplace(pair.key, pair.target);
      }

      for (const auto& origin : source.origins) {
        sourceKeys.emplace(origin.entity, origin.key);
        sourceValues.emplace(origin.key, origin.entity);
      }

      valid = validateSceneSourceUpdate(source.asset, previous, data->link.namespaces,
                                        canonical->origins, source.namespaces);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      Nodes local;

      for (const auto& entity : data->document["entities"]) {
        local.emplace(Codec::id(entity["id"]), entity);
      }

      bool changed = !data->added.empty();

      for (const auto& [value, index] : data->sources) {
        const auto found = local.find(value);
        changed |= found == local.end() || found->second != data->expected.at(index);
      }

      if (!changed) {
        auto copy = decodeScene(data->document.dump());
        auto sourceCopy = decodeScene(*sourceText);

        if (!copy || !sourceCopy) {
          return std::unexpected(!copy ? copy.error() : sourceCopy.error());
        }

        auto reserved = copy->reserveEntityValuesBefore(
            firstValue == maximum ? std::optional<std::uint64_t>{} : std::optional{firstValue});

        if (!reserved) {
          return std::unexpected(reserved.error());
        }

        const EntityId copiedRoot{copy->id(), root.value};
        return SceneSourceApply{std::move(*sourceCopy), {std::move(*copy), copiedRoot, {}}, false};
      }

      auto sourceDocument = Codec::parseJson(*sourceText);
      Nodes incoming;

      for (const auto& entity : sourceDocument["entities"]) {
        incoming.emplace(Codec::id(entity["id"]), entity);
      }

      std::set<std::uint64_t> removed, copiedRoots;
      Values additions;
      const auto rootSpace = std::ranges::find_if(
          source.namespaces, [](const auto& space) { return space.prefix.empty(); });
      auto nextSource = std::max(source.scene.nextEntityValue()->value_or(maximum),
                                 rootSpace->nextEntity.value_or(maximum));
      bool sourceChanged = false;
      const auto allocate = [&](std::uint64_t targetValue) -> Core::Result<void> {
        if (!additions.contains(targetValue)) {
          if (nextSource == maximum) {
            return Core::fail(Core::ErrorCode::Unavailable, "Source allocation is exhausted");
          }

          additions.emplace(targetValue, nextSource++);
        }

        return {};
      };

      for (const auto value : data->added) {
        valid = allocate(value);

        if (!valid) {
          return std::unexpected(valid.error());
        }
      }

      /** New local links become nested source links; all their live/reserved physical values
       * remap together. Foreign link ownership cannot be captured through one moved child.
       * 本地新关联成为来源内嵌关联，存活及预留物理值一起重映射，不能通过移动单个子节点夺取外部关联。
       */
      for (auto link : data->document.value("links", Json::array())) {
        const auto linkedRoot = Codec::id(link["root"]);

        if (linkedRoot == root.value) {
          continue;
        }

        if (!data->added.contains(linkedRoot)) {
          for (const auto& mapping : link["mapping"]) {
            if (data->added.contains(Codec::id(mapping["target"]))) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Added node is owned by an external scene link");
            }
          }

          continue;
        }

        if (link["asset"] == source.asset) {
          return Core::fail(Core::ErrorCode::InvalidState, "Applied scene link would cycle");
        }

        copiedRoots.insert(linkedRoot);
        link["root"] = std::to_string(additions.at(linkedRoot));

        for (auto& mapping : link["mapping"]) {
          const auto value = Codec::id(mapping["target"]);

          if (local.contains(value) && !data->added.contains(value)) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Applied nested link overlaps another instance");
          }

          valid = allocate(value);

          if (!valid) {
            return std::unexpected(valid.error());
          }

          mapping["target"] = std::to_string(additions.at(value));
        }

        if (!sourceDocument.contains("links")) {
          sourceDocument["links"] = Json::array();
        }

        sourceDocument["links"].push_back(std::move(link));
      }

      const auto logical = [&](bool targetSpace, std::string_view field) -> Reference {
        return [&, targetSpace, field](std::uint64_t id) -> Core::Result<Json> {
          if (targetSpace && field == "parent" && id == root.value) {
            return Json(nullptr);
          }

          const auto& keys = targetSpace ? targetKeys : sourceKeys;
          const auto found = keys.find(id);

          if (found != keys.end()) {
            return Json(found->second);
          }

          if (targetSpace && data->added.contains(id)) {
            return Json{{"added", std::to_string(id)}};
          }

          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Applied native reference is outside the instance");
        };
      };
      const auto materialize = [&](Json value, std::string_view field) -> Core::Result<Json> {
        const auto replace = [&](Json& token) -> Core::Result<void> {
          if (token.is_null()) {
            return {};
          }

          if (token.is_object()) {
            token = std::to_string(additions.at(Codec::id(token["added"])));
            return {};
          }

          const auto key = token.get<SceneSourceKey>();
          const auto found = sourceValues.find(key);

          if (found == sourceValues.end()) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Applied reference was removed from source");
          }

          token = std::to_string(found->second);
          return {};
        };
        Core::Result<void> result;

        if (field == "parent") {
          result = replace(value);
        } else if (field == "skin" && !value.is_null()) {
          for (auto& joint : value["joints"]) {
            result = replace(joint);

            if (!result) {
              break;
            }
          }
        } else if (field == "animation" && !value.is_null()) {
          for (auto& binding : value["targets"]) {
            result = replace(binding["entity"]);

            if (!result) {
              break;
            }
          }
        }

        return result ? Core::Result<Json>{std::move(value)} : std::unexpected(result.error());
      };
      const auto normalized = [&](const Json& node, bool targetSpace) -> Core::Result<Json> {
        Json result = node;
        result.erase("id");

        for (auto& item : result.items()) {
          auto value = remapField(item.value(), item.key(), logical(targetSpace, item.key()));

          if (!value) {
            return std::unexpected(value.error());
          }

          item.value() = std::move(*value);
        }

        for (const auto* field : {"camera", "audioListener"}) {
          if (!result[field].is_null()) {
            if (targetSpace && result[field]["primary"].get<bool>()) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Primary flags are placement-local; edit the source directly");
            }

            result[field]["primary"] = false;
          }
        }

        if (targetSpace && node["parent"].is_null()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "A parent outside the placement cannot be applied");
        }

        return result;
      };

      for (const auto& [targetValue, index] : data->sources) {
        const auto& key = targetKeys.at(targetValue);
        const auto found = sourceValues.find(key);
        const auto current = local.find(targetValue);
        auto old = normalized(data->expected.at(index), true);

        if (!old) {
          return std::unexpected(old.error());
        }

        if (current == local.end()) {
          if (found != sourceValues.end()) {
            auto actual = normalized(incoming.at(found->second), false);

            if (!actual || *actual != *old) {
              return actual ? Core::fail(Core::ErrorCode::InvalidState,
                                         "Source changed a locally deleted node")
                            : std::unexpected(actual.error());
            }

            removed.insert(found->second);
            incoming.erase(found->second);
            sourceChanged = true;
          }

          continue;
        }

        auto localNode = normalized(current->second, true);

        if (!localNode) {
          return std::unexpected(localNode.error());
        }

        if (found == sourceValues.end()) {
          if (*localNode != *old) {
            return Core::fail(Core::ErrorCode::InvalidState, "Source removed an applied node");
          }

          continue;
        }

        auto actual = normalized(incoming.at(found->second), false);
        auto merged = actual ? applyMerge(*old, *localNode, *actual)
                             : Core::Result<std::optional<Json>>{std::unexpected(actual.error())};

        if (!merged) {
          return std::unexpected(merged.error());
        }

        auto result = std::move(**merged);

        for (auto& item : result.items()) {
          auto value = materialize(item.value(), item.key());

          if (!value) {
            return std::unexpected(value.error());
          }

          item.value() = std::move(*value);
        }

        for (const auto* field : {"camera", "audioListener"}) {
          const auto& prior = incoming.at(found->second)[field];

          if (!result[field].is_null() && !prior.is_null()) {
            result[field]["primary"] = prior["primary"];
          }
        }

        result["id"] = std::to_string(found->second);
        sourceChanged |= result != incoming.at(found->second);
        incoming[found->second] = std::move(result);
      }

      for (const auto targetValue : data->added) {
        auto node = normalized(local.at(targetValue), true);

        if (!node) {
          return std::unexpected(node.error());
        }

        for (auto& item : node->items()) {
          auto value = materialize(item.value(), item.key());

          if (!value) {
            return std::unexpected(value.error());
          }

          item.value() = std::move(*value);
        }

        (*node)["id"] = std::to_string(additions.at(targetValue));
        incoming.emplace(additions.at(targetValue), std::move(*node));
        sourceChanged = true;
      }

      if (sourceDocument.contains("links")) {
        std::erase_if(sourceDocument["links"].get_ref<Json::array_t&>(),
                      [&](const auto& link) { return removed.contains(Codec::id(link["root"])); });
      }

      sourceDocument["entities"] = Json::array();

      for (auto& [value, node] : incoming) {
        sourceDocument["entities"].push_back(std::move(node));
      }

      if (sourceChanged) {
        sourceDocument["nextEntity"]
            = nextSource == maximum ? Json(nullptr) : Json(std::to_string(nextSource));
      }
      auto preparedSource = decodeScene(sourceDocument.dump());

      if (!preparedSource) {
        return Core::fail(preparedSource.error().code,
                          "Applied source topology: " + preparedSource.error().message);
      }

      auto bounded = encodeScene(*preparedSource);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      auto persisted
          = sourceChanged
                ? captureSceneSource(*preparedSource, source.asset)
                : Core::Result<SceneSourceSnapshot>{SceneSourceSnapshot{
                      std::move(*preparedSource), source.asset, source.origins, source.namespaces}};

      if (!persisted) {
        return std::unexpected(persisted.error());
      }

      auto final = canonicalSource(*persisted);

      if (!final) {
        return std::unexpected(final.error());
      }

      valid = validateSceneSourceUpdate(source.asset, source.origins, source.namespaces,
                                        persisted->origins, persisted->namespaces);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      valid = validateSceneSourceUpdate(source.asset, previous, data->link.namespaces,
                                        final->origins, persisted->namespaces);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      Values physicalTargets, mapping;
      std::map<SceneSourceKey, std::uint64_t> targets;
      std::vector<std::uint64_t> created;
      auto next = firstValue;

      for (const auto& [targetValue, sourceValue] : additions) {
        physicalTargets.emplace(sourceValue, targetValue);
      }

      for (const auto& origin : persisted->origins) {
        const auto promoted = physicalTargets.find(origin.entity);
        const auto old = oldTargets.find(origin.key);
        std::uint64_t value;

        if (promoted != physicalTargets.end()) {
          value = promoted->second;
        } else if (old != oldTargets.end()) {
          value = old->second;
        } else {
          if (next == maximum) {
            return Core::fail(Core::ErrorCode::Unavailable, "Instance allocation is exhausted");
          }

          value = next++;
          created.push_back(value);
        }

        targets.emplace(origin.key, value);
      }

      auto link = data->link;
      link.baseline = std::move(final->text);
      link.mapping.clear();
      link.namespaces = persisted->namespaces;

      for (const auto& origin : final->origins) {
        const auto value = targets.at(origin.key);
        mapping.emplace(origin.entity, value);
        link.mapping.push_back({origin.entity, value, origin.key});
      }

      const auto remapped = nodes(Codec::parseJson(link.baseline), mapping, root.value);
      auto& entities = data->document["entities"].get_ref<Json::array_t&>();
      std::erase_if(entities, [&](const auto& node) {
        const auto value = Codec::id(node["id"]);
        return data->sources.contains(value) || data->added.contains(value);
      });

      for (const auto& [value, node] : remapped) {
        entities.push_back(node);
      }

      auto links = target.sceneLinks().value();
      std::erase_if(links, [&](const auto& entry) { return copiedRoots.contains(entry.root); });
      *std::ranges::find(links, root.value, &SceneLink::root) = std::move(link);
      data->document.erase("links");
      data->document["nextEntity"] = next == maximum ? Json(nullptr) : Json(std::to_string(next));
      auto instance = decodeScene(data->document.dump());

      if (!instance) {
        return Core::fail(instance.error().code,
                          "Applied instance topology: " + instance.error().message);
      }

      valid = instance->setSceneLinks(links);
      auto encoded = valid ? encodeScene(*instance)
                           : Core::Result<std::string>{std::unexpected(valid.error())};

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      const EntityId copiedRoot{instance->id(), root.value};
      return SceneSourceApply{std::move(persisted->scene),
                              {std::move(*instance), copiedRoot, std::move(created)},
                              sourceChanged};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Apply scene instance to source: " + std::string(error.what()));
    }
  }

  auto prepareSceneOverrideRevert(const Scene& target, EntityId root,
                                  std::span<const SceneOverrideKey> keys)
      -> Core::Result<SceneLinkUpdate> {
    try {
      return revertOverrides(target, root, keys);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Revert scene overrides: " + std::string(error.what()));
    }
  }

  auto prepareSceneBaselineRevert(const Scene& target, EntityId root)
      -> Core::Result<SceneLinkUpdate> {
    try {
      return revertOverrides(target, root, {});
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Revert scene baseline: " + std::string(error.what()));
    }
  }

  auto prepareLinkedScene(const Scene& target, const Scene& source, std::string_view asset,
                          std::uint64_t firstValue, const Transform& placement,
                          std::optional<EntityId> parent) -> Core::Result<SceneContentResult> {
    auto snapshot = captureSceneSource(source, asset);

    if (!snapshot) {
      return std::unexpected(snapshot.error());
    }

    auto result = prepareLinkedScene(target, *snapshot, firstValue, placement, parent);

    if (result) {
      for (auto& mapping : result->entities) {
        mapping.source.scene = source.id();
      }
    }

    return result;
  }

  auto prepareLinkedScene(const Scene& target, const SceneSourceSnapshot& snapshot,
                          std::uint64_t firstValue, const Transform& placement,
                          std::optional<EntityId> parent) -> Core::Result<SceneContentResult> {
    try {
      auto valid = validateSceneSourceSnapshot(snapshot);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      const auto& source = snapshot.scene;
      auto result = prepareSceneInstance(target, source, firstValue, placement, parent);
      auto text = canonicalSource(snapshot);

      if (!result || !text) {
        return std::unexpected(!result ? result.error() : text.error());
      }

      SceneLink link{
          result->root.value, snapshot.asset, std::move(text->text), {}, snapshot.namespaces};
      std::map<std::uint64_t, SceneSourceKey> keys;
      std::map<SceneSourceKey, std::uint64_t> canonical;

      for (const auto& origin : snapshot.origins) {
        keys.emplace(origin.entity, origin.key);
      }

      for (const auto& origin : text->origins) {
        canonical.emplace(origin.key, origin.entity);
      }

      for (const auto& mapping : result->entities) {
        const auto& key = keys.at(mapping.source.value);
        link.mapping.push_back({canonical.at(key), mapping.target.value, key});
      }

      auto links = result->scene.sceneLinks().value();
      links.push_back(std::move(link));
      auto changed = result->scene.setSceneLinks(links);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      auto bounded = encodeScene(result->scene);
      return bounded ? std::move(result)
                     : Core::Result<SceneContentResult>{std::unexpected(bounded.error())};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Link scene: " + std::string(error.what()));
    }
  }

  auto prepareSceneLinkUpdate(const Scene& target, const Scene& source, EntityId root,
                              std::uint64_t firstValue, bool revert)
      -> Core::Result<SceneLinkUpdate> {
    auto owner = target.entity(root);
    auto links = target.sceneLinks();

    if (!owner || !links) {
      return std::unexpected(!owner ? owner.error() : links.error());
    }

    const auto found = std::ranges::find(*links, root.value, &SceneLink::root);

    if (found == links->end()) {
      return Core::fail(Core::ErrorCode::NotFound, "Entity is not a linked scene root");
    }

    auto snapshot = captureSceneSource(source, found->asset);
    return snapshot ? prepareSceneLinkUpdate(target, *snapshot, root, firstValue, revert)
                    : Core::Result<SceneLinkUpdate>{std::unexpected(snapshot.error())};
  }

  struct SceneLinkBatch::Impl {
    std::thread::id m_owner{std::this_thread::get_id()};
    std::uint64_t m_scope{};
    std::uint64_t m_next{};
    std::uint64_t m_last{};
    std::size_t m_count{};
    Json m_document;
    Nodes m_nodes;
    std::vector<SceneLink> m_links;
    std::vector<std::uint64_t> m_created;
    std::optional<Core::Error> m_fault;
    bool m_finished{};

    auto check() const -> Core::Result<void> {
      if (m_owner != std::this_thread::get_id()) {
        return Core::fail(Core::ErrorCode::InvalidState, "Scene link batch requires its owner");
      }

      if (m_fault) {
        return std::unexpected(*m_fault);
      }

      return m_finished ? Core::fail(Core::ErrorCode::InvalidState, "Scene link batch is finished")
                        : Core::Result<void>{};
    }

    auto storage(const SceneLink& replaced, const SceneSourceSnapshot& source,
                 std::size_t bytes) const -> Core::Result<void> {
      std::size_t mappings = source.origins.size();
      std::size_t spaces = source.namespaces.size();
      std::size_t observed{};

      for (const auto& space : source.namespaces) {
        observed += space.entities.size();
      }

      for (const auto& link : m_links) {
        if (link.root == replaced.root) {
          continue;
        }

        bytes += link.baseline.size();
        mappings += link.mapping.size();
        spaces += link.namespaces.size();

        for (const auto& space : link.namespaces) {
          observed += space.entities.size();
        }
      }

      if (bytes > 64 * 1024 * 1024 || mappings > 10000 || spaces > 10000 || observed > 100000) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Scene link batch exceeds native storage budgets");
      }

      return {};
    }

    auto update(const SceneSourceSnapshot& snapshot, EntityId root, SceneLinkAction action)
        -> Core::Result<void> {
      if (action != SceneLinkAction::Refresh && action != SceneLinkAction::RestoreSource
          && action != SceneLinkAction::Retarget) {
        return Core::fail(Core::ErrorCode::InvalidArgument, "Unknown scene link action");
      }

      if (m_count == 256 || root.scene != m_scope || !m_nodes.contains(root.value)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid scene link batch root or capacity");
      }

      auto valid = validateSceneSourceSnapshot(snapshot);

      if (!valid) {
        return valid;
      }

      auto text = canonicalSource(snapshot);

      if (!text) {
        return std::unexpected(text.error());
      }

      auto found = std::ranges::find(m_links, root.value, &SceneLink::root);
      const bool revert = action == SceneLinkAction::RestoreSource;
      const bool retarget = action == SceneLinkAction::Retarget;

      if (found == m_links.end() || (!retarget && found->asset != snapshot.asset)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Linked source asset does not match the instance");
      }

      valid = storage(*found, snapshot, text->text.size());

      if (!valid) {
        return valid;
      }

      if (retarget) {
        found->asset = snapshot.asset;

        for (auto& space : found->namespaces) {
          if (space.prefix.empty()) {
            space.asset = snapshot.asset;
          }
        }
      }

      const auto maximum = std::numeric_limits<std::uint64_t>::max();
      std::vector<SceneSourceOrigin> previousOrigins;
      Values oldMapping, mapping;
      std::map<SceneSourceKey, SceneLinkMapping> oldKeys;
      std::map<std::uint64_t, SceneSourceKey> incomingKeys;
      std::map<SceneSourceKey, std::uint64_t> incomingValues;

      for (const auto& pair : found->mapping) {
        previousOrigins.push_back({pair.source, pair.key});
        oldMapping.emplace(pair.source, pair.target);
        oldKeys.emplace(pair.key, pair);
      }

      valid = validateSceneSourceUpdate(snapshot.asset, previousOrigins, found->namespaces,
                                        text->origins, snapshot.namespaces);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      for (const auto& origin : text->origins) {
        incomingKeys.emplace(origin.entity, origin.key);
        incomingValues.emplace(origin.key, origin.entity);
      }

      const auto incoming = Codec::parseJson(text->text);
      auto& next = m_next;
      auto& created = m_created;

      for (const auto& entity : incoming["entities"]) {
        const auto id = Codec::id(entity["id"]);
        const auto old = oldKeys.find(incomingKeys.at(id));

        if (old != oldKeys.end()) {
          mapping.emplace(id, old->second.target);
        } else {
          if (next == maximum) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Linked scene entity allocation is exhausted");
          }

          mapping.emplace(id, next);
          created.push_back(next++);
        }
      }

      const auto previous = nodes(Codec::parseJson(found->baseline), oldMapping, root.value);
      const auto current = nodes(incoming, mapping, root.value);
      auto& local = m_nodes;

      for (const auto& pair : found->mapping) {
        const auto entry = local.find(pair.target);

        if (!incomingValues.contains(pair.key)) {
          if (entry != local.end() && !revert && entry->second != previous.at(pair.source)) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Source removed an overridden node: " + std::to_string(pair.source));
          }

          local.erase(pair.target);
        }
      }

      for (const auto& [id, value] : current) {
        const auto old = oldKeys.find(incomingKeys.at(id));
        const auto entry = local.find(mapping.at(id));

        if (old == oldKeys.end() || revert) {
          if (entry == local.end() && old != oldKeys.end()) {
            created.push_back(mapping.at(id));
          }

          local[mapping.at(id)] = value;
        } else if (entry != local.end()) {
          entry->second = *merge(std::optional<Json>{previous.at(old->second.source)},
                                 std::optional<Json>{entry->second}, std::optional<Json>{value});
        }
      }

      if (m_nodes.size() > 10000) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Scene link batch exceeds 10000 working nodes");
      }

      found->baseline = std::move(text->text);
      std::vector<SceneLinkMapping> nextMapping;
      nextMapping.reserve(mapping.size());

      for (const auto& [sourceValue, targetValue] : mapping) {
        nextMapping.push_back({sourceValue, targetValue, incomingKeys.at(sourceValue)});
      }

      auto nextNamespaces = snapshot.namespaces;
      found->mapping = std::move(nextMapping);
      found->namespaces = std::move(nextNamespaces);

      std::erase_if(m_created, [&](auto value) { return !m_nodes.contains(value); });
      m_last = root.value;
      ++m_count;
      return {};
    }
  };

  SceneLinkBatch::SceneLinkBatch(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  SceneLinkBatch::SceneLinkBatch(SceneLinkBatch&&) noexcept = default;

  auto SceneLinkBatch::operator=(SceneLinkBatch&&) noexcept -> SceneLinkBatch& = default;

  SceneLinkBatch::~SceneLinkBatch() = default;

  auto SceneLinkBatch::begin(const Scene& target, std::uint64_t firstValue)
      -> Core::Result<SceneLinkBatch> {
    try {
      auto links = target.sceneLinks();
      auto floor = target.nextEntityValue();
      auto encoded = encodeScene(target);

      if (!links || !floor || !encoded) {
        return std::unexpected(!links ? links.error() : !floor ? floor.error() : encoded.error());
      }

      if (firstValue == 0
          || firstValue < floor->value_or(std::numeric_limits<std::uint64_t>::max())) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Refresh requires the reserved allocation floor");
      }

      auto impl = std::make_unique<Impl>();
      impl->m_scope = target.id();
      impl->m_next = firstValue;
      impl->m_links = std::move(*links);
      impl->m_document = Codec::parseJson(*encoded);

      for (auto& entity : impl->m_document["entities"]) {
        const auto id = Codec::id(entity["id"]);
        impl->m_nodes.emplace(id, std::move(entity));
      }

      impl->m_document.erase("entities");
      impl->m_document.erase("links");
      return SceneLinkBatch{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin scene link batch: " + std::string(error.what()));
    }
  }

  auto SceneLinkBatch::sourcePath(EntityId root) const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene link batch was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (root.scene != m_impl->m_scope) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene link root belongs to another scope");
    }

    const auto found = std::ranges::find(m_impl->m_links, root.value, &SceneLink::root);
    return found == m_impl->m_links.end()
               ? Core::Result<std::string>{Core::fail(Core::ErrorCode::NotFound,
                                                      "Entity is not a linked scene root")}
               : Core::Result<std::string>{found->asset};
  }

  auto SceneLinkBatch::append(const SceneSourceSnapshot& source, EntityId root,
                              SceneLinkAction action) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene link batch was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return checked;
    }

    try {
      auto result = m_impl->update(source, root, action);

      if (!result) {
        m_impl->m_fault = result.error();
      }

      return result;
    } catch (const std::exception& error) {
      m_impl->m_fault = Core::fail(Core::ErrorCode::External,
                                   "Prepare scene link batch: " + std::string(error.what()))
                            .error();
      return std::unexpected(*m_impl->m_fault);
    }
  }

  auto SceneLinkBatch::finish() -> Core::Result<SceneLinkUpdate> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Scene link batch was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    m_impl->m_finished = true;

    try {
      if (m_impl->m_count == 0) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Scene link batch requires at least one change");
      }

      auto& document = m_impl->m_document;
      document["entities"] = Json::array();

      for (auto& [id, value] : m_impl->m_nodes) {
        document["entities"].push_back(std::move(value));
      }

      const auto next = m_impl->m_next;
      document["nextEntity"] = next == std::numeric_limits<std::uint64_t>::max()
                                   ? Json(nullptr)
                                   : Json(std::to_string(next));
      auto prepared = decodeScene(document.dump());

      if (!prepared) {
        return std::unexpected(prepared.error());
      }

      auto linked = prepared->setSceneLinks(m_impl->m_links);

      if (!linked) {
        return std::unexpected(linked.error());
      }

      auto bounded = encodeScene(*prepared);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      const EntityId root{prepared->id(), m_impl->m_last};
      return SceneLinkUpdate{std::move(*prepared), root, std::move(m_impl->m_created)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Finish scene link batch: " + std::string(error.what()));
    }
  }

  namespace {
    auto prepareLinkChange(const Scene& target, const SceneSourceSnapshot& source, EntityId root,
                           std::uint64_t firstValue, SceneLinkAction action)
        -> Core::Result<SceneLinkUpdate> {
      auto batch = SceneLinkBatch::begin(target, firstValue);

      if (!batch) {
        return std::unexpected(batch.error());
      }

      auto appended = batch->append(source, root, action);
      return appended ? batch->finish()
                      : Core::Result<SceneLinkUpdate>{std::unexpected(appended.error())};
    }
  }  // namespace

  auto prepareSceneLinkUpdate(const Scene& target, const SceneSourceSnapshot& source, EntityId root,
                              std::uint64_t firstValue, bool revert)
      -> Core::Result<SceneLinkUpdate> {
    return prepareLinkChange(target, source, root, firstValue,
                             revert ? SceneLinkAction::RestoreSource : SceneLinkAction::Refresh);
  }

  auto prepareSceneLinkRetarget(const Scene& target, const SceneSourceSnapshot& source,
                                EntityId root, std::uint64_t firstValue)
      -> Core::Result<SceneLinkUpdate> {
    return prepareLinkChange(target, source, root, firstValue, SceneLinkAction::Retarget);
  }
}  // namespace Dot::World
