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

#include "ComponentJson.hpp"

module Dot.Script.AnimationSession;

import Dot.Animation.ScenePlayers;
import Dot.Core.Text;

namespace Dot::Script {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    using Reply = Core::Result<std::optional<std::string>>;
    constexpr std::array methods{"animation.state",    "animation.play", "animation.pause",
                                 "animation.stop",     "animation.seek", "animation.configure",
                                 "animation.crossFade"};

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

      if (arguments.empty() || arguments.size() > 4096 || !Core::isValidUtf8(arguments)
          || arguments.find('\0') != std::string_view::npos) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid animation host argument bytes");
      }

      try {
        const auto args = Codec::parseJson(arguments, 8);
        const auto count = method == "animation.play" || method == "animation.configure"
                                   || method == "animation.crossFade"
                               ? 3U
                           : method == "animation.seek" ? 2U
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

          if (state->playback.transition) {
            transition = {{"elapsedSeconds", state->playback.transition->elapsed},
                          {"durationSeconds", state->playback.transition->duration}};
          }

          return std::optional{Json{{"clip", state->clip},
                                    {"asset", state->asset},
                                    {"name", state->name},
                                    {"durationSeconds", state->duration},
                                    {"timeSeconds", state->playback.time},
                                    {"speed", state->playback.speed},
                                    {"looping", state->playback.looping},
                                    {"playing", state->playback.playing},
                                    {"transition", std::move(transition)}}
                                   .dump()};
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
    return createSession(std::move(scene), files, {}, false);
  }

  auto AnimationSession::createPrepared(std::shared_ptr<World::Scene> scene,
                                        const Core::FileSystem& files,
                                        std::span<const Animation::ClipAsset> decoded)
      -> Core::Result<AnimationSession> {
    return createSession(std::move(scene), files, decoded, true);
  }

  auto AnimationSession::createSession(std::shared_ptr<World::Scene> scene,
                                       const Core::FileSystem& files,
                                       std::span<const Animation::ClipAsset> decoded, bool prepared)
      -> Core::Result<AnimationSession> {
    auto players = prepared
                       ? Animation::ScenePlayers::createPrepared(std::move(scene), files, decoded)
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
