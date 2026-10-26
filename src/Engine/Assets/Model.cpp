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
#include <tuple>
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
import Dot.Animation.Files;
import Dot.Animation.SkinFiles;
import Dot.Animation.SceneSkin;
import Dot.Graphics.Skinning;

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

        constexpr std::array integers{"index",         "texCoord",
                                      "byteOffset",    "byteLength",
                                      "byteStride",    "count",
                                      "buffer",        "bufferView",
                                      "componentType", "mesh",
                                      "material",      "scene",
                                      "source",        "sampler",
                                      "indices",       "mode",
                                      "skin",          "camera",
                                      "input",         "output",
                                      "node",          "inverseBindMatrices",
                                      "skeleton"};
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

          if (key == "interpolation") {
            require(entry == "STEP" || entry == "LINEAR" || entry == "CUBICSPLINE",
                    "Unsupported animation interpolation");
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

          if (key == "joints") {
            require(entry.is_array() && !entry.empty() && entry.size() <= 1024
                        && std::ranges::all_of(
                            entry, [](const Json& id) { return id.is_number_unsigned(); }),
                    "Invalid skin joint references");
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

    auto influences(const cgltf_primitive& primitive, std::size_t count,
                    std::vector<std::string>& warnings) -> std::vector<Animation::SkinInfluence> {
      std::array<const cgltf_accessor*, 2> joints{};
      std::array<const cgltf_accessor*, 2> weights{};

      for (std::size_t i = 0; i < primitive.attributes_count; ++i) {
        const auto& attribute = primitive.attributes[i];

        if (attribute.type != cgltf_attribute_type_joints
            && attribute.type != cgltf_attribute_type_weights) {
          continue;
        }

        require(attribute.index >= 0 && attribute.index < 2,
                "Skin supports all four/eight influences; extra attribute sets are unsupported");
        auto& slot = attribute.type == cgltf_attribute_type_joints
                         ? joints[static_cast<std::size_t>(attribute.index)]
                         : weights[static_cast<std::size_t>(attribute.index)];
        require(!slot && attribute.data, "Duplicate/missing skin attribute");
        slot = attribute.data;
      }

      require(bool(joints[0]) == bool(weights[0]) && bool(joints[1]) == bool(weights[1])
                  && (!joints[1] || joints[0]),
              "Skin joint/weight sets must be paired and contiguous");

      if (!joints[0]) {
        return {};
      }

      std::vector<Animation::SkinInfluence> result(count);

      for (auto& influence : result) {
        influence.weights = {};
      }

      bool floating{};
      std::vector<std::uint64_t> quantized(count);

      for (std::size_t set = 0; set < 2 && joints[set]; ++set) {
        const auto& joint = *joints[set];
        const auto& weight = *weights[set];
        const auto unsignedType = [](cgltf_component_type type) {
          return type == cgltf_component_type_r_8u || type == cgltf_component_type_r_16u;
        };
        const bool floatWeight = weight.component_type == cgltf_component_type_r_32f;
        require(unsignedType(joint.component_type) && !joint.normalized
                    && (floatWeight ? !weight.normalized
                                    : unsignedType(weight.component_type) && weight.normalized),
                "Invalid skin attribute component/normalization");
        floating = floating || floatWeight;
        const auto indices = accessor(&joint, cgltf_type_vec4, count);
        const auto values = accessor(&weight, cgltf_type_vec4, count);
        const auto denominator = weight.component_type == cgltf_component_type_r_8u ? 255 : 65535;

        for (std::size_t vertex = 0; vertex < count; ++vertex) {
          for (std::size_t slot = 0; slot < 4; ++slot) {
            const auto index = indices[vertex * 4 + slot];
            const auto value = values[vertex * 4 + slot];
            require(index >= 0 && index <= 65535 && std::floor(index) == index && value >= 0
                        && value <= 1,
                    "Skin attribute contains invalid joint/weight values");
            result[vertex].joints[set * 4 + slot] = static_cast<std::uint32_t>(index);
            result[vertex].weights[set * 4 + slot] = value;

            if (!floatWeight) {
              const auto raw = static_cast<std::uint64_t>(
                  std::llround(static_cast<double>(value) * denominator));
              quantized[vertex] += raw * (denominator == 255 ? 257U : 1U);
            }
          }
        }
      }

      bool renormalized{};

      for (std::size_t vertex = 0; vertex < count; ++vertex) {
        const auto& influence = result[vertex];
        double sum{};
        unsigned positive{};
        std::set<std::uint32_t> unique;

        for (std::size_t slot = 0; slot < 8; ++slot) {
          const auto weight = influence.weights[slot];
          sum += weight;

          if (weight > 0) {
            require(unique.insert(influence.joints[slot]).second,
                    "Skin repeats a positive joint influence");
            ++positive;
          }
        }

        require(sum > 0 && (floating || quantized[vertex] == 65535),
                "Skin weights require a positive sum and exact quantized normalization");
        renormalized = renormalized || (floating && std::abs(sum - 1) > 2e-7 * positive);
      }

      if (renormalized) {
        warnings.push_back("Non-unit float skin weights are normalized in native skin data");
      }

      return result;
    }

    struct SourceSkin {
      std::vector<const cgltf_node*> joints;
      std::vector<Math::Mat4> inverseBinds;
      const cgltf_node* commonRoot{};
    };

    auto sourceSkin(const cgltf_skin& source) -> SourceSkin {
      require(source.joints_count > 0 && source.joints_count <= 1024,
              "Skin joint capacity must be in [1,1024]");
      SourceSkin result;
      std::set<const cgltf_node*> unique;
      const auto ancestor = [](const cgltf_node* node, const cgltf_node* parent) {
        for (auto current = node; current; current = current->parent) {
          if (current == parent) {
            return true;
          }
        }

        return false;
      };

      for (std::size_t i = 0; i < source.joints_count; ++i) {
        const auto* joint = source.joints[i];
        require(joint && unique.insert(joint).second, "Skin joint is missing/duplicated");
        require(!source.skeleton || ancestor(joint, source.skeleton),
                "Skin skeleton is not an ancestor of every joint");
        result.joints.push_back(joint);

        if (i == 0) {
          result.commonRoot = joint;
        } else {
          while (result.commonRoot && !ancestor(joint, result.commonRoot)) {
            result.commonRoot = result.commonRoot->parent;
          }

          require(result.commonRoot, "Skin joints require a common hierarchy root");
        }
      }

      result.inverseBinds.resize(source.joints_count);

      if (const auto* matrices = source.inverse_bind_matrices) {
        require(matrices->component_type == cgltf_component_type_r_32f && !matrices->normalized
                    && matrices->count >= source.joints_count && matrices->count <= 1024
                    && (!matrices->buffer_view || matrices->buffer_view->stride == 0),
                "Skin inverse binds require bounded float MAT4 elements without stride");
        const auto values = accessor(matrices, cgltf_type_mat4, matrices->count);
        std::vector<Math::Mat4> all(matrices->count);

        for (std::size_t i = 0; i < all.size(); ++i) {
          std::copy_n(values.begin() + static_cast<std::ptrdiff_t>(i * 16), 16,
                      all[i].data.begin());
        }

        take(Animation::Skin::create({all, {Animation::SkinInfluence{}}}));
        std::copy_n(all.begin(), source.joints_count, result.inverseBinds.begin());
      }

      return result;
    }

    auto validateData(cgltf_data& data) -> void {
      require(data.nodes_count <= 10000 && data.accessors_count <= 8192
                  && data.buffer_views_count <= 8192 && data.materials_count <= 4096
                  && data.images_count <= 1024 && data.meshes_count <= 4096
                  && data.animations_count <= 1024 && data.skins_count <= 1024,
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
      require(data->cameras_count == 0 && data->lights_count == 0,
              "Camera/light model import is not supported");
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
      std::vector<SourceSkin> sourceSkins;

      for (std::size_t i = 0; i < data->skins_count; ++i) {
        sourceSkins.push_back(sourceSkin(data->skins[i]));
      }

      if (std::ranges::any_of(std::span(data->skins, data->skins_count),
                              [](const cgltf_skin& skin) { return skin.skeleton != nullptr; })) {
        result.warnings.push_back(
            "Skeleton pivot metadata is not persisted; node hierarchy and joint order are "
            "retained");
      }

      std::size_t pixelBytes{};

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
        require(decoded.pixels.size() <= 256 * 1024 * 1024 - pixelBytes,
                "Model decoded image budget exceeded");
        pixelBytes += decoded.pixels.size();

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
          m.alphaMode
              = sourceMaterial.alpha_mode == cgltf_alpha_mode_mask    ? Graphics::AlphaMode::Mask
                : sourceMaterial.alpha_mode == cgltf_alpha_mode_blend ? Graphics::AlphaMode::Blend
                                                                      : Graphics::AlphaMode::Opaque;
          m.alphaCutoff = sourceMaterial.alpha_cutoff;
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
          m.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                         pbr.base_color_factor[2], pbr.base_color_factor[3]};
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
        }

        take(validateMaterial(m));
        result.materials.push_back(std::move(m));
      }

      std::vector<std::vector<std::uint32_t>> meshPrimitives(data->meshes_count);
      std::vector<std::vector<Animation::SkinInfluence>> primitiveInfluences;
      std::size_t decodedVertices{}, vertices{}, indices{};

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
          require(position && position->count <= 1000000 - decodedVertices,
                  "Model vertex budget exceeded");
          const auto count = position->count;
          decodedVertices += count;
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
          auto skinWeights = influences(p, count, result.warnings);

          for (std::size_t j = 0; j < count; ++j) {
            output.mesh.vertices[j].position
                = {positions[j * 3], positions[j * 3 + 1], positions[j * 3 + 2]};
          }

          for (std::size_t attributeIndex = 0; attributeIndex < p.attributes_count;
               ++attributeIndex) {
            const auto& a = p.attributes[attributeIndex];

            if (a.type == cgltf_attribute_type_position || a.type == cgltf_attribute_type_joints
                || a.type == cgltf_attribute_type_weights
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
                v.color = {f[0], f[1], f[2], type == cgltf_type_vec4 ? f[3] : 1};
              }
            }
          }

          if (cgltf_find_accessor(&p, cgltf_attribute_type_tangent, 0)) {
            require(cgltf_find_accessor(&p, cgltf_attribute_type_normal, 0),
                    "Tangents require normals");

            /** glTF exporters can round a valid tangent off its normal by a small
             * amount. Bound that drift before publishing the canonical frame.
             * glTF 导出器的小数舍入会让有效切线略偏离法线正交；先限制偏差，再发布规范坐标框架。 */
            for (std::size_t j = 0; j < count; ++j) {
              auto& vertex = output.mesh.vertices[j];
              const Math::Vec3 tangent{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z};
              const auto parallel = static_cast<float>(Math::dot(tangent, vertex.normal));
              require(std::abs(parallel) <= .025F,
                      std::format("Model mesh {} primitive {} tangent {} is not orthogonal",
                                  meshIndex, primitive, j));
              const Math::Vec3 projected{tangent.x - parallel * vertex.normal.x,
                                         tangent.y - parallel * vertex.normal.y,
                                         tangent.z - parallel * vertex.normal.z};
              const auto orthogonal = take(Math::normalized(projected));
              vertex.tangent = {orthogonal.x, orthogonal.y, orthogonal.z, vertex.tangent.w};
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

          /** Material primitives can share a complete source accessor but use
           * disjoint index ranges. Retain source validation while publishing only
           * referenced vertices and matching skin weights in source order.
           * 材质分片可共享完整源访问器却只引用互不相交的索引；完整验证源数据，发布时按源顺序
           * 仅保留被引用顶点及对应蒙皮权重。 */
          std::vector<std::uint32_t> remap(count, std::numeric_limits<std::uint32_t>::max());
          std::size_t used{};

          for (const auto index : output.mesh.indices) {
            if (remap[index] == std::numeric_limits<std::uint32_t>::max()) {
              remap[index] = 0;
              ++used;
            }
          }

          if (used < count) {
            std::size_t next{};

            for (std::size_t old = 0; old < count; ++old) {
              if (remap[old] != std::numeric_limits<std::uint32_t>::max()) {
                remap[old] = static_cast<std::uint32_t>(next);

                if (next != old) {
                  output.mesh.vertices[next] = std::move(output.mesh.vertices[old]);

                  if (!skinWeights.empty()) {
                    skinWeights[next] = std::move(skinWeights[old]);
                  }
                }

                ++next;
              }
            }

            for (auto& index : output.mesh.indices) {
              index = remap[index];
            }

            output.mesh.vertices.resize(used);

            if (!skinWeights.empty()) {
              skinWeights.resize(used);
            }
          }

          if (!cgltf_find_accessor(&p, cgltf_attribute_type_normal, 0)) {
            require(!cgltf_find_accessor(&p, cgltf_attribute_type_tangent, 0),
                    "Tangents require normals");
            require(output.mesh.indices.size() <= 1000000 - vertices,
                    "Flat-normal model vertex budget exceeded");

            if (!skinWeights.empty()) {
              std::vector<Animation::SkinInfluence> expanded;
              expanded.reserve(output.mesh.indices.size());

              for (const auto index : output.mesh.indices) {
                expanded.push_back(skinWeights[index]);
              }

              skinWeights = std::move(expanded);
            }

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
          primitiveInfluences.push_back(std::move(skinWeights));
        }
      }

      require(!result.primitives.empty(), "Model has no triangle geometry");
      const auto selected = scene         ? static_cast<std::size_t>(*scene)
                            : data->scene ? cgltf_scene_index(data.get(), data->scene)
                                          : 0;
      require(!scene || selected < data->scenes_count, "Requested model scene does not exist");
      result.scene = static_cast<std::uint32_t>(selected);
      std::set<const cgltf_node*> visited;
      std::map<const cgltf_node*, std::uint32_t> nodeIds;
      std::vector<const cgltf_node*> sourceNodes;
      std::size_t entities{};
      const auto visit
          = [&](auto&& self, const cgltf_node* node, std::optional<std::uint32_t> parent) -> void {
        require(visited.insert(node).second && ++entities <= 10000,
                "Repeated/cyclic node or entity capacity exceeded");
        require(!node->camera && !node->light && !node->has_mesh_gpu_instancing
                    && (!node->skin || node->mesh),
                "Unsupported model node");
        ModelNode output;
        output.name = node->name && *node->name
                          ? node->name
                          : std::format("Node {}", cgltf_node_index(data.get(), node));
        require(output.name.size() <= 256 && Core::isValidUtf8(output.name),
                "Invalid model node name");
        if (node->has_matrix) {
          Math::Mat4 matrix;
          cgltf_node_transform_local(node, matrix.data.data());
          output.transform = take(Math::decomposeTrs(matrix));
        } else {
          // Preserve authored TRS signs/axes for partial animation channels.
          // 保留作者 TRS 符号及轴，避免部分动画通道与分解后的另一种姿态混用。
          output.transform
              = {{node->translation[0], node->translation[1], node->translation[2]},
                 {node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]},
                 {node->scale[0], node->scale[1], node->scale[2]}};
          output.transform.rotation = take(Math::normalized(output.transform.rotation));
          take(Math::compose(output.transform.position, output.transform.rotation,
                             output.transform.scale));
        }

        output.parent = parent;

        if (node->mesh) {
          for (const auto primitive : meshPrimitives[cgltf_mesh_index(data.get(), node->mesh)]) {
            output.parts.push_back({primitive, {}});
          }

          entities += output.parts.size();
          require(entities <= 10000, "Model entity capacity exceeded");
        }

        const auto id = static_cast<std::uint32_t>(result.nodes.size());
        nodeIds.emplace(node, id);
        result.nodes.push_back(std::move(output));
        sourceNodes.push_back(node);

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
      std::map<std::pair<std::uint32_t, std::size_t>, std::uint32_t> nativeSkins;
      std::size_t skinOwners{}, influencedVertices{};

      for (std::size_t nodeIndex = 0; nodeIndex < sourceNodes.size(); ++nodeIndex) {
        const auto* node = sourceNodes[nodeIndex];

        if (!node->skin) {
          continue;
        }

        const auto sourceIndex = cgltf_skin_index(data.get(), node->skin);
        const auto& skinSource = sourceSkins[sourceIndex];
        require(nodeIds.contains(skinSource.commonRoot),
                "Skin common root is outside selected scene");

        for (auto& part : result.nodes[nodeIndex].parts) {
          const auto& weights = primitiveInfluences[part.primitive];
          require(!weights.empty(), "Skinned mesh primitive requires JOINTS_0/WEIGHTS_0");
          require(++skinOwners <= 128 && weights.size() <= 4'000'000 - influencedVertices,
                  "Model exceeds 128 skin owners/four million influenced vertices");
          influencedVertices += weights.size();
          const auto key = std::pair{part.primitive, sourceIndex};
          auto existing = nativeSkins.find(key);

          if (existing == nativeSkins.end()) {
            ModelSkin skin;
            skin.data = take(Animation::Skin::create({skinSource.inverseBinds, weights}));

            for (const auto* joint : skinSource.joints) {
              require(nodeIds.contains(joint), "Skin joint is outside selected scene");
              skin.joints.push_back(nodeIds.at(joint));
            }

            existing
                = nativeSkins.emplace(key, static_cast<std::uint32_t>(result.skins.size())).first;
            result.skins.push_back(std::move(skin));
          }

          part.skin = existing->second;
        }
      }

      std::size_t channels{}, keys{};

      for (std::size_t index = 0; index < data->animations_count; ++index) {
        const auto& animation = data->animations[index];
        require(animation.channels_count > 0 && animation.channels_count <= 30000 - channels
                    && animation.samplers_count > 0 && animation.samplers_count <= 30000,
                "Animation channel/sampler budget exceeded");
        channels += animation.channels_count;
        Animation::ClipDescription description{
            animation.name && *animation.name ? animation.name : std::format("Animation {}", index),
            {}};

        for (std::size_t channelIndex = 0; channelIndex < animation.channels_count;
             ++channelIndex) {
          const auto& channel = animation.channels[channelIndex];
          require(channel.target_node && !channel.target_node->has_matrix && channel.sampler
                      && (channel.target_path == cgltf_animation_path_type_translation
                          || channel.target_path == cgltf_animation_path_type_rotation
                          || channel.target_path == cgltf_animation_path_type_scale),
                  "Animation requires a TRS node target (matrix/morph targets unsupported)");
          const auto& sampler = *channel.sampler;
          const auto* input = sampler.input;
          const auto* output = sampler.output;
          require(input && output, "Animation sampler accessors are missing");
          const auto& inputMetadata
              = metadata["accessors"][cgltf_accessor_index(data.get(), input)];
          require(input && output && input->component_type == cgltf_component_type_r_32f
                      && output->component_type == cgltf_component_type_r_32f && !input->normalized
                      && !output->normalized
                      && (!input->buffer_view || input->buffer_view->stride == 0)
                      && (!output->buffer_view || output->buffer_view->stride == 0)
                      && input->count <= 1000000 - keys && input->has_min && input->has_max
                      && inputMetadata["min"].size() == 1 && inputMetadata["max"].size() == 1,
                  "Animation requires bounded float accessors without stride/normalization and "
                  "time bounds");
          keys += input->count;
          const auto mode = sampler.interpolation == cgltf_interpolation_type_step
                                ? Animation::Interpolation::Step
                            : sampler.interpolation == cgltf_interpolation_type_linear
                                ? Animation::Interpolation::Linear
                                : Animation::Interpolation::CubicSpline;
          const bool cubic = mode == Animation::Interpolation::CubicSpline;
          const bool rotation = channel.target_path == cgltf_animation_path_type_rotation;
          const auto times = accessor(input, cgltf_type_scalar, input->count);
          const auto values = accessor(output, rotation ? cgltf_type_vec4 : cgltf_type_vec3,
                                       input->count * (cubic ? 3 : 1));
          require(input->min[0] == times.front() && input->max[0] == times.back(),
                  "Animation time bounds differ from keys");
          Animation::Track track{
              static_cast<std::uint32_t>(cgltf_node_index(data.get(), channel.target_node)),
              rotation ? Animation::Path::Rotation
              : channel.target_path == cgltf_animation_path_type_scale
                  ? Animation::Path::Scale
                  : Animation::Path::Translation,
              mode,
              {}};
          const std::size_t components = rotation ? 4 : 3;
          const auto value = [&](std::size_t item) -> Math::Vec4 {
            const auto offset = item * components;
            return {values[offset], values[offset + 1], values[offset + 2],
                    rotation ? values[offset + 3] : 0};
          };
          track.keys.reserve(times.size());

          for (std::size_t key = 0; key < times.size(); ++key) {
            track.keys.push_back(cubic ? Animation::Key{times[key], value(key * 3 + 1),
                                                        value(key * 3), value(key * 3 + 2)}
                                       : Animation::Key{times[key], value(key)});
          }

          description.tracks.push_back(std::move(track));
        }

        // Validate excluded channels too; scene selection cannot hide bad input.
        // 被场景选择排除的通道也必须校验，不允许筛选掩盖无效源数据。
        take(Animation::Clip::create(description));
        const auto sourceChannels = description.tracks.size();
        std::erase_if(description.tracks, [&](const auto& track) {
          return !nodeIds.contains(&data->nodes[track.node]);
        });
        const auto excluded = sourceChannels - description.tracks.size();

        for (auto& track : description.tracks) {
          track.node = nodeIds.at(&data->nodes[track.node]);
        }

        if (excluded) {
          result.warnings.push_back(std::format(
              "Animation {}: {} channels outside selected scene excluded", index, excluded));
        }

        if (!description.tracks.empty()) {
          result.animations.push_back(take(Animation::Clip::create(std::move(description))));
        }
      }

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
                  && model.images.size() <= 1024 && model.animations.size() <= 1024
                  && model.skins.size() <= 128,
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

      auto scene = take(World::Scene::create("Imported model"));
      std::vector<World::EntityId> nodes;
      std::vector<World::ParentUpdate> parents;
      std::vector<std::tuple<World::EntityId, std::uint32_t, std::uint32_t>> pendingSkins;
      std::set<std::uint32_t> usedSkins;
      std::size_t influencedVertices{};

      for (const auto& skin : model.skins) {
        const auto& description = *take(skin.data.description());
        require(skin.joints.size() == description.inverseBindMatrices.size(),
                "Prepared skin inverse binds/joint counts differ");
        std::set<std::uint32_t> unique;

        for (const auto joint : skin.joints) {
          require(joint < model.nodes.size() && unique.insert(joint).second,
                  "Prepared skin joint is missing/duplicated");
        }
      }

      for (const auto& node : model.nodes) {
        auto entity = take(scene.createEntity(node.name));
        nodes.push_back(entity);
        const auto& t = node.transform;
        take(scene.setTransform(entity, {t.position, t.rotation, t.scale}));

        if (node.parent) {
          require(*node.parent < nodes.size() - 1, "Invalid prepared node parent");
          parents.push_back({entity, nodes[*node.parent]});
        }

        for (const auto& attachment : node.parts) {
          const auto primitive = attachment.primitive;
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

          if (attachment.skin) {
            require(*attachment.skin < model.skins.size(), "Invalid prepared skin index");
            const auto& skin = model.skins[*attachment.skin];
            const auto size = take(skin.data.description())->vertices.size();
            require(size == model.primitives[primitive].mesh.vertices.size()
                        && size <= 4'000'000 - influencedVertices && pendingSkins.size() < 128,
                    "Prepared skin vertex count/owner budget differs from mesh");
            influencedVertices += size;
            pendingSkins.emplace_back(part, *attachment.skin, primitive);
            usedSkins.insert(*attachment.skin);
          }
        }
      }

      take(scene.setParents(parents));
      require(usedSkins.size() == model.skins.size(), "Prepared model contains unused skin assets");
      std::vector<std::vector<std::byte>> encodedSkins;
      std::size_t skinBytes{};

      for (const auto& skin : model.skins) {
        const auto& description = *take(skin.data.description());
        const auto size
            = 16 + 64 * description.inverseBindMatrices.size() + 64 * description.vertices.size();
        require(size <= 256 * 1024 * 1024 - skinBytes, "Prepared skin byte budget exceeds 256 MiB");
        skinBytes += size;
        encodedSkins.push_back(take(Animation::encodeSkin(skin.data)));
      }

      for (const auto& [owner, index, primitive] : pendingSkins) {
        const auto& skin = model.skins[index];
        World::SkinBinding binding;
        binding.asset = std::format("{}/skins/{}.dotskin", model.prefix, index);

        for (const auto joint : skin.joints) {
          binding.joints.push_back(nodes[joint].value);
        }

        take(scene.setSkin(owner, std::move(binding)));
        auto bound = take(Animation::SceneSkin::bind(scene, owner, skin.data));
        const auto& mesh = model.primitives[primitive].mesh;
        take(Graphics::deformMesh(mesh, skin.data, take(bound.palette(scene))));
      }

      std::vector<std::string> encodedClips;
      std::size_t clipBytes{}, channelCount{}, keyCount{};

      for (const auto& clip : model.animations) {
        const auto& description = *take(clip.description());
        require(description.tracks.size() <= 30000 - channelCount,
                "Prepared animation channel budget exceeded");
        channelCount += description.tracks.size();

        for (const auto& track : description.tracks) {
          require(track.node < nodes.size() && track.keys.size() <= 1000000 - keyCount,
                  "Prepared animation target/key budget exceeded");
          keyCount += track.keys.size();
        }

        auto encoded = take(Animation::encodeClip(clip));
        require(encoded.size() <= fileLimit - clipBytes, "Prepared animation byte budget exceeded");
        clipBytes += encoded.size();
        encodedClips.push_back(std::move(encoded));
      }

      Json descriptor{{"scene", model.prefix + "/model.dotscene"},
                      {"animations", Json::array()},
                      {"nodes", Json::array()}};

      for (const auto node : nodes) {
        descriptor["nodes"].push_back(std::to_string(node.value));
      }

      for (std::size_t i = 0; i < encodedClips.size(); ++i) {
        descriptor["animations"].push_back(
            std::format("{}/animations/{}.dotanimation", model.prefix, i));
      }

      const auto descriptorText = descriptor.dump() + "\n";
      require(descriptorText.size() <= 1024 * 1024, "Model descriptor exceeds 1 MiB");

      if (!encodedClips.empty()) {
        World::AnimationPlayer player;

        for (const auto& path : descriptor["animations"]) {
          player.clips.push_back(path.get<std::string>());
        }

        for (std::size_t i = 0; i < nodes.size(); ++i) {
          player.targets.push_back({static_cast<std::uint32_t>(i), nodes[i].value});
        }

        take(scene.setAnimationPlayer(nodes.front(), std::move(player)));
      }

      require(std::filesystem::is_empty(staging.root()), "Model staging must be empty");
      take(staging.createDirectories("meshes"));
      take(staging.createDirectories("materials"));
      take(staging.createDirectories("images"));

      if (!encodedClips.empty()) {
        take(staging.createDirectories("animations"));
      }

      if (!encodedSkins.empty()) {
        take(staging.createDirectories("skins"));
      }

      for (std::size_t i = 0; i < encodedSkins.size(); ++i) {
        take(staging.writeAtomic(std::format("skins/{}.dotskin", i), encodedSkins[i]));
      }

      for (std::size_t i = 0; i < encodedClips.size(); ++i) {
        const auto name = std::format("animations/{}.dotanimation", i);
        take(staging.writeTextAtomic(name, encodedClips[i]));
      }

      for (std::size_t i = 0; i < model.images.size(); ++i) {
        take(staging.writeAtomic(std::format("images/{}.png", i), model.images[i].png));
      }

      for (std::size_t i = 0; i < model.materials.size(); ++i) {
        take(saveMaterial(staging, std::format("materials/{}.dotmaterial", i), model.materials[i]));
      }

      for (std::size_t i = 0; i < model.primitives.size(); ++i) {
        take(saveMesh(staging, std::format("meshes/{}.dotmesh", i), model.primitives[i].mesh));
      }

      take(World::saveScene(staging, "model.dotscene", scene));
      take(staging.writeTextAtomic("model.dotmodel", descriptorText));
      return {};
    } catch (const Failure& error) {
      return Core::fail(error.error.code, "Write model bundle: " + error.error.message);
    } catch (const std::exception& error) {
      return Core::fail(Core::ErrorCode::External,
                        "Write model bundle: " + std::string(error.what()));
    }
  }
}  // namespace Dot::Assets
