module;

#include <cgltf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../Serialization/Json.hpp"

module Dot.Assets.Model;

import Dot.Assets.Image;
import Dot.Graphics.Renderer;
import Dot.Assets.Mesh;
import Dot.Core.Paths;
import Dot.Core.Text;
import Dot.World.Scene;
import Dot.World.SceneFiles;

namespace Dot::Assets {
  namespace {
    using Json = Serialization::Detail::Json;
    constexpr std::size_t fileLimit = 64 * 1024 * 1024;
    constexpr std::size_t inputLimit = 128 * 1024 * 1024;

    struct Failure {
      Core::Error error;
    };

    [[noreturn]] auto fail(std::string message,
                           Core::ErrorCode code = Core::ErrorCode::InvalidArgument) -> void {
      throw Failure{Core::fail(code, std::move(message)).error()};
    }

    auto require(bool condition, std::string message) -> void {
      if (!condition) {
        fail(std::move(message));
      }
    }

    template <class T>
    auto take(Core::Result<T> result) -> T {
      if (!result) {
        throw Failure{result.error()};
      }

      if constexpr (!std::is_void_v<T>) {
        return std::move(*result);
      }
    }

    struct Budget {
      std::size_t live{};
    };

    struct alignas(std::max_align_t) Allocation {
      std::size_t bytes;
    };

    auto allocate(void* user, cgltf_size size) -> void* {
      auto& budget = *static_cast<Budget*>(user);

      if (size > inputLimit - budget.live) {
        return nullptr;
      }

      auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + size));

      if (!block) {
        return nullptr;
      }

      block->bytes = size;
      budget.live += size;
      return block + 1;
    }

    auto release(void* user, void* pointer) -> void {
      if (pointer) {
        auto* block = static_cast<Allocation*>(pointer) - 1;
        static_cast<Budget*>(user)->live -= block->bytes;
        std::free(block);
      }
    }

    auto number(const std::byte* bytes, std::size_t count) -> std::uint32_t {
      std::uint32_t result{};

      for (std::size_t i = 0; i < count; ++i) {
        result |= std::to_integer<std::uint32_t>(bytes[i]) << (i * 8);
      }

      return result;
    }

    auto base64(std::string_view text) -> std::vector<std::byte> {
      require(!text.empty() && text.size() % 4 == 0 && text.size() / 4 <= fileLimit / 3 + 1,
              "Invalid/oversized base64 resource");
      constexpr std::string_view alphabet
          = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      std::vector<std::byte> result;
      result.reserve(text.size() / 4 * 3);

      for (std::size_t i = 0; i < text.size(); i += 4) {
        std::uint32_t bits{};
        unsigned padding{};

        for (std::size_t j = 0; j < 4; ++j) {
          if (text[i + j] == '=') {
            require(i + 4 == text.size() && j >= 2, "Invalid base64 padding");
            ++padding;
            bits <<= 6;
          } else {
            const auto value = alphabet.find(text[i + j]);
            require(value != std::string_view::npos && padding == 0, "Invalid base64 character");
            bits = (bits << 6) | static_cast<std::uint32_t>(value);
          }
        }

        require(padding <= 2 && (padding == 0 || (bits & (padding == 1 ? 255U : 65535U)) == 0),
                "Noncanonical base64 padding bits");
        result.push_back(static_cast<std::byte>(bits >> 16));

        if (padding < 2) {
          result.push_back(static_cast<std::byte>(bits >> 8));
        }

        if (padding == 0) {
          result.push_back(static_cast<std::byte>(bits));
        }
      }

      require(result.size() <= fileLimit, "Decoded resource exceeds file limit");
      return result;
    }

    auto uriPath(std::string_view uri) -> std::filesystem::path {
      std::string decoded;
      const auto hex = [](char c) -> int {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
      };

      for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] != '%') {
          decoded += uri[i];
        } else {
          require(i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0,
                  "Malformed URI escape");
          decoded += static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
          i += 2;
        }
      }

      require(Core::isPortableRelativePath(decoded),
              "Model URI must be portable and root-relative: " + decoded);
      return std::filesystem::path(std::u8string(decoded.begin(), decoded.end()));
    }

    auto extensions(const Json& value) -> void {
      if (value.is_object()) {
        if (value.contains("extensions")) {
          require(value["extensions"].is_object(), "Invalid model extensions");

          for (const auto& [name, unused] : value["extensions"].items()) {
            static_cast<void>(unused);

            if (name != "KHR_materials_unlit" && name != "KHR_materials_emissive_strength"
                && name != "KHR_mesh_quantization") {
              fail("Unsupported model extension: " + name, Core::ErrorCode::Unavailable);
            }
          }
        }

        if (value.contains("matrix")) {
          require(!value.contains("translation") && !value.contains("rotation")
                      && !value.contains("scale"),
                  "Node cannot contain both matrix and TRS");
        }

        if (value.contains("alphaMode")) {
          require(value["alphaMode"] == "OPAQUE" || value["alphaMode"] == "MASK"
                      || value["alphaMode"] == "BLEND",
                  "Invalid alpha mode");
        }

        constexpr std::array integers{
            "index",  "texCoord",   "byteOffset",    "byteLength", "byteStride", "count",
            "buffer", "bufferView", "componentType", "mesh",       "material",   "scene",
            "source", "sampler",    "indices",       "mode",       "skin",       "camera"};
        constexpr std::array scalars{"metallicFactor", "roughnessFactor", "strength",
                                     "emissiveStrength", "alphaCutoff"};
        constexpr std::array vectors{"translation",    "rotation", "matrix", "baseColorFactor",
                                     "emissiveFactor", "min",      "max"};
        constexpr std::array textures{"baseColorTexture", "metallicRoughnessTexture",
                                      "normalTexture", "emissiveTexture", "occlusionTexture"};

        for (const auto& [key, entry] : value.items()) {
          if (key == "extras") {
            continue;
          }

          if (std::ranges::find(integers, key) != integers.end()) {
            // Sparse indices is an object; other indices fields are references.
            // 稀疏 indices 是对象，其余 indices 字段是索引引用。
            require((key == "indices" && entry.is_object()) || entry.is_number_unsigned(),
                    "Model integer field: " + key);
          }

          if (std::ranges::find(scalars, key) != scalars.end()) {
            require(entry.is_number() && std::isfinite(entry.get<double>()),
                    "Model numeric field: " + key);
          }

          if (std::ranges::find(vectors, key) != vectors.end()
              || (key == "scale" && entry.is_array())) {
            require(entry.is_array()
                        && std::ranges::all_of(entry,
                                               [](const Json& n) {
                                                 return n.is_number()
                                                        && std::isfinite(n.get<double>());
                                               }),
                    "Model numeric vector: " + key);
          }

          if (key == "scale" && !entry.is_array()) {
            require(entry.is_number() && std::isfinite(entry.get<double>()),
                    "Invalid texture scale");
          }

          if (key == "normalized" || key == "doubleSided") {
            require(entry.is_boolean(), "Model boolean field: " + key);
          }

          if (std::ranges::find(textures, key) != textures.end()) {
            require(entry.is_object() && entry.contains("index"),
                    "Texture requires an index: " + key);
          }

          if (key == "attributes") {
            require(entry.is_object()
                        && std::ranges::all_of(
                            entry, [](const Json& index) { return index.is_number_unsigned(); }),
                    "Invalid vertex attribute indices");
          }

          extensions(entry);
        }
      } else if (value.is_string()) {
        const auto text = value.get<std::string>();
        require(Core::isValidUtf8(text) && text.find('\0') == std::string::npos,
                "Model strings require UTF-8 without NUL");
      } else if (value.is_array()) {
        for (const auto& entry : value) {
          extensions(entry);
        }
      }
    }

    auto accessor(const cgltf_accessor* a, cgltf_type type, std::size_t count)
        -> std::vector<float> {
      require(a && a->type == type && a->count == count, "Invalid accessor type/count");
      const auto components = cgltf_num_components(type);
      std::vector<float> values(count * components);
      require(cgltf_accessor_unpack_floats(a, values.data(), values.size()) == values.size(),
              "Cannot unpack accessor");
      require(std::ranges::all_of(values, [](float f) { return std::isfinite(f); }),
              "Accessor contains nonfinite values");
      return values;
    }

    auto validateData(cgltf_data& data) -> void {
      require(data.nodes_count <= 10000 && data.accessors_count <= 8192
                  && data.buffer_views_count <= 8192 && data.materials_count <= 4096
                  && data.images_count <= 1024 && data.meshes_count <= 4096,
              "Model object capacity exceeded");

      for (std::size_t i = 0; i < data.buffer_views_count; ++i) {
        const auto& v = data.buffer_views[i];
        require(v.buffer && v.size > 0 && v.offset <= v.buffer->size
                    && v.size <= v.buffer->size - v.offset
                    && (v.stride == 0 || (v.stride >= 4 && v.stride <= 252 && v.stride % 4 == 0))
                    && !v.has_meshopt_compression,
                "Invalid/unsupported buffer view");
      }

      for (std::size_t i = 0; i < data.accessors_count; ++i) {
        const auto& a = data.accessors[i];
        const auto element = cgltf_calc_size(a.type, a.component_type);
        const auto component = cgltf_component_size(a.component_type);
        require(a.count > 0 && a.count <= 3000000 && element > 0 && component > 0,
                "Invalid accessor capacity/type");

        if (a.buffer_view) {
          const auto& v = *a.buffer_view;
          require(a.stride >= element && a.offset <= v.size && element <= v.size - a.offset
                      && a.count - 1 <= (v.size - a.offset - element) / a.stride
                      && a.offset % component == 0 && v.offset % component == 0,
                  "Accessor range/alignment exceeds buffer view");
        } else {
          require(a.offset == 0, "Accessor without a buffer view cannot have an offset");
        }

        if (a.is_sparse) {
          const auto& s = a.sparse;
          const auto size = cgltf_component_size(s.indices_component_type);
          require(s.count > 0 && s.count <= a.count && s.indices_buffer_view && s.values_buffer_view
                      && (s.indices_component_type == cgltf_component_type_r_8u
                          || s.indices_component_type == cgltf_component_type_r_16u
                          || s.indices_component_type == cgltf_component_type_r_32u),
                  "Invalid sparse accessor");
          const auto& iv = *s.indices_buffer_view;
          const auto& vv = *s.values_buffer_view;
          require(iv.stride == 0 && vv.stride == 0 && s.indices_byte_offset <= iv.size
                      && s.count <= (iv.size - s.indices_byte_offset) / size
                      && s.values_byte_offset <= vv.size
                      && s.count <= (vv.size - s.values_byte_offset) / element
                      && (iv.offset + s.indices_byte_offset) % size == 0
                      && (vv.offset + s.values_byte_offset) % component == 0,
                  "Sparse accessor range/alignment");
          const auto* bytes
              = static_cast<const std::byte*>(iv.buffer->data) + iv.offset + s.indices_byte_offset;
          std::uint32_t previous{};

          for (std::size_t j = 0; j < s.count; ++j) {
            const auto index = number(bytes + j * size, size);
            require(index < a.count && (j == 0 || index > previous),
                    "Sparse indices must increase within accessor");
            previous = index;
          }
        }
      }

      for (std::size_t i = 0; i < data.nodes_count; ++i) {
        const auto* node = &data.nodes[i];
        std::size_t depth{};

        while (node) {
          require(++depth <= 256, "Model node hierarchy is cyclic/too deep");
          node = node->parent;
        }
      }

      require(cgltf_validate(&data) == cgltf_result_success,
              "glTF semantic/buffer validation failed");
    }
  }  // namespace

  auto loadModel(const Core::FileSystem& files, const std::filesystem::path& source,
                 std::string_view prefix, std::optional<std::uint32_t> scene)
      -> Core::Result<ModelAsset> {
    try {
      require(Core::isPortableRelativePath(prefix) && prefix.starts_with("assets/"),
              "Model prefix must be inside assets");
      const auto resolved = take(files.resolve(source));
      const auto rooted = take(Core::FileSystem::open(resolved.parent_path()));
      auto bytes = take(rooted.readBytes(resolved.filename(), fileLimit));
      require(!bytes.empty(), "Empty model source");
      std::string_view json(reinterpret_cast<const char*>(bytes.data()), bytes.size());

      if (bytes.size() >= 4 && number(bytes.data(), 4) == 0x46546c67U) {
        require(bytes.size() >= 20 && number(bytes.data() + 4, 4) == 2
                    && number(bytes.data() + 8, 4) == bytes.size()
                    && number(bytes.data() + 16, 4) == 0x4e4f534aU,
                "Invalid GLB header/JSON chunk");
        const auto size = number(bytes.data() + 12, 4);
        require(size > 0 && size % 4 == 0 && size <= bytes.size() - 20, "Invalid GLB JSON length");
        json = {reinterpret_cast<const char*>(bytes.data() + 20), size};
      }

      require(Core::isValidUtf8(json) && json.find('\0') == std::string_view::npos,
              "Invalid glTF JSON UTF-8/NUL");
      const auto metadata = Serialization::Detail::parseJson(json);
      require(metadata.is_object() && metadata.contains("asset")
                  && metadata["asset"].value("version", "") == "2.0",
              "Model requires glTF 2.0");
      extensions(metadata);
      const bool quantized
          = metadata.contains("extensionsRequired")
            && std::ranges::find(metadata["extensionsRequired"], "KHR_mesh_quantization")
                   != metadata["extensionsRequired"].end();
      Budget budget;
      cgltf_options options{};
      options.memory = {allocate, release, &budget};
      cgltf_data* raw{};
      const auto parsed = cgltf_parse(&options, bytes.data(), bytes.size(), &raw);
      std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, &cgltf_free);
      if (parsed == cgltf_result_out_of_memory) {
        fail("Parser allocation/128 MiB budget failed", Core::ErrorCode::External);
      }

      require(parsed == cgltf_result_success && data,
              "Cannot parse glTF/GLB: " + std::to_string(parsed));
      require(data->animations_count == 0 && data->skins_count == 0 && data->cameras_count == 0
                  && data->lights_count == 0,
              "Animated/skinned/camera/light model import is not supported");
      std::size_t inputBytes = bytes.size();
      std::map<std::string, std::vector<std::byte>> resources;
      const auto resource = [&](std::string_view uri) -> const std::vector<std::byte>& {
        auto found = resources.find(std::string(uri));

        if (found != resources.end()) {
          return found->second;
        }

        require(resources.size() < 4096, "Model resource count exceeded");
        std::vector<std::byte> content;

        if (uri.starts_with("data:")) {
          const auto comma = uri.find(',');
          require(comma != std::string_view::npos && uri.substr(0, comma).ends_with(";base64"),
                  "Only base64 data URIs are supported");
          content = base64(uri.substr(comma + 1));
        } else {
          content = take(rooted.readBytes(uriPath(uri), fileLimit));
        }

        require(content.size() <= inputLimit - inputBytes, "Model source byte budget exceeded");
        inputBytes += content.size();
        return resources.emplace(std::string(uri), std::move(content)).first->second;
      };
      require(
          data->buffers_count <= 4096 && data->textures_count <= 4096 && data->scenes_count <= 1024,
          "Model buffer/texture/scene capacity exceeded");
      std::vector<std::vector<std::byte>> buffers(data->buffers_count);

      for (std::size_t i = 0; i < data->buffers_count; ++i) {
        auto& b = data->buffers[i];
        require(b.size > 0 && b.size <= fileLimit, "Invalid buffer byte length");

        if (b.uri) {
          const auto& r = resource(b.uri);
          require(r.size() == b.size, "External/data buffer size differs from declared length");
          b.data = const_cast<std::byte*>(r.data());
        } else {
          require(i == 0 && data->bin && b.size <= data->bin_size && data->bin_size - b.size <= 3,
                  "Missing/mismatched GLB buffer");
          const auto* bin = static_cast<const std::byte*>(data->bin);
          buffers[i].assign(bin, bin + b.size);
          b.data = buffers[i].data();
        }

        b.data_free_method = cgltf_data_free_method_none;
      }

      for (std::size_t i = 0; i < data->extensions_required_count; ++i) {
        const std::string_view name(data->extensions_required[i]);
        require(name == "KHR_materials_unlit" || name == "KHR_materials_emissive_strength"
                    || name == "KHR_mesh_quantization",
                "Unsupported required extension: " + std::string(name));
      }

      validateData(*data);
      ModelAsset result;
      result.prefix = prefix;
      result.warnings.push_back(
          "Node names/hierarchy retained; mesh/material/image names, extras and unused UV/color "
          "channels are not persisted.");
      std::size_t pixelBytes{};
      std::vector<bool> normalImages;

      for (std::size_t i = 0; i < data->images_count; ++i) {
        const auto& image = data->images[i];
        std::span<const std::byte> encoded;

        if (image.uri) {
          encoded = resource(image.uri);
        } else {
          require(image.buffer_view && image.mime_type, "Image requires URI or MIME buffer view");
          const auto& view = *image.buffer_view;
          encoded = {static_cast<const std::byte*>(view.buffer->data) + view.offset, view.size};
        }

        if (image.mime_type) {
          require(std::string_view(image.mime_type) == "image/png"
                      || std::string_view(image.mime_type) == "image/jpeg",
                  "Model images must declare PNG/JPEG");
        }

        auto decoded = take(decodeImage(encoded));
        bool normal = true;

        for (std::size_t b = 2; b < decoded.pixels.size(); b += 4) {
          normal = normal && decoded.pixels[b] >= 128;
        }

        normalImages.push_back(normal);
        require(decoded.pixels.size() <= 256 * 1024 * 1024 - pixelBytes,
                "Model decoded image budget exceeded");
        pixelBytes += decoded.pixels.size();

        // Opaque glTF ignores alpha; RGB is preserved without color conversion.
        // 不透明 glTF 忽略 alpha，RGB 无色彩转换保留。
        for (std::size_t alpha = 3; alpha < decoded.pixels.size(); alpha += 4) {
          decoded.pixels[alpha] = 255;
        }

        result.images.push_back({take(encodeImage(decoded))});
      }

      const auto texture = [&](const cgltf_texture_view& view,
                               Graphics::ColorSpace color) -> std::optional<TextureSource> {
        if (!view.texture) {
          return {};
        }

        require(view.texcoord == 0 && !view.has_transform && view.texture->image
                    && !view.texture->has_basisu && !view.texture->has_webp,
                "Unsupported texture/UV transform");
        const auto image = cgltf_image_index(data.get(), view.texture->image);
        TextureSource slot{std::format("{}/images/{}.png", prefix, image)};
        slot.sampling.colorSpace = color;
        const auto* sampler = view.texture->sampler;

        if (sampler) {
          const auto mag = sampler->mag_filter ? static_cast<int>(sampler->mag_filter) : 9729;
          const auto min = sampler->min_filter ? static_cast<int>(sampler->min_filter) : 9987;
          require((mag == 9728 && (min == 9728 || min == 9984))
                      || (mag == 9729 && (min == 9729 || min == 9987)),
                  "Unsupported mixed texture filtering");
          slot.sampling.filter
              = mag == 9728 ? Graphics::TextureFilter::Nearest : Graphics::TextureFilter::Linear;
          slot.sampling.mipmaps = min >= 9984;
          const auto address = [](cgltf_wrap_mode mode) {
            require(mode == cgltf_wrap_mode_repeat || mode == cgltf_wrap_mode_clamp_to_edge
                        || mode == cgltf_wrap_mode_mirrored_repeat,
                    "Invalid texture wrap");
            return mode == cgltf_wrap_mode_repeat          ? Graphics::TextureAddress::Repeat
                   : mode == cgltf_wrap_mode_clamp_to_edge ? Graphics::TextureAddress::Clamp
                                                           : Graphics::TextureAddress::Mirror;
          };
          slot.sampling.addressU = address(sampler->wrap_s);
          slot.sampling.addressV = address(sampler->wrap_t);
        }

        return slot;
      };

      for (std::size_t i = 0; i <= data->materials_count; ++i) {
        MaterialDescription m;
        m.metallic = 1;
        m.roughness = 1;

        if (i < data->materials_count) {
          const auto& sourceMaterial = data->materials[i];
          require(sourceMaterial.alpha_mode == cgltf_alpha_mode_opaque,
                  "Transparent/masked material import is unsupported");
          const auto& pbr = sourceMaterial.pbr_metallic_roughness;
          for (const auto factor : sourceMaterial.pbr_metallic_roughness.base_color_factor) {
            require(std::isfinite(factor) && factor >= 0 && factor <= 1,
                    "Invalid base color factor");
          }

          for (const auto factor : sourceMaterial.emissive_factor) {
            require(std::isfinite(factor) && factor >= 0 && factor <= 1, "Invalid emissive factor");
          }

          m.model = sourceMaterial.unlit ? Graphics::SurfaceModel::Unlit
                                         : Graphics::SurfaceModel::MetallicRoughness;
          m.baseColor
              = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], 1};
          m.metallic = pbr.metallic_factor;
          m.roughness = pbr.roughness_factor;
          const auto strength = sourceMaterial.has_emissive_strength
                                    ? sourceMaterial.emissive_strength.emissive_strength
                                    : 1;
          m.emissive = {sourceMaterial.emissive_factor[0] * strength,
                        sourceMaterial.emissive_factor[1] * strength,
                        sourceMaterial.emissive_factor[2] * strength};
          m.normalScale = sourceMaterial.normal_texture.scale;
          m.occlusionStrength = sourceMaterial.occlusion_texture.scale;
          m.doubleSided = sourceMaterial.double_sided != 0;
          m.baseColorTexture = texture(pbr.base_color_texture, Graphics::ColorSpace::Srgb);
          m.metallicRoughnessTexture
              = texture(pbr.metallic_roughness_texture, Graphics::ColorSpace::Linear);
          m.normalTexture = texture(sourceMaterial.normal_texture, Graphics::ColorSpace::Linear);
          m.occlusionTexture
              = texture(sourceMaterial.occlusion_texture, Graphics::ColorSpace::Linear);
          m.emissiveTexture = texture(sourceMaterial.emissive_texture, Graphics::ColorSpace::Srgb);

          if (sourceMaterial.normal_texture.texture) {
            require(normalImages[cgltf_image_index(data.get(),
                                                   sourceMaterial.normal_texture.texture->image)],
                    "Normal image has unsupported nonpositive Z encoding");
          }
        }

        take(validateMaterial(m));
        result.materials.push_back(std::move(m));
      }

      std::vector<std::vector<std::uint32_t>> meshPrimitives(data->meshes_count);
      std::size_t vertices{}, indices{};

      for (std::size_t meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex) {
        const auto& mesh = data->meshes[meshIndex];

        for (std::size_t primitive = 0; primitive < mesh.primitives_count; ++primitive) {
          const auto& p = mesh.primitives[primitive];
          require(p.targets_count == 0 && p.mappings_count == 0 && !p.has_draco_mesh_compression,
                  "Morph/compressed/variant primitives are unsupported");
          require(p.type == cgltf_primitive_type_triangles
                      || p.type == cgltf_primitive_type_triangle_strip
                      || p.type == cgltf_primitive_type_triangle_fan,
                  "Only triangle model primitives are supported");
          const auto* position = cgltf_find_accessor(&p, cgltf_attribute_type_position, 0);
          require(position && position->count <= 1000000 - vertices,
                  "Model vertex budget exceeded");
          const auto count = position->count;
          const auto integer = [](cgltf_component_type c) {
            return c == cgltf_component_type_r_8 || c == cgltf_component_type_r_8u
                   || c == cgltf_component_type_r_16 || c == cgltf_component_type_r_16u;
          };
          require((position->component_type == cgltf_component_type_r_32f && !position->normalized)
                      || (quantized && integer(position->component_type)),
                  "Invalid POSITION component/quantization");
          const auto positions = accessor(position, cgltf_type_vec3, count);
          ModelPrimitive output;
          output.material = static_cast<std::uint32_t>(
              p.material ? cgltf_material_index(data.get(), p.material) : data->materials_count);
          output.mesh.vertices.resize(count);

          for (std::size_t j = 0; j < count; ++j) {
            output.mesh.vertices[j].position
                = {positions[j * 3], positions[j * 3 + 1], positions[j * 3 + 2]};
          }

          for (std::size_t attributeIndex = 0; attributeIndex < p.attributes_count;
               ++attributeIndex) {
            const auto& a = p.attributes[attributeIndex];

            if (a.type == cgltf_attribute_type_position
                || (a.index != 0
                    && (a.type == cgltf_attribute_type_texcoord
                        || a.type == cgltf_attribute_type_color))) {
              continue;
            }

            require(a.type == cgltf_attribute_type_normal || a.type == cgltf_attribute_type_tangent
                        || a.type == cgltf_attribute_type_texcoord
                        || a.type == cgltf_attribute_type_color,
                    "Unsupported vertex attribute: " + std::string(a.name ? a.name : "unknown"));
            const auto type = a.type == cgltf_attribute_type_color     ? a.data->type
                              : a.type == cgltf_attribute_type_normal  ? cgltf_type_vec3
                              : a.type == cgltf_attribute_type_tangent ? cgltf_type_vec4
                                                                       : cgltf_type_vec2;
            require(type == cgltf_type_vec2 || type == cgltf_type_vec3 || type == cgltf_type_vec4,
                    "Invalid attribute type");
            const auto c = a.data->component_type;
            const bool floating = c == cgltf_component_type_r_32f && !a.data->normalized;
            const bool signedNormalized
                = quantized && a.data->normalized
                  && (c == cgltf_component_type_r_8 || c == cgltf_component_type_r_16);
            const bool unsignedNormalized
                = a.data->normalized
                  && (c == cgltf_component_type_r_8u || c == cgltf_component_type_r_16u);
            require(floating
                        || ((a.type == cgltf_attribute_type_normal
                             || a.type == cgltf_attribute_type_tangent)
                                ? signedNormalized
                                : unsignedNormalized
                                      || (a.type == cgltf_attribute_type_texcoord && quantized
                                          && integer(c))),
                    "Invalid vertex attribute component/normalization");
            const auto values = accessor(a.data, type, count);
            const auto components = cgltf_num_components(type);

            for (std::size_t j = 0; j < count; ++j) {
              auto& v = output.mesh.vertices[j];
              const auto* f = values.data() + j * components;

              if (a.type == cgltf_attribute_type_normal) {
                const Math::Vec3 n{f[0], f[1], f[2]};
                require(std::abs(Math::length(n) - 1) <= (floating ? 1e-4 : .025),
                        "Authored normal must be unit");
                v.normal = take(Math::normalized(n));
              } else if (a.type == cgltf_attribute_type_tangent) {
                const Math::Vec3 tangent{f[0], f[1], f[2]};
                require(std::abs(Math::length(tangent) - 1) <= (floating ? 1e-4 : .025),
                        "Authored tangent must be unit");
                const auto t = take(Math::normalized(tangent));
                v.tangent = {t.x, t.y, t.z, f[3]};
              } else if (a.type == cgltf_attribute_type_texcoord) {
                v.texCoord = {f[0], f[1]};
              } else {
                require(type == cgltf_type_vec3 || type == cgltf_type_vec4,
                        "Invalid color dimensions");
                require(type != cgltf_type_vec4 || (f[3] >= 0 && f[3] <= 1), "Invalid COLOR alpha");
                v.color = {f[0], f[1], f[2], 1};
              }
            }
          }

          std::vector<std::uint32_t> sourceIndices;

          if (p.indices) {
            require(!p.indices->normalized
                        && (p.indices->component_type == cgltf_component_type_r_8u
                            || p.indices->component_type == cgltf_component_type_r_16u
                            || p.indices->component_type == cgltf_component_type_r_32u),
                    "Invalid triangle index component");
            const auto values = accessor(p.indices, cgltf_type_scalar, p.indices->count);

            for (const auto index : values) {
              require(index >= 0 && index < static_cast<float>(count) && std::floor(index) == index,
                      "Triangle index exceeds vertex range");
              sourceIndices.push_back(static_cast<std::uint32_t>(index));
            }
          } else {
            for (std::uint32_t j = 0; j < count; ++j) {
              sourceIndices.push_back(j);
            }
          }

          if (p.type == cgltf_primitive_type_triangles) {
            output.mesh.indices = std::move(sourceIndices);
          } else {
            require(
                sourceIndices.size() >= 3 && sourceIndices.size() - 2 <= (3000000 - indices) / 3,
                "Triangle expansion budget exceeded");

            for (std::size_t j = 2; j < sourceIndices.size(); ++j) {
              const auto a = sourceIndices[p.type == cgltf_primitive_type_triangle_fan ? 0 : j - 2];
              const auto b = sourceIndices[j - 1];
              const auto c = sourceIndices[j];

              if (a != b && b != c && a != c) {
                output.mesh.indices.insert(output.mesh.indices.end(),
                                           p.type == cgltf_primitive_type_triangle_strip && j % 2
                                               ? std::initializer_list<std::uint32_t>{b, a, c}
                                               : std::initializer_list<std::uint32_t>{a, b, c});
              }
            }
          }

          require(!output.mesh.indices.empty() && output.mesh.indices.size() <= 3000000 - indices,
                  "Model index budget exceeded");

          if (!cgltf_find_accessor(&p, cgltf_attribute_type_normal, 0)) {
            require(!cgltf_find_accessor(&p, cgltf_attribute_type_tangent, 0),
                    "Tangents require normals");
            output.mesh = take(Graphics::withFlatNormals(output.mesh));
          }

          require(output.mesh.vertices.size() <= 1000000 - vertices,
                  "Expanded model vertex budget exceeded");
          const auto surface = take(Graphics::meshSurface(output.mesh));
          const auto& material = result.materials[output.material];
          require(material.model == Graphics::SurfaceModel::Unlit || surface.hasNormals,
                  "Physical model requires normals");
          require(!material.normalTexture || surface.hasNormalMapFrame,
                  "Normal-mapped model has no usable frame");
          vertices += output.mesh.vertices.size();
          indices += output.mesh.indices.size();
          meshPrimitives[meshIndex].push_back(static_cast<std::uint32_t>(result.primitives.size()));
          result.primitives.push_back(std::move(output));
        }
      }

      require(!result.primitives.empty(), "Model has no triangle geometry");
      const auto selected = scene         ? static_cast<std::size_t>(*scene)
                            : data->scene ? cgltf_scene_index(data.get(), data->scene)
                                          : 0;
      require(!scene || selected < data->scenes_count, "Requested model scene does not exist");
      result.scene = static_cast<std::uint32_t>(selected);
      std::set<const cgltf_node*> visited;
      std::size_t entities{};
      const auto visit
          = [&](auto&& self, const cgltf_node* node, std::optional<std::uint32_t> parent) -> void {
        require(visited.insert(node).second && ++entities <= 10000,
                "Repeated/cyclic node or entity capacity exceeded");
        require(!node->skin && !node->camera && !node->light && !node->has_mesh_gpu_instancing,
                "Unsupported model node");
        ModelNode output;
        output.name = node->name && *node->name
                          ? node->name
                          : std::format("Node {}", cgltf_node_index(data.get(), node));
        require(output.name.size() <= 256 && Core::isValidUtf8(output.name),
                "Invalid model node name");
        Math::Mat4 matrix;
        cgltf_node_transform_local(node, matrix.data.data());
        output.transform = take(Math::decomposeTrs(matrix));
        output.parent = parent;

        if (node->mesh) {
          output.primitives = meshPrimitives[cgltf_mesh_index(data.get(), node->mesh)];
          entities += output.primitives.size();
          require(entities <= 10000, "Model entity capacity exceeded");
        }

        const auto id = static_cast<std::uint32_t>(result.nodes.size());
        result.nodes.push_back(std::move(output));

        for (std::size_t child = 0; child < node->children_count; ++child) {
          self(self, node->children[child], id);
        }
      };

      if (data->scenes_count) {
        const auto& selectedScene = data->scenes[selected];

        for (std::size_t i = 0; i < selectedScene.nodes_count; ++i) {
          require(!selectedScene.nodes[i]->parent, "Scene root node has a parent");
          visit(visit, selectedScene.nodes[i], {});
        }
      } else {
        for (std::size_t i = 0; i < data->nodes_count; ++i) {
          if (!data->nodes[i].parent) {
            visit(visit, &data->nodes[i], {});
          }
        }
      }

      require(!result.nodes.empty(), "Selected model scene is empty");
      return result;
    } catch (const Failure& error) {
      return Core::fail(error.error.code, "Import model: " + error.error.message);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::InvalidArgument,
                        "Import model: " + std::string(error.what()));
    }
  }

  auto writeModel(const Core::FileSystem& staging, const ModelAsset& model) -> Core::Result<void> {
    try {
      require(Core::isPortableRelativePath(model.prefix) && model.prefix.starts_with("assets/")
                  && !model.nodes.empty() && model.nodes.size() <= 10000
                  && !model.primitives.empty() && model.materials.size() <= 4097
                  && model.images.size() <= 1024,
              "Invalid prepared model capacities/prefix");
      std::size_t vertices{}, indices{}, pixels{};
      std::set<std::string> imagePaths;

      for (std::size_t i = 0; i < model.images.size(); ++i) {
        const auto& image = model.images[i].png;
        require(image.size() <= fileLimit && image.size() >= 8
                    && number(image.data(), 4) == 0x474e5089U,
                "Prepared images must be bounded PNG");
        auto decoded = take(decodeImage(image));
        require(decoded.pixels.size() <= 256 * 1024 * 1024 - pixels,
                "Prepared image budget exceeded");
        pixels += decoded.pixels.size();
        imagePaths.insert(std::format("{}/images/{}.png", model.prefix, i));
      }

      for (const auto& material : model.materials) {
        take(validateMaterial(material));
        for (const auto* texture :
             {&material.baseColorTexture, &material.metallicRoughnessTexture,
              &material.normalTexture, &material.emissiveTexture, &material.occlusionTexture}) {
          require(!*texture || imagePaths.contains((*texture)->path),
                  "Prepared material references foreign/missing image");
        }
      }

      for (const auto& primitive : model.primitives) {
        require(primitive.material < model.materials.size()
                    && primitive.mesh.vertices.size() <= 1000000 - vertices
                    && primitive.mesh.indices.size() <= 3000000 - indices,
                "Invalid prepared primitive/budget");
        take(Graphics::validateMesh(primitive.mesh));
        vertices += primitive.mesh.vertices.size();
        indices += primitive.mesh.indices.size();
      }

      require(std::filesystem::is_empty(staging.root()), "Model staging must be empty");
      take(staging.createDirectories("meshes"));
      take(staging.createDirectories("materials"));
      take(staging.createDirectories("images"));

      for (std::size_t i = 0; i < model.images.size(); ++i) {
        take(staging.writeAtomic(std::format("images/{}.png", i), model.images[i].png));
      }

      for (std::size_t i = 0; i < model.materials.size(); ++i) {
        take(saveMaterial(staging, std::format("materials/{}.dotmaterial", i), model.materials[i]));
      }

      for (std::size_t i = 0; i < model.primitives.size(); ++i) {
        take(saveMesh(staging, std::format("meshes/{}.dotmesh", i), model.primitives[i].mesh));
      }

      auto scene = take(World::Scene::create("Imported model"));
      std::vector<World::EntityId> nodes;
      std::vector<World::ParentUpdate> parents;

      for (const auto& node : model.nodes) {
        auto entity = take(scene.createEntity(node.name));
        nodes.push_back(entity);
        const auto& t = node.transform;
        take(scene.setTransform(entity, {t.position, t.rotation, t.scale}));

        if (node.parent) {
          require(*node.parent < nodes.size() - 1, "Invalid prepared node parent");
          parents.push_back({entity, nodes[*node.parent]});
        }

        for (const auto primitive : node.primitives) {
          require(primitive < model.primitives.size(), "Invalid prepared primitive index");
          auto part = take(scene.createEntity(std::format("Primitive {}", primitive)));
          take(scene.setMeshRenderer(
              part, World::MeshRenderer{
                        .kind = World::MeshKind::Asset,
                        .asset = std::format("{}/meshes/{}.dotmesh", model.prefix, primitive)}));
          take(scene.setMaterial(
              part, World::MaterialBinding{std::format("{}/materials/{}.dotmaterial", model.prefix,
                                                       model.primitives[primitive].material)}));
          parents.push_back({part, entity});
        }
      }

      take(scene.setParents(parents));
      take(World::saveScene(staging, "model.dotscene", scene));
      return {};
    } catch (const Failure& error) {
      return Core::fail(error.error.code, "Write model bundle: " + error.error.message);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Write model bundle: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Assets
