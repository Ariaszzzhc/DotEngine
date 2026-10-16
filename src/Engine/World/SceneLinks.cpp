module;

#include <algorithm>
#include <exception>
#include <expected>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.World.SceneLinks;

import Dot.Core.Result;
import Dot.World.Scene;
import Dot.World.SceneContent;
import Dot.World.SceneFiles;

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
        entity["parent"] = std::to_string(
            entity["parent"].is_null() ? root : mapping.at(Codec::id(entity["parent"])));

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

      SceneOverridePage page{root, data->link.asset, node, offset, limit, 0, {}};
      visitOverrides(
          *data, node,
          [&](const SceneOverrideKey& key, auto source, const Json* before, const Json* after) {
            const auto index = page.total++;

            if (index >= offset && page.changes.size() < limit) {
              page.changes.push_back({key, source,
                                      before ? std::optional{before->dump()} : std::nullopt,
                                      after ? std::optional{after->dump()} : std::nullopt});
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
    try {
      auto result = prepareSceneInstance(target, source, firstValue, placement, parent);
      auto text = baseline(source);

      if (!result || !text) {
        return std::unexpected(!result ? result.error() : text.error());
      }

      SceneLink link{result->root.value, std::string(asset), std::move(*text), {}};

      for (const auto& mapping : result->entities) {
        link.mapping.push_back({mapping.source.value, mapping.target.value});
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
    try {
      auto owner = target.entity(root);
      auto links = target.sceneLinks();
      auto text = baseline(source);
      auto encoded = encodeScene(target);
      auto floor = target.nextEntityValue();

      if (!owner || !links || !text || !encoded || !floor) {
        return std::unexpected(!owner     ? owner.error()
                               : !links   ? links.error()
                               : !text    ? text.error()
                               : !encoded ? encoded.error()
                                          : floor.error());
      }

      auto found = std::ranges::find(*links, root.value, &SceneLink::root);

      if (found == links->end()) {
        return Core::fail(Core::ErrorCode::NotFound, "Entity is not a linked scene root");
      }

      const auto maximum = std::numeric_limits<std::uint64_t>::max();
      const auto minimum = floor->value_or(maximum);

      if (firstValue < minimum || firstValue == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Refresh requires the reserved allocation floor");
      }

      Values oldMapping, mapping;

      for (const auto& pair : found->mapping) {
        oldMapping.emplace(pair.source, pair.target);
      }

      const auto incoming = Codec::parseJson(*text);
      const auto original = Codec::parseJson(found->baseline);
      const auto sourceFloor = [](const Json& value) {
        return value["nextEntity"].is_null() ? std::numeric_limits<std::uint64_t>::max()
                                             : Codec::id(value["nextEntity"]);
      };

      if (sourceFloor(incoming) < sourceFloor(original)) {
        return Core::fail(Core::ErrorCode::InvalidState, "Linked source allocation regressed");
      }

      auto next = firstValue;
      std::vector<std::uint64_t> created;

      for (const auto& entity : incoming["entities"]) {
        const auto id = Codec::id(entity["id"]);
        const auto old = oldMapping.find(id);

        if (old != oldMapping.end()) {
          mapping.emplace(id, old->second);
        } else {
          if (id < sourceFloor(original)) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Linked source reused a retired node value");
          }

          if (next == maximum) {
            return Core::fail(Core::ErrorCode::Unavailable,
                              "Linked scene entity allocation is exhausted");
          }

          mapping.emplace(id, next);
          created.push_back(next++);
        }
      }

      const auto previous = nodes(original, oldMapping, root.value);
      const auto current = nodes(incoming, mapping, root.value);
      auto document = Codec::parseJson(*encoded);
      Nodes local;

      for (const auto& entity : document["entities"]) {
        local.emplace(Codec::id(entity["id"]), entity);
      }

      for (const auto& [id, old] : previous) {
        const auto value = oldMapping.at(id);
        const auto entry = local.find(value);

        if (!current.contains(id)) {
          if (entry != local.end() && !revert && entry->second != old) {
            return Core::fail(Core::ErrorCode::InvalidState,
                              "Source removed an overridden node: " + std::to_string(id));
          }

          local.erase(value);
        }
      }

      for (const auto& [id, value] : current) {
        const auto old = previous.find(id);
        const auto entry = local.find(mapping.at(id));

        if (old == previous.end() || revert) {
          if (entry == local.end() && old != previous.end()) {
            created.push_back(mapping.at(id));
          }

          local[mapping.at(id)] = value;
        } else if (entry != local.end()) {
          entry->second = *merge(std::optional<Json>{old->second},
                                 std::optional<Json>{entry->second}, std::optional<Json>{value});
        }
      }

      document["entities"] = Json::array();

      for (auto& [id, value] : local) {
        document["entities"].push_back(std::move(value));
      }

      found->baseline = std::move(*text);
      found->mapping.clear();

      for (const auto& [sourceValue, targetValue] : mapping) {
        found->mapping.push_back({sourceValue, targetValue});
      }

      document.erase("links");
      document["nextEntity"] = next == maximum ? Json(nullptr) : Json(std::to_string(next));
      auto prepared = decodeScene(document.dump());

      if (!prepared) {
        return Core::fail(prepared.error().code,
                          "Linked scene refresh: " + prepared.error().message);
      }

      auto changed = prepared->setSceneLinks(*links);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      auto bounded = encodeScene(*prepared);

      if (!bounded) {
        return std::unexpected(bounded.error());
      }

      const auto handle = EntityId{prepared->id(), root.value};
      return SceneLinkUpdate{std::move(*prepared), handle, std::move(created)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Refresh linked scene: " + std::string(error.what()));
    }
  }
}  // namespace Dot::World
