module;

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

module Dot.Editor.AssetPreview;

import Dot.Core.Paths;
import Dot.Math;
import Dot.Assets.Image;
import Dot.Assets.Mesh;
import Dot.Graphics.SceneRenderer;

namespace Dot::Editor {
  auto assetPreviewKind(std::string_view path) -> std::optional<AssetPreviewKind> {
    if (!Core::isPortableRelativePath(path)) {
      return {};
    }

    if (path.ends_with(".png") || path.ends_with(".jpg") || path.ends_with(".jpeg")) {
      return AssetPreviewKind::Image;
    }

    if (path.ends_with(".dotmesh") || path.ends_with(".obj")) {
      return AssetPreviewKind::Mesh;
    }

    return path.ends_with(".dotmaterial") ? std::optional{AssetPreviewKind::Material}
                                          : std::nullopt;
  }

  struct AssetPreview::Impl {
    Core::FileSystem files;
    Graphics::Renderer renderer;
    std::optional<Graphics::SceneRenderer> materials;
  };

  AssetPreview::AssetPreview(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {
  }

  AssetPreview::AssetPreview(AssetPreview&&) noexcept = default;

  auto AssetPreview::operator=(AssetPreview&&) noexcept -> AssetPreview& = default;

  AssetPreview::~AssetPreview() = default;

  auto AssetPreview::create(const Core::FileSystem& files, const Graphics::HdrImage* studio)
      -> Core::Result<AssetPreview> {
    try {
      auto renderer = Graphics::Renderer::create();

      if (!renderer) {
        return std::unexpected(renderer.error());
      }

      auto impl = std::make_unique<Impl>(Impl{files, std::move(*renderer), {}});
      auto materials = Graphics::SceneRenderer::create(impl->renderer, files, 64 * 1024 * 1024);

      if (!materials) {
        return std::unexpected(materials.error());
      }

      impl->materials.emplace(std::move(*materials));

      if (studio) {
        auto baked = impl->renderer.bakeEnvironment(*studio, {64, 16, 64, 64, 64});

        if (!baked) {
          return std::unexpected(baked.error());
        }

        auto bound
            = impl->renderer.setEnvironment(Graphics::EnvironmentLighting{*baked, 1, 0, false});

        if (!bound) {
          return std::unexpected(bound.error());
        }
      }

      return AssetPreview(std::move(impl));
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Create asset preview: " + std::string(error.what()));
    }
  }

  auto AssetPreview::render(std::string_view path, std::uint32_t size,
                            const Assets::MaterialDescription* draft)
      -> Core::Result<AssetPreviewImage> {
    if (!m_impl) {
      return Core::fail(Core::ErrorCode::InvalidState, "Asset preview was moved from");
    }

    auto checked = m_impl->renderer.adapterInfo();

    if (!checked) {
      return std::unexpected(checked.error());
    }

    const auto kind = assetPreviewKind(path);

    if (!kind || size < 16 || size > 512 || (draft && *kind != AssetPreviewKind::Material)) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Unsupported asset preview path/size/draft");
    }

    try {
      auto cleared = m_impl->materials->clearCache();

      if (!cleared) {
        return std::unexpected(cleared.error());
      }

      const std::filesystem::path source(std::u8string(path.begin(), path.end()));
      Graphics::MeshData geometry;
      Graphics::Material material;
      Math::Mat4 transform;
      Math::Vec3 eye{0, 0, 3};
      std::string warning;

      if (*kind == AssetPreviewKind::Image) {
        auto image = Assets::loadImage(m_impl->files, source);

        if (!image) {
          return std::unexpected(image.error());
        }

        const float side = static_cast<float>(std::max(image->width, image->height));
        geometry = Graphics::quad({1.9F * static_cast<float>(image->width) / side,
                                   1.9F * static_cast<float>(image->height) / side})
                       .value();
        auto texture
            = m_impl->renderer.uploadTexture(*image, {.addressU = Graphics::TextureAddress::Clamp,
                                                      .addressV = Graphics::TextureAddress::Clamp});

        if (!texture) {
          return std::unexpected(texture.error());
        }

        material.baseColorTexture = std::move(*texture);
        material.alphaMode = Graphics::AlphaMode::Blend;
      } else if (*kind == AssetPreviewKind::Material) {
        auto value = draft ? Core::Result<Assets::MaterialDescription>{*draft}
                           : Assets::loadMaterial(m_impl->files, source);

        if (!value) {
          return std::unexpected(value.error());
        }

        auto resolved = m_impl->materials->resolveMaterial(*value);

        if (!resolved) {
          return std::unexpected(resolved.error());
        }

        material = std::move(*resolved);
        geometry = Graphics::sphere(.85F).value();
      } else {
        auto mesh = Assets::loadMesh(m_impl->files, source);

        if (!mesh) {
          return std::unexpected(mesh.error());
        }

        auto surface = Graphics::meshSurface(mesh->data);

        if (!surface) {
          return std::unexpected(surface.error());
        }

        if (!surface->hasNormals) {
          auto flat = Graphics::withFlatNormals(mesh->data);

          if (!flat) {
            return std::unexpected(flat.error());
          }

          mesh->data = std::move(*flat);
        }

        const auto bounds = surface->bounds;
        const double span = std::max({double(bounds.maximum.x) - bounds.minimum.x,
                                      double(bounds.maximum.y) - bounds.minimum.y,
                                      double(bounds.maximum.z) - bounds.minimum.z});

        if (!(span > 0)) {
          return Core::fail(Core::ErrorCode::InvalidArgument, "Preview mesh has no extent");
        }

        const float scale = static_cast<float>(1.5 / span);
        const Math::Vec3 position{
            static_cast<float>(-(double(bounds.maximum.x) + bounds.minimum.x) * .5 * scale),
            static_cast<float>(-(double(bounds.maximum.y) + bounds.minimum.y) * .5 * scale),
            static_cast<float>(-(double(bounds.maximum.z) + bounds.minimum.z) * .5 * scale)};
        auto matrix = Math::compose(position, {}, {scale, scale, scale});

        if (!matrix) {
          return std::unexpected(matrix.error());
        }

        transform = *matrix;
        geometry = std::move(mesh->data);
        warning = std::move(mesh->warnings);
        material.model = Graphics::SurfaceModel::MetallicRoughness;
        material.baseColor = {.65F, .65F, .65F, 1};
        material.roughness = .65F;
        eye = {2, 1.4F, 3};
      }

      Graphics::Lighting lighting;
      lighting.viewPosition = eye;
      lighting.viewDirection = Math::normalized(eye * -1).value();
      lighting.lights = {{.direction = {-1, -1, -1}, .intensity = 3},
                         {.direction = {1, .3F, -.5F}, .intensity = 1},
                         {.direction = {0, 1, .3F}, .intensity = .5F}};
      auto configured = m_impl->renderer.setLighting(lighting);

      if (!configured) {
        return std::unexpected(configured.error());
      }

      configured = m_impl->renderer.setPostProcess({0, *kind == AssetPreviewKind::Image
                                                           ? Graphics::ToneMap::LinearClamp
                                                           : Graphics::ToneMap::AcesFitted});

      if (!configured) {
        return std::unexpected(configured.error());
      }

      auto mesh = m_impl->renderer.upload(geometry);

      if (!mesh) {
        return std::unexpected(mesh.error());
      }

      const auto camera
          = Math::multiply(Math::orthographic(-1.1F, 1.1F, -1.1F, 1.1F, .05F, 10).value(),
                           Math::lookAt(eye, {}).value())
                .value();
      const std::array draws{Graphics::MeshDraw{&*mesh, transform, {1, 1, 1, 1}, material}};
      auto image = m_impl->renderer.renderMeshes(draws, camera, size, size, {.025F, .03F, .04F, 1});

      if (!image) {
        return std::unexpected(image.error());
      }

      return AssetPreviewImage{std::move(*image), std::move(warning)};
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Preview '" + std::string(path) + "': " + error.what());
    }
  }

  auto AssetPreview::clearCache() -> Core::Result<void> {
    return m_impl ? m_impl->materials->clearCache()
                  : Core::fail(Core::ErrorCode::InvalidState, "Asset preview was moved from");
  }

  auto AssetPreview::stats() const -> Core::Result<Graphics::ResourceStats> {
    return m_impl ? m_impl->renderer.resourceStats()
                  : Core::fail(Core::ErrorCode::InvalidState, "Asset preview was moved from");
  }
}  // namespace Dot::Editor
