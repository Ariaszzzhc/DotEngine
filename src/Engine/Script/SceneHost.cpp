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
#include <vector>

#include "ComponentJson.hpp"

module Dot.Script.SceneHost;

import Dot.Script.Host;
import Dot.Core.Result;
import Dot.Core.Text;
import Dot.World.Scene;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Audio.Source;
import Dot.Script.Source;

namespace Dot::Script {
  namespace {
    using Json = nlohmann::json;
    using Core::ErrorCode;
    namespace Codec = Serialization::Detail;
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
      SetParent,
      Reparent,
      Group,
      Ungroup,
      GetComponent,
      SetComponent,
      Exists,
      PrimaryCamera,
      PrimaryListener
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
                                 std::pair{"hierarchy.setParent", Operation::SetParent},
                                 std::pair{"hierarchy.reparent", Operation::Reparent},
                                 std::pair{"scene.group", Operation::Group},
                                 std::pair{"scene.ungroup", Operation::Ungroup},
                                 std::pair{"component.get", Operation::GetComponent},
                                 std::pair{"component.set", Operation::SetComponent},
                                 std::pair{"entity.exists", Operation::Exists},
                                 std::pair{"scene.primaryCamera", Operation::PrimaryCamera},
                                 std::pair{"scene.primaryListener", Operation::PrimaryListener}};

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

    auto written(Core::Result<void> result) -> Reply;

    auto cameraJson(const World::Camera& value) -> Json {
      return {{"projection",
               value.projection == World::Projection::Perspective ? "perspective" : "orthographic"},
              {"verticalFovDegrees", value.verticalFovDegrees},
              {"orthographicHeight", value.orthographicHeight},
              {"nearPlane", value.nearPlane},
              {"farPlane", value.farPlane},
              {"primary", value.primary}};
    }

    auto cameraValue(const Json& value) -> World::Camera {
      Codec::requireFields(value, {"projection", "verticalFovDegrees", "orthographicHeight",
                                   "nearPlane", "farPlane", "primary"});
      return {Detail::named<World::Projection>(value["projection"],
                                               {{"perspective", World::Projection::Perspective},
                                                {"orthographic", World::Projection::Orthographic}}),
              Codec::number(value["verticalFovDegrees"]),
              Codec::number(value["orthographicHeight"]),
              Codec::number(value["nearPlane"]),
              Codec::number(value["farPlane"]),
              value["primary"].get<bool>()};
    }

    auto meshJson(const World::MeshRenderer& value) -> Json {
      const auto kind = value.kind == World::MeshKind::Cube   ? "cube"
                        : value.kind == World::MeshKind::Quad ? "quad"
                                                              : "asset";
      return {{"kind", kind},
              {"asset", value.asset},
              {"tint", Detail::xyzw(value.tint)},
              {"visible", value.visible}};
    }

    auto meshValue(const Json& value) -> World::MeshRenderer {
      Codec::requireFields(value, {"kind", "asset", "tint", "visible"});
      return {Detail::named<World::MeshKind>(value["kind"], {{"cube", World::MeshKind::Cube},
                                                             {"quad", World::MeshKind::Quad},
                                                             {"asset", World::MeshKind::Asset}}),
              value["asset"].get<std::string>(), Detail::color<Math::Vec4>(value["tint"]),
              value["visible"].get<bool>()};
    }

    auto bodyJson(const World::RigidBody& value) -> Json {
      const auto motion = value.motion == Physics::Motion::Static      ? "static"
                          : value.motion == Physics::Motion::Kinematic ? "kinematic"
                                                                       : "dynamic";
      const auto shape = value.shape == Physics::Shape::Box      ? "box"
                         : value.shape == Physics::Shape::Sphere ? "sphere"
                                                                 : "capsule";
      return {{"motion", motion},
              {"collisionDetection",
               value.collisionDetection == Physics::CollisionDetection::Continuous ? "continuous"
                                                                                   : "discrete"},
              {"shape", shape},
              {"halfExtents", Detail::xyz(value.halfExtents)},
              {"radius", value.radius},
              {"halfHeight", value.halfHeight},
              {"velocity", Detail::xyz(value.velocity)},
              {"mass", value.mass},
              {"friction", value.friction},
              {"restitution", value.restitution},
              {"gravityFactor", value.gravityFactor},
              {"sensor", value.sensor}};
    }

    auto bodyValue(const Json& value) -> World::RigidBody {
      Codec::requireFields(
          value, {"motion", "shape", "halfExtents", "radius", "halfHeight", "velocity", "mass",
                  "friction", "restitution", "gravityFactor", "sensor", "collisionDetection"});
      World::RigidBody result;
      result.motion = Detail::named<Physics::Motion>(value["motion"],
                                                     {{"static", Physics::Motion::Static},
                                                      {"kinematic", Physics::Motion::Kinematic},
                                                      {"dynamic", Physics::Motion::Dynamic}});
      result.shape
          = Detail::named<Physics::Shape>(value["shape"], {{"box", Physics::Shape::Box},
                                                           {"sphere", Physics::Shape::Sphere},
                                                           {"capsule", Physics::Shape::Capsule}});
      result.collisionDetection = Detail::named<Physics::CollisionDetection>(
          value["collisionDetection"], {{"discrete", Physics::CollisionDetection::Discrete},
                                        {"continuous", Physics::CollisionDetection::Continuous}});
      result.halfExtents = Detail::vector<Math::Vec3>(value["halfExtents"]);
      result.radius = Codec::number(value["radius"]);
      result.halfHeight = Codec::number(value["halfHeight"]);
      result.velocity = Detail::vector<Math::Vec3>(value["velocity"]);
      result.mass = Codec::number(value["mass"]);
      result.friction = Codec::number(value["friction"]);
      result.restitution = Codec::number(value["restitution"]);
      result.gravityFactor = Codec::number(value["gravityFactor"]);
      result.sensor = value["sensor"].get<bool>();
      return result;
    }

    auto characterJson(const World::CharacterController& value) -> Json {
      return {{"radius", value.radius},
              {"halfHeight", value.halfHeight},
              {"maxSlopeDegrees", value.maxSlopeDegrees},
              {"stepHeight", value.stepHeight},
              {"stickDistance", value.stickDistance},
              {"mass", value.mass},
              {"maxStrength", value.maxStrength},
              {"jumpSpeed", value.jumpSpeed},
              {"maxContacts", value.maxContacts}};
    }

    auto characterValue(const Json& value) -> World::CharacterController {
      Codec::requireFields(
          value, {"radius", "halfHeight", "maxSlopeDegrees", "stepHeight", "stickDistance", "mass",
                  "maxStrength", "jumpSpeed", "maxContacts"});

      if (!value["maxContacts"].is_number_integer() || value["maxContacts"] < 1
          || value["maxContacts"] > 4096) {
        throw std::invalid_argument("Character maxContacts must be an integer in [1,4096]");
      }

      return {Codec::number(value["radius"]),           Codec::number(value["halfHeight"]),
              Codec::number(value["maxSlopeDegrees"]),  Codec::number(value["stepHeight"]),
              Codec::number(value["stickDistance"]),    Codec::number(value["mass"]),
              Codec::number(value["maxStrength"]),      Codec::number(value["jumpSpeed"]),
              value["maxContacts"].get<std::uint32_t>()};
    }

    auto audioJson(const World::AudioSource& value) -> Json {
      const auto& s = value.spatial;
      const auto attenuation = s.attenuation == Audio::Attenuation::Inverse  ? "inverse"
                               : s.attenuation == Audio::Attenuation::Linear ? "linear"
                                                                             : "exponential";
      return {{"asset", value.asset},
              {"playOnStart", value.playOnStart},
              {"voice",
               {{"volume", value.voice.volume},
                {"pan", value.voice.pan},
                {"pitch", value.voice.pitch},
                {"looping", value.voice.looping}}},
              {"spatial",
               {{"enabled", s.enabled},
                {"attenuation", attenuation},
                {"minDistance", s.minDistance},
                {"maxDistance", s.maxDistance},
                {"rolloff", s.rolloff},
                {"dopplerFactor", s.dopplerFactor},
                {"innerConeDegrees", s.innerConeDegrees},
                {"outerConeDegrees", s.outerConeDegrees},
                {"outerConeGain", s.outerConeGain}}}};
    }

    auto audioValue(const Json& value) -> World::AudioSource {
      Codec::requireFields(value, {"asset", "playOnStart", "voice", "spatial"});
      const auto& voice = value["voice"];
      const auto& s = value["spatial"];
      Codec::requireFields(voice, {"volume", "pan", "pitch", "looping"});
      Codec::requireFields(
          s, {"enabled", "attenuation", "minDistance", "maxDistance", "rolloff", "dopplerFactor",
              "innerConeDegrees", "outerConeDegrees", "outerConeGain"});
      World::AudioSource result;
      result.asset = value["asset"].get<std::string>();
      result.playOnStart = value["playOnStart"].get<bool>();
      result.voice = {Codec::number(voice["volume"]), Codec::number(voice["pan"]),
                      Codec::number(voice["pitch"]), voice["looping"].get<bool>()};
      result.spatial = {s["enabled"].get<bool>(),
                        Detail::named<Audio::Attenuation>(
                            s["attenuation"], {{"inverse", Audio::Attenuation::Inverse},
                                               {"linear", Audio::Attenuation::Linear},
                                               {"exponential", Audio::Attenuation::Exponential}}),
                        Codec::number(s["minDistance"]),
                        Codec::number(s["maxDistance"]),
                        Codec::number(s["rolloff"]),
                        Codec::number(s["dopplerFactor"]),
                        Codec::number(s["innerConeDegrees"]),
                        Codec::number(s["outerConeDegrees"]),
                        Codec::number(s["outerConeGain"])};
      return result;
    }

    auto listenerValue(const Json& value) -> World::AudioListener {
      Codec::requireFields(value, {"primary"});
      return {value["primary"].get<bool>()};
    }

    auto scriptJson(const World::ScriptComponent& value) -> Json {
      return {{"source", value.source},
              {"className", value.className},
              {"properties", Codec::parseJson(value.properties, 32)},
              {"enabled", value.enabled}};
    }

    auto scriptValue(const Json& value) -> World::ScriptComponent {
      Codec::requireFields(value, {"source", "className", "properties", "enabled"});

      if (!value["properties"].is_object()) {
        throw std::invalid_argument("Script properties require an object");
      }

      return {value["source"].get<std::string>(), value["className"].get<std::string>(),
              value["properties"].dump(), value["enabled"].get<bool>()};
    }

    auto componentJson(const World::EntityInfo& entity, std::string_view name)
        -> Core::Result<Json> {
      if (name == "camera") {
        return entity.camera ? cameraJson(*entity.camera) : Json(nullptr);
      }

      if (name == "mesh") {
        return entity.mesh ? meshJson(*entity.mesh) : Json(nullptr);
      }

      if (name == "rigidBody") {
        return entity.rigidBody ? bodyJson(*entity.rigidBody) : Json(nullptr);
      }

      if (name == "character") {
        return entity.character ? characterJson(*entity.character) : Json(nullptr);
      }

      if (name == "audioSource") {
        return entity.audioSource ? audioJson(*entity.audioSource) : Json(nullptr);
      }

      if (name == "audioListener") {
        return entity.audioListener ? Json{{"primary", entity.audioListener->primary}}
                                    : Json(nullptr);
      }

      if (name == "script") {
        return entity.script ? scriptJson(*entity.script) : Json(nullptr);
      }

      return Core::fail(ErrorCode::InvalidArgument, "Unknown component name");
    }

    auto setComponent(World::Scene& scene, World::EntityId id, std::string_view name,
                      const Json& value) -> Reply {
      if (name == "camera") {
        return written(scene.setCamera(
            id, value.is_null() ? std::nullopt : std::optional{cameraValue(value)}));
      }

      if (name == "mesh") {
        return written(scene.setMeshRenderer(
            id, value.is_null() ? std::nullopt : std::optional{meshValue(value)}));
      }

      if (name == "rigidBody") {
        return written(scene.setRigidBody(
            id, value.is_null() ? std::nullopt : std::optional{bodyValue(value)}));
      }

      if (name == "character") {
        return written(scene.setCharacterController(
            id, value.is_null() ? std::nullopt : std::optional{characterValue(value)}));
      }

      if (name == "audioSource") {
        return written(scene.setAudioSource(
            id, value.is_null() ? std::nullopt : std::optional{audioValue(value)}));
      }

      if (name == "audioListener") {
        return written(scene.setAudioListener(
            id, value.is_null() ? std::nullopt : std::optional{listenerValue(value)}));
      }

      if (name == "script") {
        return written(scene.setScript(
            id, value.is_null() ? std::nullopt : std::optional{scriptValue(value)}));
      }

      return Core::fail(ErrorCode::InvalidArgument, "Unknown component name");
    }

    auto entityJson(const World::EntityInfo& entity) -> Json {
      return {{"id", idJson(entity.id)},
              {"name", entity.name},
              {"parent", entity.parent ? idJson(*entity.parent) : Json(nullptr)},
              {"transform", transformJson(entity.transform)},
              {"camera", componentJson(entity, "camera").value()},
              {"mesh", componentJson(entity, "mesh").value()},
              {"rigidBody", componentJson(entity, "rigidBody").value()},
              {"character", componentJson(entity, "character").value()},
              {"audioSource", componentJson(entity, "audioSource").value()},
              {"audioListener", componentJson(entity, "audioListener").value()},
              {"script", componentJson(entity, "script").value()}};
    }

    auto written(Core::Result<void> result) -> Reply {
      if (!result) {
        return std::unexpected(result.error());
      }

      return std::optional<std::string>{};
    }

    auto dispatch(World::Scene& scene, Operation operation, const Json& args) -> Reply {
      if (operation == Operation::PrimaryCamera || operation == Operation::PrimaryListener) {
        if (!args.empty()) {
          return Core::fail(ErrorCode::InvalidArgument,
                            "Primary component lookup takes no arguments");
        }

        auto id = operation == Operation::PrimaryCamera ? scene.primaryCamera()
                                                        : scene.primaryAudioListener();

        if (!id) {
          const auto absent = operation == Operation::PrimaryCamera ? ErrorCode::Unavailable
                                                                    : ErrorCode::NotFound;

          if (id.error().code == absent) {
            return std::optional<std::string>{"null"};
          }

          return std::unexpected(id.error());
        }

        return std::optional{idJson(*id).dump()};
      }

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

      if (operation == Operation::Group || operation == Operation::Ungroup) {
        const auto count = operation == Operation::Group ? 2U : 1U;

        if (args.size() != count || !args[0].is_array() || args[0].empty() || args[0].size() > 10000
            || (operation == Operation::Group && !args[1].is_string())) {
          return Core::fail(ErrorCode::InvalidArgument, "Invalid group operation arguments");
        }

        std::vector<World::EntityId> entities;
        entities.reserve(args[0].size());

        for (const auto& value : args[0]) {
          auto entity = entityId(value);

          if (!entity) {
            return std::unexpected(entity.error());
          }

          entities.push_back(*entity);
        }

        if (operation == Operation::Group) {
          auto grouped = scene.groupEntities(entities, args[1].get<std::string>());
          return grouped ? Reply{std::optional{idJson(*grouped).dump()}}
                         : Reply{std::unexpected(grouped.error())};
        }

        auto ungrouped = scene.ungroupEntities(entities);

        if (!ungrouped) {
          return std::unexpected(ungrouped.error());
        }

        auto result = Json::array();

        for (const auto entity : *ungrouped) {
          result.push_back(idJson(entity));
        }

        return std::optional{result.dump()};
      }

      const bool two = operation == Operation::RenameEntity || operation == Operation::SetTransform
                       || operation == Operation::SetParent;

      const auto count = (operation == Operation::SetComponent || operation == Operation::Reparent)
                             ? 3U
                         : operation == Operation::GetComponent ? 2U
                                                                : (two ? 2U : 1U);

      if (args.size() != count) {
        return Core::fail(ErrorCode::InvalidArgument,
                          "Entity operation has incorrect argument count");
      }

      auto entity = entityId(args[0]);

      if (!entity) {
        return std::unexpected(entity.error());
      }

      if (operation == Operation::Exists) {
        auto value = scene.entity(*entity);

        if (!value && value.error().code != ErrorCode::NotFound) {
          return std::unexpected(value.error());
        }

        return std::optional<std::string>{value ? "true" : "false"};
      }

      if (operation == Operation::GetComponent || operation == Operation::SetComponent) {
        if (!args[1].is_string()) {
          return Core::fail(ErrorCode::InvalidArgument, "Component name must be a string");
        }

        const auto& name = args[1].get_ref<const std::string&>();

        if (operation == Operation::SetComponent) {
          return setComponent(scene, *entity, name, args[2]);
        }

        auto info = scene.entity(*entity);

        if (!info) {
          return std::unexpected(info.error());
        }

        auto value = componentJson(*info, name);

        if (!value) {
          return std::unexpected(value.error());
        }

        return std::optional{value->dump()};
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

      if (operation == Operation::SetParent || operation == Operation::Reparent) {
        std::optional<World::EntityId> parent;

        if (!args[1].is_null()) {
          auto value = entityId(args[1]);

          if (!value) {
            return std::unexpected(value.error());
          }

          parent = *value;
        }

        auto space = World::ParentSpace::Local;

        if (operation == Operation::Reparent) {
          if (!args[2].is_string() || (args[2] != "local" && args[2] != "world")) {
            return Core::fail(ErrorCode::InvalidArgument, "Reparent space must be local or world");
          }

          space = args[2] == "world" ? World::ParentSpace::World : World::ParentSpace::Local;
        }

        return written(scene.setParent(*entity, parent, space));
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
          {method,
           [scene, identity, operation](std::string_view arguments) -> Reply {
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
               const auto args = Codec::parseJson(arguments);

               if (!args.is_array()) {
                 return Core::fail(ErrorCode::InvalidArgument,
                                   "Scene host arguments must be an array");
               }

               return dispatch(*scene, operation, args);
             } catch (const Json::exception& error) {
               return Core::fail(ErrorCode::InvalidArgument,
                                 "Scene host JSON: " + std::string(error.what()));
             } catch (const std::invalid_argument& error) {
               return Core::fail(ErrorCode::InvalidArgument,
                                 "Scene component JSON: " + std::string(error.what()));
             } catch (const std::exception& error) {
               return Core::fail(ErrorCode::External,
                                 "Scene host operation: " + std::string(error.what()));
             }
           },
           operation == Operation::Info || operation == Operation::List
                   || operation == Operation::Get || operation == Operation::GetTransform
                   || operation == Operation::WorldTransform || operation == Operation::GetComponent
                   || operation == Operation::Exists || operation == Operation::PrimaryCamera
                   || operation == Operation::PrimaryListener
               ? HostAccess::ReadOnly
               : HostAccess::ReadWrite});
    }

    return bindings;
  }
}  // namespace Dot::Script
