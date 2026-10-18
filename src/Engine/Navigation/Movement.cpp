module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

module Dot.Navigation.Movement;

namespace Dot::Navigation {
  namespace {
    auto distance(Math::Vec3 a, Math::Vec3 b) -> double {
      return std::hypot(double(a.x) - b.x, double(a.z) - b.z);
    }

    auto valid(const MovementBody& value) -> bool {
      const auto& body = value.body;
      return body.id && Math::isFinite(body.position) && std::abs(body.position.x) <= 1e6F
             && std::abs(body.position.y) <= 1e6F && std::abs(body.position.z) <= 1e6F
             && Math::isFinite(body.velocity) && body.velocity.y == 0
             && Math::length(body.velocity) <= 1000 && std::isfinite(body.radius)
             && body.radius >= .05F && body.radius <= 5 && std::isfinite(body.height)
             && body.height >= .1F && body.height <= 20;
    }

    auto arrived(const MovementBody& body, const MoveRequest& request) -> bool {
      return body.grounded && distance(body.body.position, request.goal) <= request.stoppingDistance
             && std::abs(double(body.body.position.y) - request.goal.y) <= .35;
    }
  }  // namespace

  auto validateMoveRequest(const MoveRequest& request) -> Core::Result<void> {
    if (!Math::isFinite(request.goal) || std::abs(request.goal.x) > 1e6F
        || std::abs(request.goal.y) > 1e6F || std::abs(request.goal.z) > 1e6F
        || !std::isfinite(request.speed) || request.speed < .1F || request.speed > 10
        || !std::isfinite(request.acceleration) || request.acceleration < .1F
        || request.acceleration > 50 || !std::isfinite(request.stoppingDistance)
        || request.stoppingDistance < .05F || request.stoppingDistance > 1
        || !std::isfinite(request.neighborDistance) || request.neighborDistance < .5F
        || request.neighborDistance > 8 || !std::isfinite(request.timeHorizon)
        || request.timeHorizon < .25F || request.timeHorizon > 5) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid native navigation move request");
    }

    return {};
  }

  struct Movement::Impl {
    struct Agent {
      MoveRequest request;
      std::shared_ptr<const std::vector<Math::Vec3>> path;
      std::size_t corner{}, neighbors{};
      Math::Vec3 lastPosition, velocity, wallPosition;
      std::shared_ptr<const std::vector<WallSegment>> walls;
      double blockedSeconds{};
      std::optional<Core::Error> error;
    };

    Surface m_surface;
    std::map<std::uint64_t, Agent> m_agents;

    auto path(Math::Vec3 start, const MoveRequest& request) const
        -> Core::Result<std::shared_ptr<const std::vector<Math::Vec3>>> {
      auto route = m_surface.route({start, request.goal});

      if (!route) {
        return std::unexpected(route.error());
      }

      if (distance(route->goal, request.goal) > request.stoppingDistance
          || std::abs(double(route->goal.y) - request.goal.y) > .35) {
        return Core::fail(Core::ErrorCode::Unavailable,
                          "Goal is outside navigation arrival tolerance");
      }

      return std::make_shared<const std::vector<Math::Vec3>>(std::move(route->points));
    }

    auto desired(Agent& agent, const MovementBody& measured, double seconds) const
        -> Core::Result<Math::Vec3> {
      const auto& body = measured.body;

      if (agent.error || arrived(measured, agent.request)) {
        return Math::Vec3{};
      }

      if (distance(body.position, agent.lastPosition)
              > std::max(1.0, agent.request.speed * seconds * 4)
          || std::abs(double(body.position.y) - agent.lastPosition.y) > .5) {
        auto rebuilt = path(body.position, agent.request);

        if (!rebuilt) {
          return std::unexpected(rebuilt.error());
        }

        agent.path = std::move(*rebuilt);
        agent.corner = 0;
        agent.walls.reset();
      }

      while (agent.corner + 1 < agent.path->size()
             && distance(body.position, (*agent.path)[agent.corner]) <= .08
             && std::abs(double(body.position.y) - (*agent.path)[agent.corner].y) <= .35) {
        ++agent.corner;
      }

      if (agent.corner == agent.path->size()) {
        return Math::Vec3{};
      }

      /** Skip an obstructed old corner only when the following walkable segment is visible.
       * 仅在后续可行走段直视可达时跳过旧拐点，避免为绕动态角色而卡在旧拐点。 */
      for (auto candidate = std::min(agent.path->size() - 1, agent.corner + 4);
           candidate > agent.corner; --candidate) {
        if (distance(body.position, (*agent.path)[candidate]) > 2.5) {
          continue;
        }

        auto direct = m_surface.trace(body.position, (*agent.path)[candidate]);

        if (!direct) {
          return std::unexpected(direct.error());
        }

        if (direct->reached) {
          agent.corner = candidate;
          break;
        }
      }

      const auto target = (*agent.path)[agent.corner];
      const auto delta = Math::Vec3{target.x - body.position.x, 0, target.z - body.position.z};
      const auto length = Math::length(delta);
      const auto remaining = distance(body.position, agent.request.goal);
      const auto braking
          = std::sqrt(2 * agent.request.acceleration
                      * std::max(0.0, remaining - agent.request.stoppingDistance * .5));
      const auto speed = std::min({double(agent.request.speed), length / seconds, braking});
      return length > 0 ? delta * static_cast<float>(speed / length) : Math::Vec3{};
    }
  };

  Movement::Movement(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  Movement::Movement(Movement&&) noexcept = default;

  auto Movement::operator=(Movement&&) noexcept -> Movement& = default;

  Movement::~Movement() = default;

  auto Movement::create(Surface surface) -> Core::Result<Movement> {
    auto validSurface = surface.description();

    if (!validSurface) {
      return std::unexpected(validSurface.error());
    }

    try {
      auto impl = std::make_unique<Impl>();
      impl->m_surface = std::move(surface);
      return Movement{std::move(impl)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create navigation movement: " + std::string(error.what()));
    }
  }

  auto Movement::move(const MovementBody& body, const MoveRequest& request) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    auto checked = validateMoveRequest(request);

    if (!checked || !valid(body)) {
      return checked ? Core::fail(Core::ErrorCode::InvalidArgument, "Invalid movement body")
                     : checked;
    }

    try {
      if (!m_impl->m_agents.contains(body.body.id) && m_impl->m_agents.size() == 256) {
        return Core::fail(Core::ErrorCode::Unavailable, "Navigation movement capacity exhausted");
      }

      auto path = m_impl->path(body.body.position, request);

      if (!path) {
        return std::unexpected(path.error());
      }

      Impl::Agent agent;
      agent.request = request;
      agent.path = std::move(*path);
      agent.lastPosition = body.body.position;
      m_impl->m_agents.insert_or_assign(body.body.id, std::move(agent));
      return {};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Request navigation movement: " + std::string(error.what()));
    }
  }

  auto Movement::stop(std::uint64_t entity) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    if (!entity) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Movement identity is empty");
    }

    m_impl->m_agents.erase(entity);
    return {};
  }

  auto Movement::state(const MovementBody& body) const -> Core::Result<std::optional<MoveState>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    if (!valid(body)) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid movement observation");
    }

    const auto found = m_impl->m_agents.find(body.body.id);

    if (found == m_impl->m_agents.end()) {
      return std::optional<MoveState>{};
    }

    const auto& agent = found->second;
    return std::optional{MoveState{
        agent.request,
        agent.error                    ? MovePhase::Failed
        : arrived(body, agent.request) ? MovePhase::Arrived
                                       : MovePhase::Moving,
        agent.velocity, static_cast<float>(distance(body.body.position, agent.request.goal)),
        agent.blockedSeconds >= .75, agent.corner, agent.path->size(), agent.neighbors,
        agent.error}};
  }

  auto Movement::entities() const -> Core::Result<std::vector<std::uint64_t>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    std::vector<std::uint64_t> result;

    for (const auto& [entity, agent] : m_impl->m_agents) {
      result.push_back(entity);
    }

    return result;
  }

  auto Movement::fail(const Core::Error& error) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    for (auto& [entity, agent] : m_impl->m_agents) {
      agent.error = error;
      agent.velocity = {};
    }

    return {};
  }

  auto Movement::fail(std::uint64_t entity, const Core::Error& error) -> Core::Result<void> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    auto found = m_impl->m_agents.find(entity);

    if (found != m_impl->m_agents.end()) {
      found->second.error = error;
      found->second.velocity = {};
    }

    return {};
  }

  auto Movement::advance(std::span<const MovementBody> bodies, double seconds)
      -> Core::Result<std::vector<MovementIntent>> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Navigation movement was moved");
    }

    if (!std::isfinite(seconds) || seconds < 1e-6 || seconds > 1 || bodies.size() > 256) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Invalid navigation movement phase/budget");
    }

    try {
      std::map<std::uint64_t, const MovementBody*> observed;

      for (const auto& body : bodies) {
        if (!valid(body) || !observed.emplace(body.body.id, &body).second) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid/duplicate navigation movement observation");
        }
      }

      auto prepared = m_impl->m_agents;
      std::erase_if(prepared, [&](const auto& entry) { return !observed.contains(entry.first); });
      std::map<std::uint64_t, Math::Vec3> desired;

      for (auto& [entity, agent] : prepared) {
        auto velocity = m_impl->desired(agent, *observed.at(entity), seconds);

        if (!velocity) {
          agent.error = velocity.error();
          agent.velocity = {};
          desired[entity] = {};
        } else {
          desired[entity] = *velocity;
        }
      }

      std::vector<SteeringBody> neighbors;
      neighbors.reserve(bodies.size());

      for (const auto& [entity, measured] : observed) {
        auto body = measured->body;
        body.desired = desired.contains(entity) ? desired.at(entity) : body.velocity;
        neighbors.push_back(body);
      }

      std::vector<MovementIntent> intents;
      intents.reserve(prepared.size());

      for (auto& [entity, agent] : prepared) {
        auto body = observed.at(entity)->body;
        body.desired = desired.at(entity);
        const auto speed = Math::length(body.desired);
        Math::Vec3 output{};

        if (!agent.error && speed > 1e-6) {
          if (!agent.walls || distance(body.position, agent.wallPosition) > .25
              || std::abs(double(body.position.y) - agent.wallPosition.y) > .15) {
            auto boundary = m_impl->m_surface.boundary(
                body.position, std::min(8.F, agent.request.neighborDistance + .5F));

            if (!boundary) {
              agent.error = boundary.error();
            } else {
              agent.walls
                  = std::make_shared<const std::vector<WallSegment>>(std::move(boundary->walls));
              agent.wallPosition = body.position;
            }
          }

          if (!agent.error) {
            auto steered = steer(
                body, neighbors, *agent.walls,
                {agent.request.speed, agent.request.neighborDistance, agent.request.timeHorizon});

            if (!steered) {
              agent.error = steered.error();
            } else {
              auto delta = steered->velocity - body.velocity;
              const auto length = Math::length(delta);
              const auto maximum = agent.request.acceleration * seconds;
              output = body.velocity
                       + (length > maximum ? delta * static_cast<float>(maximum / length) : delta);
              const auto magnitude = Math::length(output);

              if (magnitude > agent.request.speed) {
                output = output * static_cast<float>(agent.request.speed / magnitude);
              }

              auto traced = m_impl->m_surface.trace(
                  body.position, body.position + output * static_cast<float>(seconds));

              if (!traced) {
                agent.error = traced.error();
              } else if (!traced->reached) {
                const Math::Vec3 clamped{traced->position.x - body.position.x, 0,
                                         traced->position.z - body.position.z};
                const auto distance = Math::length(clamped);
                output = distance < magnitude * seconds ? clamped * static_cast<float>(1 / seconds)
                                                        : Math::Vec3{};
              }

              agent.neighbors = steered->neighbors;
            }
          }
        }

        if (agent.error) {
          output = {};
        }

        agent.blockedSeconds
            = speed > .1 && Math::length(body.velocity) < .05 ? agent.blockedSeconds + seconds : 0;
        agent.lastPosition = body.position;
        agent.velocity = output;
        intents.push_back({entity, output});
      }

      m_impl->m_agents.swap(prepared);
      return intents;
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Advance native navigation movement: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Navigation
