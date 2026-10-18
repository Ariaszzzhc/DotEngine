module;

#include <algorithm>
#include <expected>
#include <format>
#include <optional>
#include <unordered_map>
#include <utility>

#include "../Serialization/Json.hpp"

module Dot.World.SceneFiles;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.World.Scene;
import Dot.Graphics.Shadows;
import Dot.Math;
import Dot.Physics.Body;
import Dot.Physics.Character;
import Dot.Audio.Source;
import Dot.Script.Source;

namespace Dot::World {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t maxFileBytes = 64 * 1024 * 1024;

    auto vector3(const Json& value) -> Math::Vec3 {
      const auto result = Codec::numbers<3>(value);
      return {result[0], result[1], result[2]};
    }

    auto vector4(const Json& value) -> Math::Vec4 {
      const auto result = Codec::numbers<4>(value);
      return {result[0], result[1], result[2], result[3]};
    }

    auto transform(const Json& value) -> Transform {
      Codec::requireFields(value, {"position", "rotation", "scale"});
      const auto rotation = Codec::numbers<4>(value["rotation"]);
      return {vector3(value["position"]),
              {rotation[0], rotation[1], rotation[2], rotation[3]},
              vector3(value["scale"])};
    }

    auto camera(const Json& value) -> Camera {
      Codec::requireFields(value, {"projection", "verticalFovDegrees", "orthographicHeight",
                                   "nearPlane", "farPlane", "primary"});
      Camera result;
      const auto projection = value["projection"].get<std::string>();

      if (projection == "perspective") {
        result.projection = Projection::Perspective;
      } else if (projection == "orthographic") {
        result.projection = Projection::Orthographic;
      } else {
        throw std::invalid_argument("Unknown camera projection");
      }

      result.verticalFovDegrees = Codec::number(value["verticalFovDegrees"]);
      result.orthographicHeight = Codec::number(value["orthographicHeight"]);
      result.nearPlane = Codec::number(value["nearPlane"]);
      result.farPlane = Codec::number(value["farPlane"]);
      result.primary = value["primary"].get<bool>();
      return result;
    }

    auto light(const Json& value) -> Light {
      Codec::requireFields(value,
                           {"kind", "color", "intensity", "range", "innerConeDegrees",
                            "outerConeDegrees", "enabled"},
                           {"shadows"});
      Light result;
      const auto kind = value["kind"].get<std::string>();

      if (kind == "directional") {
        result.kind = LightKind::Directional;
      } else if (kind == "point") {
        result.kind = LightKind::Point;
      } else if (kind == "spot") {
        result.kind = LightKind::Spot;
      } else {
        throw std::invalid_argument("Unknown scene light kind");
      }

      result.color = vector3(value["color"]);
      result.intensity = Codec::number(value["intensity"]);
      result.range = Codec::number(value["range"]);
      result.innerConeDegrees = Codec::number(value["innerConeDegrees"]);
      result.outerConeDegrees = Codec::number(value["outerConeDegrees"]);
      result.enabled = value["enabled"].get<bool>();

      if (value.contains("shadows") && !value["shadows"].is_null()) {
        const auto& settings = value["shadows"];
        Codec::requireFields(settings, {"resolution", "cascades", "distance", "splitLambda",
                                        "depthBias", "slopeBias", "normalBias", "filterRadius"});
        const auto integer = [&](const char* key) -> std::uint32_t {
          if (!settings[key].is_number_integer() || settings[key] < 0 || settings[key] > 4096) {
            throw std::invalid_argument("Shadow work values require bounded integers");
          }

          return settings[key].get<std::uint32_t>();
        };
        result.shadows = Graphics::ShadowSettings{integer("resolution"),
                                                  integer("cascades"),
                                                  Codec::number(settings["distance"]),
                                                  Codec::number(settings["splitLambda"]),
                                                  Codec::number(settings["depthBias"]),
                                                  Codec::number(settings["slopeBias"]),
                                                  Codec::number(settings["normalBias"]),
                                                  Codec::number(settings["filterRadius"])};
      }
      return result;
    }

    auto mesh(const Json& value) -> MeshRenderer {
      Codec::requireFields(value, {"kind", "asset", "tint", "visible"},
                           {"castsShadow", "receivesShadow"});
      MeshRenderer result;
      const auto kind = value["kind"].get<std::string>();

      if (kind == "cube") {
        result.kind = MeshKind::Cube;
      } else if (kind == "quad") {
        result.kind = MeshKind::Quad;
      } else if (kind == "asset") {
        result.kind = MeshKind::Asset;
      } else {
        throw std::invalid_argument("Unknown mesh kind");
      }

      result.asset = value["asset"].get<std::string>();
      result.tint = vector4(value["tint"]);
      result.visible = value["visible"].get<bool>();
      result.castsShadow = value.value("castsShadow", true);
      result.receivesShadow = value.value("receivesShadow", true);
      return result;
    }

    auto rigidBody(const Json& value) -> RigidBody {
      Codec::requireFields(
          value,
          {"motion", "shape", "halfExtents", "radius", "halfHeight", "velocity", "mass", "friction",
           "restitution", "gravityFactor", "sensor", "collisionDetection"},
          value.at("shape") == "mesh" ? std::initializer_list<std::string_view>{"meshAsset"}
                                      : std::initializer_list<std::string_view>{});
      RigidBody result;
      const auto motion = value["motion"].get<std::string>();
      const auto shape = value["shape"].get<std::string>();

      if (motion == "static") {
        result.motion = Physics::Motion::Static;
      } else if (motion == "kinematic") {
        result.motion = Physics::Motion::Kinematic;
      } else if (motion == "dynamic") {
        result.motion = Physics::Motion::Dynamic;
      } else {
        throw std::invalid_argument("Unknown rigid-body motion");
      }

      if (shape == "box") {
        result.shape = Physics::Shape::Box;
      } else if (shape == "sphere") {
        result.shape = Physics::Shape::Sphere;
      } else if (shape == "capsule") {
        result.shape = Physics::Shape::Capsule;
      } else if (shape == "mesh") {
        result.shape = Physics::Shape::Mesh;
      } else {
        throw std::invalid_argument("Unknown rigid-body shape");
      }

      const auto detection = value["collisionDetection"].get<std::string>();

      if (detection == "discrete") {
        result.collisionDetection = Physics::CollisionDetection::Discrete;
      } else if (detection == "continuous") {
        result.collisionDetection = Physics::CollisionDetection::Continuous;
      } else {
        throw std::invalid_argument("Unknown collision detection mode");
      }

      result.halfExtents = vector3(value["halfExtents"]);
      result.radius = Codec::number(value["radius"]);
      result.halfHeight = Codec::number(value["halfHeight"]);
      result.velocity = vector3(value["velocity"]);
      result.mass = Codec::number(value["mass"]);
      result.friction = Codec::number(value["friction"]);
      result.restitution = Codec::number(value["restitution"]);
      result.gravityFactor = Codec::number(value["gravityFactor"]);
      result.sensor = value["sensor"].get<bool>();

      if (result.shape == Physics::Shape::Mesh) {
        result.meshAsset = value.at("meshAsset").get<std::string>();
      }

      return result;
    }

    auto characterController(const Json& value) -> CharacterController {
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

    auto audioSource(const Json& value) -> AudioSource {
      Codec::requireFields(value, {"asset", "voice", "spatial", "playOnStart"});
      AudioSource result;
      result.asset = value["asset"].get<std::string>();
      result.playOnStart = value["playOnStart"].get<bool>();
      const auto& voice = value["voice"];
      Codec::requireFields(voice, {"volume", "pan", "pitch", "looping"});
      result.voice.volume = Codec::number(voice["volume"]);
      result.voice.pan = Codec::number(voice["pan"]);
      result.voice.pitch = Codec::number(voice["pitch"]);
      result.voice.looping = voice["looping"].get<bool>();
      const auto& spatial = value["spatial"];
      Codec::requireFields(
          spatial, {"enabled", "attenuation", "minDistance", "maxDistance", "rolloff",
                    "dopplerFactor", "innerConeDegrees", "outerConeDegrees", "outerConeGain"});
      result.spatial.enabled = spatial["enabled"].get<bool>();
      const auto model = spatial["attenuation"].get<std::string>();

      if (model == "inverse") {
        result.spatial.attenuation = Audio::Attenuation::Inverse;
      } else if (model == "linear") {
        result.spatial.attenuation = Audio::Attenuation::Linear;
      } else if (model == "exponential") {
        result.spatial.attenuation = Audio::Attenuation::Exponential;
      } else {
        throw std::invalid_argument("Unknown audio attenuation model");
      }

      result.spatial.minDistance = Codec::number(spatial["minDistance"]);
      result.spatial.maxDistance = Codec::number(spatial["maxDistance"]);
      result.spatial.rolloff = Codec::number(spatial["rolloff"]);
      result.spatial.dopplerFactor = Codec::number(spatial["dopplerFactor"]);
      result.spatial.innerConeDegrees = Codec::number(spatial["innerConeDegrees"]);
      result.spatial.outerConeDegrees = Codec::number(spatial["outerConeDegrees"]);
      result.spatial.outerConeGain = Codec::number(spatial["outerConeGain"]);
      return result;
    }

    auto audioListener(const Json& value) -> AudioListener {
      Codec::requireFields(value, {"primary"});
      return {value["primary"].get<bool>()};
    }

    auto script(const Json& value) -> ScriptComponent {
      Codec::requireFields(value, {"source", "className", "properties", "enabled"});

      if (!value["properties"].is_object()) {
        throw std::invalid_argument("Script properties must be an object");
      }

      return {value["source"].get<std::string>(), value["className"].get<std::string>(),
              value["properties"].dump(), value["enabled"].get<bool>()};
    }

    auto validateExtension(const std::filesystem::path& path) -> Core::Result<void> {
      try {
        auto extension = path.extension().generic_u8string();

        for (auto& character : extension) {
          if (character >= u8'A' && character <= u8'Z') {
            character = static_cast<char8_t>(character + (u8'a' - u8'A'));
          }
        }

        if (extension != u8".dotscene") {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Scene extension must be .dotscene");
        }
      } catch (const std::filesystem::filesystem_error& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          std::format("Decode scene path: {}", error.what()));
      }

      return {};
    }

    struct Record {
      std::uint64_t id;
      std::string name;
      std::optional<std::uint64_t> parent;
      Transform transform;
      std::optional<Camera> camera;
      std::optional<MeshRenderer> mesh;
      std::optional<RigidBody> rigidBody;
      std::optional<AudioSource> audioSource;
      std::optional<AudioListener> audioListener;
      std::optional<ScriptComponent> script;
      std::optional<MaterialBinding> material;
      std::optional<Light> light;
      std::optional<AnimationPlayer> animation;
      std::optional<SkinBinding> skin;
      std::optional<CharacterController> character;
    };

    auto parsedScene(const Json& root) -> Core::Result<Scene> {
      Codec::requireFields(root, {"version", "name", "nextEntity", "entities"},
                           {"environment", "occlusion", "links", "ui", "navigation"});

      if (!root["version"].is_number_integer() || root["version"] != 1
          || !root["entities"].is_array() || root["entities"].size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid scene version or entity array");
      }

      std::vector<Record> records;
      records.reserve(root["entities"].size());
      std::unordered_set<std::uint64_t> ids;

      for (const auto& value : root["entities"]) {
        Codec::requireFields(value, {"id", "name", "parent", "transform"},
                             {"camera", "mesh", "rigidBody", "audioSource", "audioListener",
                              "script", "material", "light", "animation", "skin", "character"});
        Record record;
        record.id = Codec::id(value["id"]);
        record.name = value["name"].get<std::string>();
        record.transform = transform(value["transform"]);

        if (!ids.insert(record.id).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Duplicate scene entity ID");
        }

        if (!value["parent"].is_null()) {
          record.parent = Codec::id(value["parent"]);
        }

        if (value.contains("camera") && !value["camera"].is_null()) {
          record.camera = camera(value["camera"]);
        }

        if (value.contains("mesh") && !value["mesh"].is_null()) {
          record.mesh = mesh(value["mesh"]);
        }

        if (value.contains("rigidBody") && !value["rigidBody"].is_null()) {
          record.rigidBody = rigidBody(value["rigidBody"]);
        }

        if (value.contains("character") && !value["character"].is_null()) {
          record.character = characterController(value["character"]);
        }

        if (value.contains("audioSource") && !value["audioSource"].is_null()) {
          record.audioSource = audioSource(value["audioSource"]);
        }

        if (value.contains("audioListener") && !value["audioListener"].is_null()) {
          record.audioListener = audioListener(value["audioListener"]);
        }

        if (value.contains("script") && !value["script"].is_null()) {
          record.script = script(value["script"]);
        }

        if (value.contains("material") && !value["material"].is_null()) {
          Codec::requireFields(value["material"], {"asset"});
          record.material = MaterialBinding{value["material"]["asset"].get<std::string>()};
        }

        if (value.contains("light") && !value["light"].is_null()) {
          record.light = light(value["light"]);
        }

        if (value.contains("animation") && !value["animation"].is_null()) {
          auto animation = Animation::decodePlayerProperties(value["animation"].dump());

          if (!animation) {
            return std::unexpected(animation.error());
          }

          record.animation = std::move(*animation);
        }

        if (value.contains("skin") && !value["skin"].is_null()) {
          auto skin = Animation::decodeSkinProperties(value["skin"].dump());

          if (!skin) {
            return std::unexpected(skin.error());
          }

          record.skin = std::move(*skin);
        }

        records.push_back(std::move(record));
      }

      for (const auto& record : records) {
        if (record.parent && !ids.contains(*record.parent)) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene parent refers to a missing entity");
        }
      }

      std::ranges::sort(records, {}, &Record::id);
      auto scene = Scene::create(root["name"].get<std::string>());

      if (!scene) {
        return std::unexpected(scene.error());
      }

      if (root.contains("environment") && !root["environment"].is_null()) {
        const auto& value = root["environment"];
        Codec::requireFields(value,
                             {"asset", "intensity", "rotationDegrees", "background", "radianceSize",
                              "diffuseSize", "specularSize", "brdfSize", "samples"});
        const auto dimension = [&](const char* key) -> std::uint32_t {
          if (!value[key].is_number_integer() || value[key] < 0 || value[key] > 1024) {
            throw std::invalid_argument("Environment processing values require bounded integers");
          }

          return value[key].get<std::uint32_t>();
        };
        Environment environment{
            value["asset"].get<std::string>(),
            Codec::number(value["intensity"]),
            Codec::number(value["rotationDegrees"]),
            value["background"].get<bool>(),
            {dimension("radianceSize"), dimension("diffuseSize"), dimension("specularSize"),
             dimension("brdfSize"), dimension("samples")}};
        auto changed = scene->setEnvironment(std::move(environment));

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      if (root.contains("occlusion") && !root["occlusion"].is_null()) {
        const auto& value = root["occlusion"];
        Codec::requireFields(value,
                             {"radius", "bias", "strength", "directions", "steps", "filterRadius"});
        const auto integer = [&](const char* key) -> std::uint32_t {
          if (!value[key].is_number_integer() || value[key] < 0 || value[key] > 16) {
            throw std::invalid_argument("Occlusion work values require bounded integers");
          }

          return value[key].get<std::uint32_t>();
        };
        auto changed = scene->setOcclusion(Graphics::OcclusionSettings{
            Codec::number(value["radius"]), Codec::number(value["bias"]),
            Codec::number(value["strength"]), integer("directions"), integer("steps"),
            integer("filterRadius")});

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      if (root.contains("ui") && !root["ui"].is_null()) {
        auto changed = scene->setUi(root["ui"].get<std::string>());

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      if (root.contains("navigation") && !root["navigation"].is_null()) {
        auto changed = scene->setNavigation(root["navigation"].get<std::string>());

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      std::unordered_map<std::uint64_t, EntityId> handles;
      std::vector<TransformUpdate> transforms;
      std::vector<ParentUpdate> parents;
      transforms.reserve(records.size());
      parents.reserve(records.size());

      for (const auto& record : records) {
        auto entity = scene->createEntityWithId(record.name, record.id);

        if (!entity) {
          return std::unexpected(entity.error());
        }

        handles.emplace(record.id, *entity);
        transforms.push_back({*entity, record.transform});
      }

      auto reserved = scene->reserveEntityValuesBefore(
          root["nextEntity"].is_null() ? std::optional<std::uint64_t>{}
                                       : std::optional{Codec::id(root["nextEntity"])});

      if (!reserved) {
        return std::unexpected(reserved.error());
      }

      auto changed = scene->setTransforms(transforms);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      for (const auto& record : records) {
        parents.push_back({handles.at(record.id), record.parent
                                                      ? std::optional{handles.at(*record.parent)}
                                                      : std::nullopt});
      }

      changed = scene->setParents(parents);

      if (!changed) {
        return std::unexpected(changed.error());
      }

      for (const auto& record : records) {
        const auto entity = handles.at(record.id);
        changed = scene->setCamera(entity, record.camera);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setMeshRenderer(entity, record.mesh);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setMaterial(entity, record.material);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setLight(entity, record.light);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setRigidBody(entity, record.rigidBody);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setCharacterController(entity, record.character);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setAudioSource(entity, record.audioSource);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setAudioListener(entity, record.audioListener);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setScript(entity, record.script);

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      for (const auto& record : records) {
        changed = scene->setSkin(handles.at(record.id), record.skin);

        if (!changed) {
          return std::unexpected(changed.error());
        }

        changed = scene->setAnimationPlayer(handles.at(record.id), record.animation);

        if (!changed) {
          return std::unexpected(changed.error());
        }
      }

      if (root.contains("links")) {
        if (!root["links"].is_array() || root["links"].size() > 10000) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Scene links must be a bounded array");
        }

        std::vector<SceneLink> links;

        for (const auto& value : root["links"]) {
          Codec::requireFields(value, {"root", "asset", "baseline", "mapping", "namespaces"});
          SceneLink link{Codec::id(value["root"]),
                         value["asset"].get<std::string>(),
                         value["baseline"].dump(),
                         {},
                         {}};

          const auto keyFromJson = [](const Json& key, bool prefix) -> SceneSourceKey {
            if (!key.is_array() || (!prefix && key.empty()) || key.size() > (prefix ? 31U : 32U)) {
              throw std::invalid_argument("Invalid source key/prefix array");
            }

            SceneSourceKey result;

            for (const auto& component : key) {
              result.push_back(Codec::id(component));
            }

            return result;
          };

          if (!value["mapping"].is_array() || value["mapping"].size() > 10000) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid scene link mapping array");
          }

          for (const auto& mapping : value["mapping"]) {
            Codec::requireFields(mapping, {"source", "target", "key"});
            link.mapping.push_back({Codec::id(mapping["source"]), Codec::id(mapping["target"]),
                                    keyFromJson(mapping["key"], false)});
          }

          if (!value["namespaces"].is_array() || value["namespaces"].empty()
              || value["namespaces"].size() > 10000) {
            return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid source namespace array");
          }

          for (const auto& space : value["namespaces"]) {
            Codec::requireFields(space, {"prefix", "asset", "nextEntity", "entities"});

            if (!space["entities"].is_array() || space["entities"].size() > 100000) {
              return Core::fail(Core::ErrorCode::InvalidArgument,
                                "Invalid source namespace live-value array");
            }

            std::vector<std::uint64_t> live;

            for (const auto& liveValue : space["entities"]) {
              live.push_back(Codec::id(liveValue));
            }

            link.namespaces.push_back(
                {keyFromJson(space["prefix"], true), space["asset"].get<std::string>(),
                 space["nextEntity"].is_null() ? std::optional<std::uint64_t>{}
                                               : std::optional{Codec::id(space["nextEntity"])},
                 std::move(live)});
          }

          links.push_back(std::move(link));
        }

        auto linked = scene->setSceneLinks(links);

        if (!linked) {
          return std::unexpected(linked.error());
        }
      }

      return std::move(*scene);
    }

    auto sourceJson(const AudioSource& value) -> Json {
      const auto& voice = value.voice;
      const auto& spatial = value.spatial;
      const auto model = spatial.attenuation == Audio::Attenuation::Linear        ? "linear"
                         : spatial.attenuation == Audio::Attenuation::Exponential ? "exponential"
                                                                                  : "inverse";
      return {{"asset", value.asset},
              {"playOnStart", value.playOnStart},
              {"voice",
               {{"volume", voice.volume},
                {"pan", voice.pan},
                {"pitch", voice.pitch},
                {"looping", voice.looping}}},
              {"spatial",
               {{"enabled", spatial.enabled},
                {"attenuation", model},
                {"minDistance", spatial.minDistance},
                {"maxDistance", spatial.maxDistance},
                {"rolloff", spatial.rolloff},
                {"dopplerFactor", spatial.dopplerFactor},
                {"innerConeDegrees", spatial.innerConeDegrees},
                {"outerConeDegrees", spatial.outerConeDegrees},
                {"outerConeGain", spatial.outerConeGain}}}};
    }

    auto cameraJson(const Camera& value) -> Json {
      return {{"projection",
               value.projection == Projection::Perspective ? "perspective" : "orthographic"},
              {"verticalFovDegrees", value.verticalFovDegrees},
              {"orthographicHeight", value.orthographicHeight},
              {"nearPlane", value.nearPlane},
              {"farPlane", value.farPlane},
              {"primary", value.primary}};
    }

    auto lightJson(const Light& value) -> Json {
      const auto kind = value.kind == LightKind::Directional ? "directional"
                        : value.kind == LightKind::Point     ? "point"
                                                             : "spot";
      Json shadows = nullptr;

      if (value.shadows) {
        const auto& settings = *value.shadows;
        shadows = {{"resolution", settings.resolution}, {"cascades", settings.cascades},
                   {"distance", settings.distance},     {"splitLambda", settings.splitLambda},
                   {"depthBias", settings.depthBias},   {"slopeBias", settings.slopeBias},
                   {"normalBias", settings.normalBias}, {"filterRadius", settings.filterRadius}};
      }

      return {{"kind", kind},
              {"color", {value.color.x, value.color.y, value.color.z}},
              {"intensity", value.intensity},
              {"range", value.range},
              {"innerConeDegrees", value.innerConeDegrees},
              {"outerConeDegrees", value.outerConeDegrees},
              {"enabled", value.enabled},
              {"shadows", std::move(shadows)}};
    }

    auto meshJson(const MeshRenderer& value) -> Json {
      const auto kind = value.kind == MeshKind::Cube   ? "cube"
                        : value.kind == MeshKind::Quad ? "quad"
                                                       : "asset";
      return {{"kind", kind},
              {"asset", value.asset},
              {"visible", value.visible},
              {"castsShadow", value.castsShadow},
              {"receivesShadow", value.receivesShadow},
              {"tint", {value.tint.x, value.tint.y, value.tint.z, value.tint.w}}};
    }

    auto bodyJson(const RigidBody& value) -> Json {
      const auto motion = value.motion == Physics::Motion::Static      ? "static"
                          : value.motion == Physics::Motion::Kinematic ? "kinematic"
                                                                       : "dynamic";
      const auto shape = value.shape == Physics::Shape::Box       ? "box"
                         : value.shape == Physics::Shape::Sphere  ? "sphere"
                         : value.shape == Physics::Shape::Capsule ? "capsule"
                                                                  : "mesh";
      auto result
          = Json{{"motion", motion},
                 {"collisionDetection",
                  value.collisionDetection == Physics::CollisionDetection::Continuous ? "continuous"
                                                                                      : "discrete"},
                 {"shape", shape},
                 {"halfExtents", {value.halfExtents.x, value.halfExtents.y, value.halfExtents.z}},
                 {"radius", value.radius},
                 {"halfHeight", value.halfHeight},
                 {"velocity", {value.velocity.x, value.velocity.y, value.velocity.z}},
                 {"mass", value.mass},
                 {"friction", value.friction},
                 {"restitution", value.restitution},
                 {"gravityFactor", value.gravityFactor},
                 {"sensor", value.sensor}};

      if (value.shape == Physics::Shape::Mesh) {
        result["meshAsset"] = *value.meshAsset;
      }

      return result;
    }

    auto characterJson(const CharacterController& value) -> Json {
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
  }  // namespace

  auto encodeScene(const Scene& scene) -> Core::Result<std::string> {
    auto name = scene.name();
    auto entities = scene.entities();
    auto next = scene.nextEntityValue();

    if (!next) {
      return std::unexpected(next.error());
    }

    if (!name) {
      return std::unexpected(name.error());
    }

    if (!entities) {
      return std::unexpected(entities.error());
    }

    Json records = Json::array();

    for (const auto& entity : *entities) {
      const auto& transform = entity.transform;
      Json skin = nullptr;

      if (entity.skin) {
        auto encoded = Animation::encodeSkinProperties(*entity.skin);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        skin = Codec::parseJson(*encoded);
      }

      Json animation = nullptr;

      if (entity.animation) {
        auto encoded = Animation::encodePlayerProperties(*entity.animation);

        if (!encoded) {
          return std::unexpected(encoded.error());
        }

        animation = Codec::parseJson(*encoded);
      }

      records.push_back(
          {{"id", std::to_string(entity.id.value)},
           {"name", entity.name},
           {"parent", entity.parent ? Json(std::to_string(entity.parent->value)) : Json(nullptr)},
           {"transform",
            {{"position", {transform.position.x, transform.position.y, transform.position.z}},
             {"rotation",
              {transform.rotation.x, transform.rotation.y, transform.rotation.z,
               transform.rotation.w}},
             {"scale", {transform.scale.x, transform.scale.y, transform.scale.z}}}},
           {"camera", entity.camera ? cameraJson(*entity.camera) : Json(nullptr)},
           {"mesh", entity.mesh ? meshJson(*entity.mesh) : Json(nullptr)},
           {"material", entity.material ? Json{{"asset", entity.material->asset}} : Json(nullptr)},
           {"light", entity.light ? lightJson(*entity.light) : Json(nullptr)},
           {"rigidBody", entity.rigidBody ? bodyJson(*entity.rigidBody) : Json(nullptr)},
           {"character", entity.character ? characterJson(*entity.character) : Json(nullptr)},
           {"audioSource", entity.audioSource ? sourceJson(*entity.audioSource) : Json(nullptr)},
           {"audioListener", entity.audioListener ? Json{{"primary", entity.audioListener->primary}}
                                                  : Json(nullptr)},
           {"animation", std::move(animation)},
           {"skin", std::move(skin)},
           {"script", entity.script
                          ? Json{{"source", entity.script->source},
                                 {"className", entity.script->className},
                                 {"properties", Codec::parseJson(entity.script->properties, 32)},
                                 {"enabled", entity.script->enabled}}
                          : Json(nullptr)}});
    }

    auto environment = scene.environment();

    if (!environment) {
      return std::unexpected(environment.error());
    }

    Json environmentJson = nullptr;

    if (*environment) {
      const auto& e = **environment;
      environmentJson = {{"asset", e.asset},
                         {"intensity", e.intensity},
                         {"rotationDegrees", e.rotationDegrees},
                         {"background", e.background},
                         {"radianceSize", e.processing.radianceSize},
                         {"diffuseSize", e.processing.diffuseSize},
                         {"specularSize", e.processing.specularSize},
                         {"brdfSize", e.processing.brdfSize},
                         {"samples", e.processing.samples}};
    }

    auto occlusion = scene.occlusion();

    if (!occlusion) {
      return std::unexpected(occlusion.error());
    }

    Json occlusionJson = nullptr;

    if (*occlusion) {
      const auto& settings = **occlusion;
      occlusionJson = {{"radius", settings.radius},     {"bias", settings.bias},
                       {"strength", settings.strength}, {"directions", settings.directions},
                       {"steps", settings.steps},       {"filterRadius", settings.filterRadius}};
    }

    auto ui = scene.ui();

    if (!ui) {
      return std::unexpected(ui.error());
    }

    auto navigation = scene.navigation();

    if (!navigation) {
      return std::unexpected(navigation.error());
    }

    auto document = Json{{"version", 1},
                         {"name", *name},
                         {"nextEntity", *next ? Json(std::to_string(**next)) : Json(nullptr)},
                         {"entities", std::move(records)},
                         {"environment", std::move(environmentJson)},
                         {"occlusion", std::move(occlusionJson)},
                         {"ui", *ui ? Json(**ui) : Json(nullptr)},
                         {"navigation", *navigation ? Json(**navigation) : Json(nullptr)}};
    auto links = scene.sceneLinks();

    if (!links) {
      return std::unexpected(links.error());
    }

    if (!links->empty()) {
      document["links"] = Json::array();

      for (const auto& link : *links) {
        Json mapping = Json::array();
        const auto keyJson = [](const SceneSourceKey& key) {
          auto value = Json::array();

          for (auto component : key) {
            value.push_back(std::to_string(component));
          }

          return value;
        };

        for (const auto& pair : link.mapping) {
          mapping.push_back({{"source", std::to_string(pair.source)},
                             {"target", std::to_string(pair.target)},
                             {"key", keyJson(pair.key)}});
        }

        Json namespaces = Json::array();

        for (const auto& space : link.namespaces) {
          namespaces.push_back(
              {{"prefix", keyJson(space.prefix)},
               {"asset", space.asset},
               {"entities", keyJson(space.entities)},
               {"nextEntity",
                space.nextEntity ? Json(std::to_string(*space.nextEntity)) : Json(nullptr)}});
        }

        document["links"].push_back({{"root", std::to_string(link.root)},
                                     {"asset", link.asset},
                                     {"baseline", Codec::parseJson(link.baseline)},
                                     {"mapping", std::move(mapping)},
                                     {"namespaces", std::move(namespaces)}});
      }
    }

    auto encoded = document.dump(2) + '\n';

    if (encoded.size() > maxFileBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Encoded scene exceeds 64 MiB");
    }

    return encoded;
  }

  auto decodeScene(std::string_view source) -> Core::Result<Scene> {
    if (source.empty() || source.size() > maxFileBytes
        || source.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Scene source must be nonempty, at most 64 MiB, and contain no NUL bytes");
    }

    try {
      return parsedScene(Codec::parseJson(source));
    } catch (const Json::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("Decode scene JSON: {}", error.what()));
    } catch (const std::invalid_argument& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        std::format("Decode scene: {}", error.what()));
    }
  }

  auto loadScene(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<Scene> {
    auto checked = validateExtension(relative);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto source = files.readText(relative, maxFileBytes);

    if (!source) {
      return std::unexpected(source.error());
    }

    return decodeScene(*source);
  }

  auto saveScene(const Core::FileSystem& files, const std::filesystem::path& relative,
                 const Scene& scene) -> Core::Result<void> {
    auto checked = validateExtension(relative);

    if (!checked) {
      return checked;
    }

    auto source = encodeScene(scene);

    if (!source) {
      return std::unexpected(source.error());
    }

    return files.writeTextAtomic(relative, *source);
  }
}  // namespace Dot::World
