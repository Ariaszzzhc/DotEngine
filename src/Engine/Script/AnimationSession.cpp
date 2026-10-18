module;

#include <array>
#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "ComponentJson.hpp"

module Dot.Script.AnimationSession;

import Dot.Animation.ScenePlayers;
import Dot.Animation.ControllerFiles;
import Dot.Core.Text;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Reply = Core::Result<std::optional<std::string>>;
    constexpr std::array methods{"animation.state",      "animation.play",
                                 "animation.pause",      "animation.stop",
                                 "animation.seek",       "animation.configure",
                                 "animation.crossFade",  "animation.layer",
                                 "animation.parameters", "animation.resetController"};

    auto written(Core::Result<void> value) -> Reply {
      return value ? Reply{std::optional<std::string>{}} : std::unexpected(value.error());
    }
  }  // namespace

  struct AnimationSession::Impl {
    Animation::ScenePlayers m_players;

    explicit Impl(Animation::ScenePlayers players) : m_players(std::move(players)) {
    }

    auto invoke(std::string_view method, std::string_view arguments) -> Reply {
      auto valid = m_players.stats();

      if (!valid) {
        return std::unexpected(valid.error());
      }

      if (arguments.empty() || arguments.size() > 32 * 1024 || !Core::isValidUtf8(arguments)
          || arguments.find('\0') != std::string_view::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid animation host argument bytes");
      }

      try {
        const auto args = Codec::parseJson(arguments, 8);
        const auto count = method == "animation.play" || method == "animation.configure"
                                   || method == "animation.crossFade" || method == "animation.layer"
                               ? 3U
                           : method == "animation.seek" || method == "animation.parameters" ? 2U
                                                                                            : 1U;

        if (!args.is_array() || args.size() != count) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Incorrect animation arguments");
        }

        Codec::requireFields(args[0], {"scene", "value"});
        const World::EntityId owner{Codec::id(args[0]["scene"]), Codec::id(args[0]["value"])};

        if (method == "animation.state") {
          auto state = m_players.state(owner);

          if (!state) {
            return std::unexpected(state.error());
          }

          Json transition = nullptr;
          Json layers = Json::array();

          for (const auto& layer : state->layers) {
            layers.push_back(
                {{"name", layer.name},
                 {"durationSeconds", layer.duration},
                 {"timeSeconds", layer.playback.time},
                 {"weight", layer.playback.weight},
                 {"speed", layer.playback.speed},
                 {"looping", layer.playback.looping},
                 {"playing", layer.playback.playing},
                 {"active", layer.playback.active},
                 {"paused", layer.playback.paused},
                 {"fade", layer.playback.fade
                              ? Json{{"from", layer.playback.fade->from},
                                     {"to", layer.playback.fade->to},
                                     {"elapsedSeconds", layer.playback.fade->elapsed},
                                     {"durationSeconds", layer.playback.fade->duration},
                                     {"stop", layer.playback.fade->stop}}
                              : Json(nullptr)}});
          }

          if (state->playback.transition) {
            transition = {{"elapsedSeconds", state->playback.transition->elapsed},
                          {"durationSeconds", state->playback.transition->duration}};
          }

          Json controller = nullptr;

          if (state->controller) {
            const auto& observed = *state->controller;
            Json parameters = Json::object();

            for (const auto& parameter : observed.playback.parameters) {
              std::visit([&](auto value) { parameters[parameter.name] = value; }, parameter.value);
            }

            controller = {{"asset", observed.asset},
                          {"name", observed.name},
                          {"state", observed.playback.state},
                          {"elapsedSeconds", observed.playback.elapsedSeconds},
                          {"parameters", std::move(parameters)},
                          {"lastTransition", observed.playback.lastTransition
                                                 ? Json(*observed.playback.lastTransition)
                                                 : Json(nullptr)},
                          {"transitions", std::to_string(observed.playback.transitions)}};
          }

          return std::optional{Json{{"clip", state->clip},
                                    {"asset", state->asset},
                                    {"name", state->name},
                                    {"durationSeconds", state->duration},
                                    {"timeSeconds", state->playback.time},
                                    {"speed", state->playback.speed},
                                    {"looping", state->playback.looping},
                                    {"playing", state->playback.playing},
                                    {"paused", state->playback.paused},
                                    {"layers", std::move(layers)},
                                    {"controller", std::move(controller)},
                                    {"transition", std::move(transition)}}
                                   .dump()};
        }

        if (method == "animation.parameters") {
          auto parameters = Animation::decodeControllerParameters(args[1].dump());
          return parameters ? written(m_players.parameters(owner, *parameters))
                            : Reply{std::unexpected(parameters.error())};
        }

        if (method == "animation.resetController") {
          return written(m_players.resetController(owner));
        }

        if (method == "animation.play") {
          std::optional<std::uint32_t> clip;

          if (!args[1].is_null()) {
            if (!args[1].is_number_unsigned() || args[1] > 1023) {
              return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid animation clip index");
            }

            clip = args[1].get<std::uint32_t>();
          }

          return written(m_players.play(owner, clip, args[2].get<bool>()));
        }

        if (method == "animation.layer") {
          const auto name = args[1].get<std::string>();
          auto operation = Animation::decodeLayerControl(args[2].dump());
          return operation ? written(m_players.controlLayer(owner, name, *operation))
                           : Reply{std::unexpected(operation.error())};
        }

        if (method == "animation.pause") {
          return written(m_players.pause(owner));
        }

        if (method == "animation.crossFade") {
          if (!args[1].is_number_unsigned() || args[1] > 1023 || !args[2].is_number()) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Animation cross-fade requires a clip index and seconds");
          }

          return written(
              m_players.crossFade(owner, args[1].get<std::uint32_t>(), args[2].get<double>()));
        }

        if (method == "animation.stop") {
          return written(m_players.stop(owner));
        }

        if (!args[1].is_number()) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Animation control requires a number");
        }

        if (method == "animation.seek") {
          return written(m_players.seek(owner, args[1].get<double>()));
        }

        return written(m_players.configure(owner, args[1].get<double>(), args[2].get<bool>()));
      } catch (const std::invalid_argument& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Animation JSON: " + std::string(error.what()));
      } catch (const Json::exception& error) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Animation JSON: " + std::string(error.what()));
      } catch (const std::exception& error) {
        return Core::fail(Core::ErrorCode::External,
                          "Animation host operation: " + std::string(error.what()));
      }
    }
  };

  AnimationSession::AnimationSession(std::shared_ptr<Impl> impl) : m_impl(std::move(impl)) {
  }

  AnimationSession::AnimationSession(AnimationSession&&) noexcept = default;
  auto AnimationSession::operator=(AnimationSession&&) noexcept -> AnimationSession& = default;
  AnimationSession::~AnimationSession() = default;

  auto AnimationSession::create(std::shared_ptr<World::Scene> scene, const Core::FileSystem& files)
      -> Core::Result<AnimationSession> {
    return createSession(std::move(scene), files, {}, {}, false);
  }

  auto AnimationSession::createPrepared(std::shared_ptr<World::Scene> scene,
                                        const Core::FileSystem& files,
                                        std::span<const Animation::ClipAsset> decoded,
                                        std::span<const Animation::ControllerAsset> controllers)
      -> Core::Result<AnimationSession> {
    return createSession(std::move(scene), files, decoded, controllers, true);
  }

  auto AnimationSession::createSession(std::shared_ptr<World::Scene> scene,
                                       const Core::FileSystem& files,
                                       std::span<const Animation::ClipAsset> decoded,
                                       std::span<const Animation::ControllerAsset> controllers,
                                       bool prepared) -> Core::Result<AnimationSession> {
    auto players = prepared ? Animation::ScenePlayers::createPrepared(std::move(scene), files,
                                                                      decoded, controllers)
                            : Animation::ScenePlayers::create(std::move(scene), files);

    if (!players) {
      return std::unexpected(players.error());
    }

    try {
      return AnimationSession(std::make_shared<Impl>(std::move(*players)));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create animation session: " + std::string(error.what()));
    }
  }

  auto AnimationSession::advance(double seconds) -> Core::Result<void> {
    return m_impl ? m_impl->m_players.advance(seconds)
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
  }

  auto AnimationSession::advancePrepared(double seconds,
                                         std::span<const Animation::ClipAsset> decoded,
                                         std::span<const Animation::ControllerAsset> controllers)
      -> Core::Result<void> {
    return m_impl ? m_impl->m_players.advancePrepared(seconds, decoded, controllers)
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
  }

  auto AnimationSession::state(World::EntityId owner) const
      -> Core::Result<Animation::ScenePlayerState> {
    return m_impl ? m_impl->m_players.state(owner)
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
  }

  auto AnimationSession::parameters(World::EntityId owner,
                                    std::span<const Animation::ControllerParameter> parameters)
      -> Core::Result<void> {
    return m_impl ? m_impl->m_players.parameters(owner, parameters)
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
  }

  auto AnimationSession::resetController(World::EntityId owner) -> Core::Result<void> {
    return m_impl ? m_impl->m_players.resetController(owner)
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
  }

  auto AnimationSession::controlLayer(World::EntityId owner, std::string_view name,
                                      const Animation::LayerControl& operation)
      -> Core::Result<void> {
    return m_impl ? m_impl->m_players.controlLayer(owner, name, operation)
                  : Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
  }

  auto AnimationSession::inspect(World::EntityId owner) const -> Core::Result<std::string> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
    }

    auto result
        = m_impl->invoke("animation.state", Json::array({{{"scene", std::to_string(owner.scene)},
                                                          {"value", std::to_string(owner.value)}}})
                                                .dump());

    if (!result) {
      return std::unexpected(result.error());
    }

    if (!*result) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation state produced no snapshot");
    }

    return std::move(**result);
  }

  auto AnimationSession::bindings() const -> Core::Result<HostBindings> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Animation session was moved");
    }

    auto valid = m_impl->m_players.stats();

    if (!valid) {
      return std::unexpected(valid.error());
    }

    HostBindings bindings;

    for (const auto* method : methods) {
      bindings.push_back({method,
                          [impl = m_impl, method](std::string_view args) -> Reply {
                            return impl->invoke(method, args);
                          },
                          std::string_view(method) == "animation.state" ? HostAccess::ReadOnly
                                                                        : HostAccess::ReadWrite});
    }

    return bindings;
  }
}  // namespace Dot::Script
