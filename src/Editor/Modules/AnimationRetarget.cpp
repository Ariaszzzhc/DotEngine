module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../../Engine/Serialization/Json.hpp"

module Dot.Editor.AnimationRetarget;

import Dot.Animation.Files;
import Dot.Animation.ScenePlayers;
import Dot.Core.Paths;
import Dot.Core.Text;
import Dot.Editor.AnimationPreview;
import Dot.World.SceneFiles;

namespace Dot::Editor {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;

    auto referencePath(std::string_view path, std::string_view extension) -> bool {
      return path.size() <= 1024 && Core::isValidUtf8(path) && Core::isPortableRelativePath(path)
             && path.starts_with("assets/") && path.ends_with(extension)
             && path.find("/.dot-stage-") == std::string_view::npos;
    }

    auto settingsValid(const RetargetProfile& profile) -> Core::Result<void> {
      const auto& settings = profile.settings;
      const double rate = settings.rate.denominator
                              ? double(settings.rate.numerator) / settings.rate.denominator
                              : 0;

      if (!referencePath(profile.sourceScene, ".dotscene")
          || !referencePath(profile.targetScene, ".dotscene")
          || !referencePath(profile.clip, ".dotanimation") || !profile.sourceEntity
          || !profile.targetEntity || settings.name.empty() || settings.name.size() > 256
          || !Core::isValidUtf8(settings.name) || settings.name.find('\0') != std::string::npos
          || settings.mappings.empty() || settings.mappings.size() > 10000
          || !settings.rate.denominator || settings.rate.denominator > 10000
          || settings.rate.numerator > 240000 || !std::isfinite(rate) || rate < 1 || rate > 240
          || (settings.translation
              && (!std::isfinite(settings.translation->scale) || settings.translation->scale < 1e-6
                  || settings.translation->scale > 1e6))) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid retarget profile references/settings");
      }

      std::set<std::uint32_t> source, target;

      for (const auto& mapping : settings.mappings) {
        if (mapping.source >= 10000 || mapping.target >= 10000
            || !source.insert(mapping.source).second || !target.insert(mapping.target).second
            || mapping.sourceName.size() > 1024 || mapping.targetName.size() > 1024
            || !Core::isValidUtf8(mapping.sourceName) || !Core::isValidUtf8(mapping.targetName)
            || mapping.sourceName.find('\0') != std::string::npos
            || mapping.targetName.find('\0') != std::string::npos) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid/duplicate retarget mapping");
        }
      }

      if (settings.translation && !target.contains(settings.translation->target)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Retarget displacement target must be mapped");
      }

      return {};
    }

    auto path(std::string_view value) -> std::filesystem::path {
      return std::filesystem::path{std::u8string(value.begin(), value.end())};
    }

    auto reference(const World::Scene& scene, std::uint64_t owner)
        -> Core::Result<RetargetReference> {
      auto entity = scene.findEntity(owner);
      auto info = entity ? scene.entity(*entity)
                         : Core::Result<World::EntityInfo>{std::unexpected(entity.error())};

      if (!info || !info->animation) {
        return info ? Core::fail(Core::ErrorCode::InvalidArgument,
                                 "Reference needs an animation owner")
                    : std::unexpected(info.error());
      }

      auto rig = Animation::captureRetargetRig(scene, *entity);
      return rig ? Core::Result<RetargetReference>{{owner, info->name, info->animation->clips,
                                                    std::move(*rig)}}
                 : std::unexpected(rig.error());
    }

    auto prepare(const Core::FileSystem& files, std::string initial, std::uint64_t owner,
                 const std::string& output, const RetargetProfile& profile, std::string metadata,
                 std::stop_token stop) -> Core::Result<RetargetCreation> {
      const auto cancelled = [&]() -> Core::Result<void> {
        return stop.stop_requested()
                   ? Core::fail(Core::ErrorCode::Unavailable, "Animation retarget cancelled")
                   : Core::Result<void>{};
      };

      if (auto check = cancelled(); !check) {
        return std::unexpected(check.error());
      }

      auto sourceText = files.readText(path(profile.sourceScene), 64 * 1024 * 1024);
      auto targetText = files.readText(path(profile.targetScene), 64 * 1024 * 1024);
      auto clipText = files.readText(path(profile.clip), 64 * 1024 * 1024);

      if (!sourceText) {
        return Core::fail(sourceText.error().code, "Retarget source scene " + profile.sourceScene
                                                       + ": " + sourceText.error().message);
      }

      if (!targetText) {
        return Core::fail(targetText.error().code, "Retarget target scene " + profile.targetScene
                                                       + ": " + targetText.error().message);
      }

      if (!clipText) {
        return Core::fail(clipText.error().code,
                          "Retarget source clip " + profile.clip + ": " + clipText.error().message);
      }

      auto sourceScene = World::decodeScene(*sourceText);
      auto targetScene = World::decodeScene(*targetText);
      auto authoring = World::decodeScene(initial);
      auto clip = Animation::decodeClip(*clipText);

      if (!sourceScene || !targetScene || !authoring || !clip) {
        return std::unexpected(!sourceScene   ? sourceScene.error()
                               : !targetScene ? targetScene.error()
                               : !authoring   ? authoring.error()
                                              : clip.error());
      }

      auto source = reference(*sourceScene, profile.sourceEntity);
      auto target = reference(*targetScene, profile.targetEntity);

      if (!source || !target) {
        return std::unexpected(!source ? source.error() : target.error());
      }

      if (std::ranges::find(source->clips, profile.clip) == source->clips.end()) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Source clip is not in the reference player's library");
      }

      auto compatible = validateRetargetTarget(*authoring, owner, target->rig);

      if (!compatible) {
        return std::unexpected(compatible.error());
      }

      auto baked = Animation::retargetClip(source->rig, target->rig, *clip, profile.settings, stop);

      if (!baked) {
        return std::unexpected(baked.error());
      }

      if (auto check = cancelled(); !check) {
        return std::unexpected(check.error());
      }

      auto entity = authoring->findEntity(owner).value();
      auto player = authoring->entity(entity)->animation.value();
      player.clip = static_cast<std::uint32_t>(player.clips.size());
      player.clips.push_back(output);
      auto valid = Animation::validatePlayerProperties(player);

      if (!valid) {
        return std::unexpected(valid.error());
      }

      auto enabled = player;
      enabled.enabled = true;
      auto assigned = authoring->setAnimationPlayer(entity, enabled);

      if (!assigned) {
        return std::unexpected(assigned.error());
      }

      auto preview = AnimationPreview::create(*authoring, files, owner,
                                              Animation::ClipAsset{output, baked->clip});

      if (!preview) {
        return Core::fail(preview.error().code,
                          "Retarget native preview preflight: " + preview.error().message);
      }

      if (auto check = cancelled(); !check) {
        return std::unexpected(check.error());
      }

      auto encoded = Animation::encodeClip(baked->clip);

      if (!encoded) {
        return std::unexpected(encoded.error());
      }

      AnimationCreation animation{baked->clip, std::move(player), std::move(*encoded)};
      return RetargetCreation{std::move(animation),   std::move(*baked),      profile,
                              std::move(*sourceText), std::move(*targetText), std::move(*clipText),
                              std::move(metadata)};
    }
  }  // namespace

  auto decodeRetargetProfile(std::string_view text) -> Core::Result<RetargetProfile> {
    if (text.empty() || text.size() > 512 * 1024 || !Core::isValidUtf8(text)
        || text.find('\0') != std::string_view::npos) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid retarget profile bytes");
    }

    try {
      const auto value = Codec::parseJson(text, 32);
      Codec::requireFields(value, {"sourceScene", "sourceEntity", "targetScene", "targetEntity",
                                   "clip", "name", "rate", "mappings", "translation"});
      Codec::requireFields(value["rate"], {"numerator", "denominator"});

      if (!value["rate"]["numerator"].is_number_unsigned() || value["rate"]["numerator"] > 240000
          || !value["rate"]["denominator"].is_number_unsigned()
          || value["rate"]["denominator"] > 10000 || !value["mappings"].is_array()
          || value["mappings"].size() > 10000) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid retarget profile types/capacities");
      }

      RetargetProfile result{value["sourceScene"].get<std::string>(),
                             Codec::id(value["sourceEntity"]),
                             value["targetScene"].get<std::string>(),
                             Codec::id(value["targetEntity"]),
                             value["clip"].get<std::string>(),
                             {value["name"].get<std::string>(),
                              {},
                              {value["rate"]["numerator"].get<std::uint32_t>(),
                               value["rate"]["denominator"].get<std::uint32_t>()},
                              {}}};

      for (const auto& mapping : value["mappings"]) {
        Codec::requireFields(mapping, {"source", "target", "sourceName", "targetName"});

        if (!mapping["source"].is_number_unsigned() || mapping["source"] > 9999
            || !mapping["target"].is_number_unsigned() || mapping["target"] > 9999) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid retarget logical node");
        }

        result.settings.mappings.push_back(
            {mapping["source"].get<std::uint32_t>(), mapping["target"].get<std::uint32_t>(),
             mapping["sourceName"].get<std::string>(), mapping["targetName"].get<std::string>()});
      }

      if (!value["translation"].is_null()) {
        const auto& displacement = value["translation"];
        Codec::requireFields(displacement, {"target", "scale"});

        if (!displacement["target"].is_number_unsigned() || displacement["target"] > 9999
            || !displacement["scale"].is_number()) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid retarget displacement");
        }

        result.settings.translation = Animation::RetargetTranslation{
            displacement["target"].get<std::uint32_t>(), displacement["scale"].get<double>()};
      }

      auto valid = settingsValid(result);
      return valid ? Core::Result<RetargetProfile>{std::move(result)}
                   : std::unexpected(valid.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode retarget profile: " + std::string(error.what()));
    }
  }

  auto encodeRetargetProfile(const RetargetProfile& profile) -> Core::Result<std::string> {
    auto valid = settingsValid(profile);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    try {
      const auto& settings = profile.settings;
      Json mappings = Json::array();

      for (const auto& pair : settings.mappings) {
        mappings.push_back({{"source", pair.source},
                            {"target", pair.target},
                            {"sourceName", pair.sourceName},
                            {"targetName", pair.targetName}});
      }

      auto text = Json{{"sourceScene", profile.sourceScene},
                       {"sourceEntity", std::to_string(profile.sourceEntity)},
                       {"targetScene", profile.targetScene},
                       {"targetEntity", std::to_string(profile.targetEntity)},
                       {"clip", profile.clip},
                       {"name", settings.name},
                       {"rate",
                        {{"numerator", settings.rate.numerator},
                         {"denominator", settings.rate.denominator}}},
                       {"mappings", std::move(mappings)},
                       {"translation", settings.translation
                                           ? Json{{"target", settings.translation->target},
                                                  {"scale", settings.translation->scale}}
                                           : Json(nullptr)}}
                      .dump();

      if (text.size() > 512 * 1024) {
        return Core::fail(Core::ErrorCode::Unavailable, "Retarget profile exceeds 512 KiB");
      }

      return text;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Encode retarget profile: " + std::string(error.what()));
    }
  }

  auto inspectRetargetReference(const Core::FileSystem& files, std::string_view name)
      -> Core::Result<std::vector<RetargetReference>> {
    if (!referencePath(name, ".dotscene")) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Choose an assets native reference scene");
    }

    auto scene = World::loadScene(files, path(name));

    if (!scene) {
      return std::unexpected(scene.error());
    }

    try {
      auto players = scene->animationPlayers();
      std::vector<RetargetReference> result;

      if (!players) {
        return std::unexpected(players.error());
      }

      if (players->size() > 64) {
        return Core::fail(Core::ErrorCode::Unavailable, "Retarget reference exceeds 64 players");
      }

      std::size_t nodes{};

      for (const auto& player : *players) {
        auto captured = reference(*scene, player.entity.value);

        if (!captured) {
          return std::unexpected(captured.error());
        }

        nodes += captured->rig.nodes.size();

        if (nodes > 30000) {
          return Core::fail(Core::ErrorCode::Unavailable,
                            "Retarget reference inspection exceeds 30000 nodes");
        }

        result.push_back(std::move(*captured));
      }

      return result;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Inspect retarget reference: " + std::string(error.what()));
    }
  }

  auto validateRetargetTarget(const World::Scene& scene, std::uint64_t owner,
                              const Animation::RetargetRig& reference) -> Core::Result<void> {
    auto valid = Animation::validateRetargetRig(reference);

    if (!valid) {
      return valid;
    }

    auto entity = scene.findEntity(owner);
    auto rig = entity ? Animation::captureRetargetRig(scene, *entity)
                      : Core::Result<Animation::RetargetRig>{std::unexpected(entity.error())};

    if (!rig) {
      return std::unexpected(rig.error());
    }

    using Parent = std::optional<std::uint32_t>;
    std::map<std::uint32_t, std::pair<std::uint32_t, Parent>> expected, actual;
    const auto bindings = [](const Animation::RetargetRig& value, auto& output) {
      for (std::size_t i = 0; i < value.nodes.size(); ++i) {
        const auto& node = value.nodes[i];

        if (!node.binding) {
          continue;
        }

        auto parent = node.parent;

        while (parent && !value.nodes[*parent].binding) {
          parent = value.nodes[*parent].parent;
        }

        output.emplace(*node.binding, std::pair{static_cast<std::uint32_t>(i),
                                                parent ? value.nodes[*parent].binding : Parent{}});
      }
    };
    bindings(reference, expected);
    bindings(*rig, actual);

    if (expected.size() != actual.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Target reference binding count differs from selected player");
    }

    for (const auto& [binding, entry] : expected) {
      const auto found = actual.find(binding);

      if (found == actual.end() || entry.second != found->second.second
          || ((entry.first != reference.anchor || found->second.first != rig->anchor)
              && reference.nodes[entry.first].name != rig->nodes[found->second.first].name)) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            "Target reference binding name/ancestry differs at " + std::to_string(binding));
      }

      auto first = reference.nodes[entry.first].parent;
      auto last = rig->nodes[found->second.first].parent;

      while (first && !reference.nodes[*first].binding) {
        if (!last || rig->nodes[*last].binding
            || reference.nodes[*first].name != rig->nodes[*last].name
            || reference.nodes[*first].reference.position != rig->nodes[*last].reference.position
            || reference.nodes[*first].reference.rotation != rig->nodes[*last].reference.rotation
            || reference.nodes[*first].reference.scale != rig->nodes[*last].reference.scale) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Target reference helper hierarchy differs");
        }

        first = reference.nodes[*first].parent;
        last = rig->nodes[*last].parent;
      }

      if (last && !rig->nodes[*last].binding) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Target player has extra unbound helpers");
      }
    }

    return {};
  }

  auto validateRetargetInputs(const Core::FileSystem& files, const RetargetCreation& creation)
      -> Core::Result<void> {
    const std::pair<std::string, const std::string*> inputs[]{
        {creation.profile.sourceScene, &creation.sourceBytes},
        {creation.profile.targetScene, &creation.targetBytes},
        {creation.profile.clip, &creation.clipBytes},
        {"project.dotproject", &creation.metadataBytes}};

    for (const auto& [name, expected] : inputs) {
      auto current = files.readText(path(name), 64 * 1024 * 1024);

      if (!current) {
        return Core::fail(current.error().code,
                          "Retarget input " + name + ": " + current.error().message);
      }

      if (*current != *expected) {
        return Core::fail(Core::ErrorCode::InvalidState,
                          "Retarget input changed; prepare again: " + name);
      }
    }

    return {};
  }

  struct AnimationRetarget::Impl {
    struct Completion {
      std::mutex m_mutex;
      std::optional<Core::Result<RetargetCreation>> m_result;
    };

    std::thread::id m_owner{std::this_thread::get_id()};
    bool m_taken{};
    std::shared_ptr<Completion> m_completion = std::make_shared<Completion>();
    std::jthread m_worker;

    Impl(Core::FileSystem files, std::string initial, std::uint64_t owner, std::string output,
         RetargetProfile profile, std::string metadata)
        : m_worker([state = m_completion, files = std::move(files), initial = std::move(initial),
                    owner, output = std::move(output), profile = std::move(profile),
                    metadata = std::move(metadata)](std::stop_token stop) {
            auto value = [&]() -> Core::Result<RetargetCreation> {
              try {
                return prepare(files, initial, owner, output, profile, metadata, stop);
              } catch (const std::exception& error) {
                return Core::fail(Core::ErrorCode::External,
                                  "Background animation retarget: " + std::string(error.what()));
              }
            }();
            std::lock_guard lock(state->m_mutex);
            state->m_result.emplace(std::move(value));
          }) {
    }

    auto check() const -> Core::Result<void> {
      return m_owner == std::this_thread::get_id()
                 ? Core::Result<void>{}
                 : Core::fail(Core::ErrorCode::InvalidState,
                              "Animation retarget requires its owner thread");
    }
  };

  AnimationRetarget::AnimationRetarget(std::unique_ptr<Impl> impl) noexcept
      : m_impl(std::move(impl)) {
  }

  AnimationRetarget::AnimationRetarget(AnimationRetarget&&) noexcept = default;
  auto AnimationRetarget::operator=(AnimationRetarget&&) noexcept -> AnimationRetarget& = default;
  AnimationRetarget::~AnimationRetarget() = default;

  auto AnimationRetarget::begin(const Project::Project& project, const World::Scene& scene,
                                std::uint64_t owner, std::string output, RetargetProfile profile)
      -> Core::Result<AnimationRetarget> {
    auto valid = encodeRetargetProfile(profile);
    auto initial = World::encodeScene(scene);
    auto files = project.files();

    if (!valid || !initial || !files || !referencePath(output, ".dotanimation")) {
      return !valid     ? std::unexpected(valid.error())
             : !initial ? std::unexpected(initial.error())
             : !files   ? std::unexpected(files.error())
                        : Core::fail(Core::ErrorCode::InvalidArgument,
                                     "Choose a new assets animation path");
    }

    auto entity = scene.findEntity(owner);
    auto info = entity ? scene.entity(*entity)
                       : Core::Result<World::EntityInfo>{std::unexpected(entity.error())};

    if (!info || !info->animation) {
      return info ? Core::fail(Core::ErrorCode::InvalidArgument,
                               "Retarget creation requires an existing animation owner")
                  : std::unexpected(info.error());
    }

    auto metadata = files->readText("project.dotproject", 1024 * 1024);

    if (!metadata) {
      return std::unexpected(metadata.error());
    }

    try {
      return AnimationRetarget{std::make_unique<Impl>(std::move(*files), std::move(*initial), owner,
                                                      std::move(output), std::move(profile),
                                                      std::move(*metadata))};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Begin animation retarget: " + std::string(error.what()));
    }
  }

  auto AnimationRetarget::ready() const -> Core::Result<bool> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation retarget was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);
    return lock.owns_lock() && m_impl->m_completion->m_result.has_value();
  }

  auto AnimationRetarget::take() -> Core::Result<RetargetCreation> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation retarget was moved");
    }

    auto checked = m_impl->check();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    if (m_impl->m_taken) {
      return Core::fail(Core::ErrorCode::InvalidState,
                        "Animation retarget result was already taken");
    }

    std::unique_lock lock(m_impl->m_completion->m_mutex, std::try_to_lock);

    if (!lock.owns_lock() || !m_impl->m_completion->m_result) {
      return Core::fail(Core::ErrorCode::Unavailable, "Animation retarget is pending");
    }

    m_impl->m_taken = true;
    return std::move(*m_impl->m_completion->m_result);
  }

  auto AnimationRetarget::requestStop() noexcept -> void {
    if (m_impl) {
      m_impl->m_worker.request_stop();
    }
  }
}  // namespace Dot::Editor
