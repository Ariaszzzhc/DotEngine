module;

#include <array>
#include <cmath>
#include <expected>
#include <format>
#include <stdexcept>

#include "../Serialization/Json.hpp"

module Dot.Assets.Material;

import Dot.Core.Result;
import Dot.Core.Files;
import Dot.Core.Paths;
import Dot.Math;
import Dot.Graphics.Renderer;

namespace Dot::Assets {
  namespace {
    namespace Codec = Serialization::Detail;
    using Json = Codec::Json;
    constexpr std::size_t maxBytes = 1024 * 1024;

    auto validPath(std::string_view path, bool material) -> bool {
      return Core::isPortableRelativePath(path)
             && (material
                     ? path.ends_with(".dotmaterial")
                     : path.ends_with(".png") || path.ends_with(".jpg") || path.ends_with(".jpeg"));
    }

    auto texture(const Json& value) -> std::optional<TextureSource> {
      if (value.is_null()) {
        return std::nullopt;
      }

      Codec::requireFields(value, {"path", "sampling"});
      const auto& sampling = value["sampling"];
      Codec::requireFields(
          sampling, {"colorSpace", "filter", "addressU", "addressV", "mipmaps", "maxAnisotropy"});
      TextureSource result;
      result.path = value["path"].get<std::string>();
      const auto color = sampling["colorSpace"].get<std::string>();
      const auto filter = sampling["filter"].get<std::string>();

      if ((color != "linear" && color != "srgb") || (filter != "nearest" && filter != "linear")) {
        throw std::invalid_argument("Unknown texture color space/filter");
      }

      result.sampling.colorSpace
          = color == "linear" ? Graphics::ColorSpace::Linear : Graphics::ColorSpace::Srgb;
      result.sampling.filter
          = filter == "linear" ? Graphics::TextureFilter::Linear : Graphics::TextureFilter::Nearest;
      const auto address = [](const Json& field) {
        const auto name = field.get<std::string>();

        if (name == "repeat") {
          return Graphics::TextureAddress::Repeat;
        }

        if (name == "clamp") {
          return Graphics::TextureAddress::Clamp;
        }

        if (name == "mirror") {
          return Graphics::TextureAddress::Mirror;
        }

        throw std::invalid_argument("Unknown texture address mode");
      };
      result.sampling.addressU = address(sampling["addressU"]);
      result.sampling.addressV = address(sampling["addressV"]);
      result.sampling.mipmaps = sampling["mipmaps"].get<bool>();
      const auto anisotropy = sampling["maxAnisotropy"];

      if (!anisotropy.is_number_integer() || anisotropy < 1 || anisotropy > 16) {
        throw std::invalid_argument("Texture anisotropy must be an integer in [1,16]");
      }

      result.sampling.maxAnisotropy = anisotropy.get<std::uint16_t>();
      return result;
    }

    auto textureJson(const std::optional<TextureSource>& source) -> Json {
      if (!source) {
        return nullptr;
      }

      const auto& options = source->sampling;
      const auto address = [](Graphics::TextureAddress value) {
        return value == Graphics::TextureAddress::Clamp    ? "clamp"
               : value == Graphics::TextureAddress::Repeat ? "repeat"
                                                           : "mirror";
      };
      return {
          {"path", source->path},
          {"sampling",
           {{"colorSpace", options.colorSpace == Graphics::ColorSpace::Linear ? "linear" : "srgb"},
            {"filter", options.filter == Graphics::TextureFilter::Linear ? "linear" : "nearest"},
            {"addressU", address(options.addressU)},
            {"addressV", address(options.addressV)},
            {"mipmaps", options.mipmaps},
            {"maxAnisotropy", options.maxAnisotropy}}}};
    }

    auto checkedFile(const Core::FileSystem& files, const std::filesystem::path& relative,
                     bool allowMissing) -> Core::Result<void> {
      auto current = files.root();

      for (const auto& part : relative) {
        current /= part;
        std::error_code error;
        const auto status = std::filesystem::symlink_status(current, error);
        const bool final = current == files.root() / relative;

        if (status.type() == std::filesystem::file_type::not_found) {
          if (final && allowMissing) {
            return {};
          }

          return Core::fail(Core::ErrorCode::NotFound, "Material file/parent is missing");
        }

        if (error || std::filesystem::is_symlink(status)
            || (final ? !std::filesystem::is_regular_file(status)
                      : !std::filesystem::is_directory(status))) {
          return Core::fail(Core::ErrorCode::Io,
                            "Material path is inaccessible, a symlink or a special file");
        }
      }

      return {};
    }

    auto checkedPath(const std::filesystem::path& path) -> Core::Result<void> {
      const auto bytes = path.generic_u8string();
      const std::string text(bytes.begin(), bytes.end());

      if (!validPath(text, true)) {
        return Core::fail(Core::ErrorCode::InvalidArgument,
                          "Material requires a portable relative .dotmaterial path");
      }

      return {};
    }
  }  // namespace

  auto validateMaterial(const MaterialDescription& material) -> Core::Result<void> {
    const auto color = material.baseColor;
    const auto emission = material.emissive;
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };

    if ((material.model != Graphics::SurfaceModel::Unlit
         && material.model != Graphics::SurfaceModel::MetallicRoughness)
        || !Math::isFinite(color) || !unit(color.x) || !unit(color.y) || !unit(color.z)
        || !unit(color.w) || !Math::isFinite(emission) || emission.x < 0 || emission.x > 65503
        || emission.y < 0 || emission.y > 65503 || emission.z < 0 || emission.z > 65503
        || !unit(material.metallic) || !unit(material.roughness)
        || !std::isfinite(material.normalScale) || material.normalScale < 0
        || material.normalScale > 8) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Invalid material factors/model");
    }

    const std::array slots{&material.baseColorTexture, &material.metallicRoughnessTexture,
                           &material.normalTexture, &material.emissiveTexture};

    for (std::size_t index = 0; index < slots.size(); ++index) {
      if (!*slots[index]) {
        continue;
      }

      const auto& source = **slots[index];
      auto checked = Graphics::validateTextureOptions(source.sampling);

      if (!checked || !validPath(source.path, false)
          || ((index == 1 || index == 2)
              && (source.sampling.colorSpace != Graphics::ColorSpace::Linear
                  || material.model == Graphics::SurfaceModel::Unlit))) {
        return Core::fail(
            Core::ErrorCode::InvalidArgument,
            std::format("Invalid material texture slot {} ('{}')", index, source.path));
      }
    }

    return {};
  }

  auto decodeMaterial(std::string_view source) -> Core::Result<MaterialDescription> {
    if (source.size() > maxBytes) {
      return Core::fail(Core::ErrorCode::InvalidArgument, "Material exceeds 1 MiB");
    }

    try {
      const auto root = Codec::parseJson(source, 16);
      Codec::requireFields(root, {"model", "baseColor", "emissive", "metallic", "roughness",
                                  "normalScale", "doubleSided", "baseColorTexture",
                                  "metallicRoughnessTexture", "normalTexture", "emissiveTexture"});
      MaterialDescription result;
      const auto model = root["model"].get<std::string>();

      if (model != "unlit" && model != "metallicRoughness") {
        throw std::invalid_argument("Unknown material surface model");
      }

      result.model = model == "unlit" ? Graphics::SurfaceModel::Unlit
                                      : Graphics::SurfaceModel::MetallicRoughness;
      const auto base = Codec::numbers<4>(root["baseColor"]);
      const auto emission = Codec::numbers<3>(root["emissive"]);
      result.baseColor = {base[0], base[1], base[2], base[3]};
      result.emissive = {emission[0], emission[1], emission[2]};
      result.metallic = Codec::number(root["metallic"]);
      result.roughness = Codec::number(root["roughness"]);
      result.normalScale = Codec::number(root["normalScale"]);
      result.doubleSided = root["doubleSided"].get<bool>();
      result.baseColorTexture = texture(root["baseColorTexture"]);
      result.metallicRoughnessTexture = texture(root["metallicRoughnessTexture"]);
      result.normalTexture = texture(root["normalTexture"]);
      result.emissiveTexture = texture(root["emissiveTexture"]);
      auto checked = validateMaterial(result);
      return checked ? Core::Result<MaterialDescription>{std::move(result)}
                     : std::unexpected(checked.error());
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Decode material: " + std::string(error.what()));
    }
  }

  auto encodeMaterial(const MaterialDescription& material) -> Core::Result<std::string> {
    auto checked = validateMaterial(material);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    try {
      const auto c = material.baseColor;
      const auto e = material.emissive;
      Json root{{"model",
                 material.model == Graphics::SurfaceModel::Unlit ? "unlit" : "metallicRoughness"},
                {"baseColor", {c.x, c.y, c.z, c.w}},
                {"emissive", {e.x, e.y, e.z}},
                {"metallic", material.metallic},
                {"roughness", material.roughness},
                {"normalScale", material.normalScale},
                {"doubleSided", material.doubleSided},
                {"baseColorTexture", textureJson(material.baseColorTexture)},
                {"metallicRoughnessTexture", textureJson(material.metallicRoughnessTexture)},
                {"normalTexture", textureJson(material.normalTexture)},
                {"emissiveTexture", textureJson(material.emissiveTexture)}};
      return root.dump(2) + "\n";
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External, "Encode material: " + std::string(error.what()));
    }
  }

  auto loadMaterial(const Core::FileSystem& files, const std::filesystem::path& relative)
      -> Core::Result<MaterialDescription> {
    auto checked = checkedPath(relative);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    checked = checkedFile(files, relative, false);

    if (!checked) {
      return std::unexpected(checked.error());
    }

    auto source = files.readText(relative, maxBytes);
    auto result = source ? decodeMaterial(*source)
                         : Core::Result<MaterialDescription>{std::unexpected(source.error())};

    if (!result) {
      return Core::fail(result.error().code, std::format("Material '{}': {}", relative.string(),
                                                         result.error().message));
    }

    return result;
  }

  auto saveMaterial(const Core::FileSystem& files, const std::filesystem::path& relative,
                    const MaterialDescription& material) -> Core::Result<void> {
    auto checked = checkedPath(relative);

    if (!checked) {
      return checked;
    }

    checked = checkedFile(files, relative, true);

    if (!checked) {
      return checked;
    }

    auto source = encodeMaterial(material);
    return source ? files.writeTextAtomic(relative, *source) : std::unexpected(source.error());
  }
}  // namespace Dot::Assets
