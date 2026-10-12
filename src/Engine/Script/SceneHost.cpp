module;

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <expected>
#include <initializer_list>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module Dot.Script.SceneHost;

import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Core.Text;
import Dot.World.Scene;
import Dot.Math;

namespace Dot::Script {
  namespace {
    using Json = nlohmann::json;
    using Core::ErrorCode;
    using Reply = Core::Result<std::optional<std::string>>;

    enum class Operation {
      Info,
      RenameScene,
      List,
      Create,
      Get,
      Destroy,
      RenameEntity,
      GetTransform,
      SetTransform,
      WorldTransform,
      SetParent
    };

    constexpr std::array methods{std::pair{"scene.info", Operation::Info},
                                 std::pair{"scene.rename", Operation::RenameScene},
                                 std::pair{"entity.list", Operation::List},
                                 std::pair{"entity.create", Operation::Create},
                                 std::pair{"entity.get", Operation::Get},
                                 std::pair{"entity.destroy", Operation::Destroy},
                                 std::pair{"entity.rename", Operation::RenameEntity},
                                 std::pair{"transform.get", Operation::GetTransform},
                                 std::pair{"transform.set", Operation::SetTransform},
                                 std::pair{"transform.world", Operation::WorldTransform},
                                 std::pair{"hierarchy.setParent", Operation::SetParent}};

    auto shape(const Json& value, std::initializer_list<std::string_view> keys) -> bool {
      if (!value.is_object() || value.size() != keys.size()) {
        return false;
      }

      for (const auto key : keys) {
        if (!value.contains(key)) {
          return false;
        }
      }

      return true;
    }

    auto decimal(const Json& value) -> Core::Result<std::uint64_t> {
      if (!value.is_string()) {
        return Core::fail(ErrorCode::InvalidArgument, "Entity IDs require decimal strings");
      }

      const auto& text = value.get_ref<const std::string&>();
      std::uint64_t result = 0;
      const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);

      if (text.empty() || text.size() > 20 || text.front() == '0' || error != std::errc{}
          || end != text.data() + text.size() || result == 0
          || result == std::numeric_limits<std::uint64_t>::max()) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Entity ID is not a canonical positive uint64 string");
      }

      return result;
    }

    auto entityId(const Json& value) -> Core::Result<World::EntityId> {
      if (!shape(value, {"scene", "value"})) {
        return Core::fail(ErrorCode::InvalidArgument, "Entity ID requires exactly scene and value");
      }

      auto scene = decimal(value.at("scene"));
      auto entity = decimal(value.at("value"));

      if (!scene) {
        return std::unexpected(scene.error());
      }

      if (!entity) {
        return std::unexpected(entity.error());
      }

      return World::EntityId{*scene, *entity};
    }

    auto idJson(World::EntityId entity) -> Json {
      return {{"scene", std::to_string(entity.scene)}, {"value", std::to_string(entity.value)}};
    }

    auto number(const Json& value) -> Core::Result<float> {
      if (!value.is_number()) {
        return Core::fail(ErrorCode::InvalidArgument, "Transform fields require finite numbers");
      }

      const auto result = value.get<double>();
      const auto limit = static_cast<double>(std::numeric_limits<float>::max());

      if (!std::isfinite(result) || result < -limit || result > limit) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Transform number exceeds finite float range");
      }

      return static_cast<float>(result);
    }

    auto vector(const Json& value) -> Core::Result<Math::Vec3> {
      if (!shape(value, {"x", "y", "z"})) {
        return Core::fail(ErrorCode::InvalidArgument, "Vector requires exactly x, y and z");
      }

      auto x = number(value.at("x"));
      auto y = number(value.at("y"));
      auto z = number(value.at("z"));

      if (!x || !y || !z) {
        return Core::fail(ErrorCode::InvalidArgument, "Invalid finite vector fields");
      }

      return Math::Vec3{*x, *y, *z};
    }

    auto transform(const Json& value) -> Core::Result<World::Transform> {
      if (!shape(value, {"position", "rotation", "scale"})
          || !shape(value.at("rotation"), {"x", "y", "z", "w"})) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Transform requires complete position/rotation/scale");
      }

      auto position = vector(value.at("position"));
      auto scale = vector(value.at("scale"));
      const auto& rotation = value.at("rotation");
      auto x = number(rotation.at("x"));
      auto y = number(rotation.at("y"));
      auto z = number(rotation.at("z"));
      auto w = number(rotation.at("w"));

      if (!position || !scale || !x || !y || !z || !w) {
        return Core::fail(ErrorCode::InvalidArgument, "Invalid transform fields");
      }

      return World::Transform{*position, {*x, *y, *z, *w}, *scale};
    }

    auto vectorJson(Math::Vec3 value) -> Json {
      return {{"x", value.x}, {"y", value.y}, {"z", value.z}};
    }

    auto transformJson(const World::Transform& value) -> Json {
      const auto q = value.rotation;
      return {{"position", vectorJson(value.position)},
              {"scale", vectorJson(value.scale)},
              {"rotation", {{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}}}};
    }

    auto entityJson(const World::EntityInfo& entity) -> Json {
      return {{"id", idJson(entity.id)},
              {"name", entity.name},
              {"parent", entity.parent ? idJson(*entity.parent) : Json(nullptr)},
              {"transform", transformJson(entity.transform)}};
    }

    auto written(Core::Result<void> result) -> Reply {
      if (!result) {
        return std::unexpected(result.error());
      }

      return std::optional<std::string>{};
    }

    auto dispatch(World::Scene& scene, Operation operation, const Json& args) -> Reply {
      if (operation == Operation::Info || operation == Operation::List) {
        if (!args.empty()) {
          return Core::fail(ErrorCode::InvalidArgument, "Scene info/entity list take no arguments");
        }

        if (operation == Operation::Info) {
          auto name = scene.name();
          auto count = scene.entityCount();

          if (!name || !count) {
            return Core::fail(ErrorCode::InvalidState, "Scene info is unavailable");
          }

          return std::optional{Json{
              {"id", std::to_string(scene.id())},
              {"name", *name},
              {"entities", *count}}.dump()};
        }

        auto entities = scene.entities();

        if (!entities) {
          return std::unexpected(entities.error());
        }

        auto result = Json::array();

        for (const auto& entity : *entities) {
          result.push_back(idJson(entity.id));
        }

        return std::optional{result.dump()};
      }

      if (operation == Operation::RenameScene || operation == Operation::Create) {
        if (args.size() != 1 || !args[0].is_string()) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Scene rename/entity create require one name");
        }

        const auto& name = args[0].get_ref<const std::string&>();

        if (operation == Operation::RenameScene) {
          return written(scene.rename(name));
        }

        auto result = scene.createEntity(name);

        if (!result) {
          return std::unexpected(result.error());
        }

        return std::optional{idJson(*result).dump()};
      }

      const bool two = operation == Operation::RenameEntity || operation == Operation::SetTransform
                       || operation == Operation::SetParent;

      if (args.size() != (two ? 2U : 1U)) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Entity operation has incorrect argument count");
      }

      auto entity = entityId(args[0]);

      if (!entity) {
        return std::unexpected(entity.error());
      }

      if (operation == Operation::Destroy) {
        return written(scene.destroyEntity(*entity));
      }

      if (operation == Operation::RenameEntity) {
        if (!args[1].is_string()) {
          return Core::fail(ErrorCode::InvalidArgument, "Entity rename requires a name string");
        }

        return written(scene.renameEntity(*entity, args[1].get_ref<const std::string&>()));
      }

      if (operation == Operation::SetTransform) {
        auto value = transform(args[1]);

        if (!value) {
          return std::unexpected(value.error());
        }

        return written(scene.setTransform(*entity, *value));
      }

      if (operation == Operation::SetParent) {
        std::optional<World::EntityId> parent;

        if (!args[1].is_null()) {
          auto value = entityId(args[1]);

          if (!value) {
            return std::unexpected(value.error());
          }

          parent = *value;
        }

        return written(scene.setParent(*entity, parent));
      }

      if (operation == Operation::WorldTransform) {
        auto value = scene.worldTransform(*entity);

        if (!value) {
          return std::unexpected(value.error());
        }

        return std::optional{Json(value->data).dump()};
      }

      auto info = scene.entity(*entity);

      if (!info) {
        return std::unexpected(info.error());
      }

      return std::optional{(operation == Operation::GetTransform ? transformJson(info->transform)
                                                                 : entityJson(*info))
                               .dump()};
    }
  }  // namespace

  auto sceneBindings(std::shared_ptr<World::Scene> scene) -> Core::Result<HostBindings> {
    if (!scene || scene->id() == 0) {
      return Core::fail(ErrorCode::InvalidArgument, "Scene bindings require a live owned scene");
    }

    auto name = scene->name();

    if (!name) {
      return std::unexpected(name.error());
    }

    const auto identity = scene->id();
    HostBindings bindings;
    bindings.reserve(methods.size());

    for (const auto& [method, operation] : methods) {
      bindings.push_back(
          {method, [scene, identity, operation](std::string_view arguments) -> Reply {
             if (scene->id() != identity) {
               return Core::fail(ErrorCode::InvalidState, "Bound scene was moved or replaced");
             }

             auto state = scene->name();

             if (!state) {
               return std::unexpected(state.error());
             }

             if (arguments.empty() || arguments.size() > 16 * 1024 * 1024
                 || arguments.find('\0') != std::string_view::npos
                 || !Core::isValidUtf8(arguments)) {
               return Core::fail(ErrorCode::InvalidArgument, "Invalid scene host argument bytes");
             }

             try {
               const auto args = Json::parse(arguments);

               if (!args.is_array()) {
                 return Core::fail(ErrorCode::InvalidArgument,
                                   "Scene host arguments must be an array");
               }

               return dispatch(*scene, operation, args);
             } catch (const Json::exception& error) {
               return Core::fail(ErrorCode::InvalidArgument,
                                 "Scene host JSON: " + std::string(error.what()));
             } catch (const std::exception& error) {
               return Core::fail(ErrorCode::External,
                                 "Scene host operation: " + std::string(error.what()));
             }
           }});
    }

    return bindings;
  }
}  // namespace Dot::Script
