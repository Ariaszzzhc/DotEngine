module;

#include <algorithm>
#include <cmath>
#include <exception>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <utility>

module Dot.Animation.Skin;

namespace Dot::Animation {
  namespace {
    auto affine(const Math::Mat4& matrix) -> Core::Result<void> {
      const auto& m = matrix.data;

      if (!Math::isFinite(matrix) || m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Skin matrix requires finite affine data");
      }

      auto inverse = Math::inverse(matrix);
      return inverse ? Core::Result<void>{} : std::unexpected(inverse.error());
    }
  }  // namespace

  struct Skin::Data {
    SkinDescription description;
  };

  Skin::Skin(std::shared_ptr<const Data> data) : m_data(std::move(data)) {
  }

  auto Skin::create(SkinDescription description) -> Core::Result<Skin> {
    const auto joints = description.inverseBindMatrices.size();

    if (joints == 0 || joints > 1024 || description.vertices.empty()
        || description.vertices.size() > 1'000'000) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skin joint/vertex capacity invalid");
    }

    for (std::size_t i = 0; i < joints; ++i) {
      auto valid = affine(description.inverseBindMatrices[i]);

      if (!valid) {
        return Core::fail(valid.error().code,
                          "Skin inverse bind " + std::to_string(i) + ": " + valid.error().message);
      }
    }

    for (std::size_t vertex = 0; vertex < description.vertices.size(); ++vertex) {
      auto& influence = description.vertices[vertex];
      double sum{};

      for (std::size_t i = 0; i < 8; ++i) {
        const auto weight = influence.weights[i];

        if (influence.joints[i] >= joints || !std::isfinite(weight) || weight < 0 || weight > 1) {
          return Core::fail(Core::ErrorCode::InvalidArgument,
                            "Invalid skin influence at vertex " + std::to_string(vertex));
        }

        for (std::size_t j = 0; j < i; ++j) {
          if (weight > 0 && influence.weights[j] > 0
              && influence.joints[i] == influence.joints[j]) {
            return Core::fail(Core::ErrorCode::InvalidArgument,
                              "Repeated weighted joint at vertex " + std::to_string(vertex));
          }
        }

        sum += weight;
      }

      if (sum == 0) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Zero skin weights at vertex " + std::to_string(vertex));
      }

      for (auto& weight : influence.weights) {
        weight = static_cast<float>(weight / sum);
      }
    }

    try {
      return Skin(std::make_shared<const Data>(std::move(description)));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Create skin: " + std::string(error.what()));
    }
  }

  auto Skin::description() const -> Core::Result<const SkinDescription*> {
    return m_data ? Core::Result<const SkinDescription*>{&m_data->description}
                  : Core::fail(Core::ErrorCode::InvalidState, "Skin is empty or moved");
  }

  auto validateSkinPalette(const Skin& skin, std::span<const Math::Mat4> palette)
      -> Core::Result<void> {
    auto description = skin.description();

    if (!description) {
      return std::unexpected(description.error());
    }

    if (palette.size() != (*description)->inverseBindMatrices.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skin palette joint count differs");
    }

    for (std::size_t i = 0; i < palette.size(); ++i) {
      auto valid = affine(palette[i]);

      if (!valid
          || std::ranges::any_of(palette[i].data, [](float v) { return std::abs(v) > 1e8F; })) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Invalid/over-limit skin palette joint " + std::to_string(i));
      }
    }

    return {};
  }

  auto Skin::palette(const Math::Mat4& meshWorld, std::span<const Math::Mat4> jointWorld) const
      -> Core::Result<std::vector<Math::Mat4>> {
    auto source = description();

    if (!source) {
      return std::unexpected(source.error());
    }

    if (jointWorld.size() != (*source)->inverseBindMatrices.size()) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Skin joint world count differs");
    }

    auto valid = affine(meshWorld);

    if (!valid) {
      return std::unexpected(valid.error());
    }

    auto inverseMesh = Math::inverse(meshWorld).value();
    std::vector<Math::Mat4> result;
    result.reserve(jointWorld.size());

    for (std::size_t i = 0; i < jointWorld.size(); ++i) {
      valid = affine(jointWorld[i]);

      if (!valid) {
        return Core::fail(valid.error().code,
                          "Skin joint world " + std::to_string(i) + ": " + valid.error().message);
      }

      auto local = Math::multiply(inverseMesh, jointWorld[i]);

      if (!local) {
        return std::unexpected(local.error());
      }

      auto combined = Math::multiply(*local, (*source)->inverseBindMatrices[i]);

      if (!combined) {
        return std::unexpected(combined.error());
      }

      result.push_back(*combined);
    }

    valid = validateSkinPalette(*this, result);
    return valid ? Core::Result<std::vector<Math::Mat4>>{std::move(result)}
                 : std::unexpected(valid.error());
  }
}  // namespace Dot::Animation
