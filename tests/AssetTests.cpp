#include <doctest/doctest.h>

#include <filesystem>
#include <limits>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>

import Dot.Assets.Mesh;
import Dot.Core.Files;
import Dot.Core.Result;
import Dot.Graphics.Geometry;
import Dot.Math;

namespace {
  namespace Fs = std::filesystem;
  using Json = nlohmann::json;
  using namespace Dot::Assets;
  constexpr auto triangleObj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";

  struct Directory {
    Fs::path root;

    Directory() {
      std::random_device random;

      for (int attempt = 0; attempt < 100; ++attempt) {
        root = Fs::temp_directory_path()
               / ("dot-assets-" + std::to_string(random()) + "-" + std::to_string(random()));
        std::error_code error;

        if (Fs::create_directory(root, error)) {
          return;
        }

        if (error) {
          throw std::runtime_error(error.message());
        }
      }

      throw std::runtime_error("Cannot create asset test directory");
    }

    ~Directory() {
      std::error_code ignored;
      Fs::remove_all(root, ignored);
    }
  };
}  // namespace

TEST_CASE("assets.mesh_json_round_trip_preserves_vertices_indices_and_colors") {
  auto mesh = Dot::Graphics::cube({2, 3, 4});
  REQUIRE(mesh);
  mesh->vertices[2].color = {0.2F, 0.4F, 0.6F, 0.8F};
  auto encoded = encodeMesh(*mesh);
  REQUIRE(encoded);
  auto decoded = decodeMesh(*encoded, MeshFormat::DotMesh);
  REQUIRE_MESSAGE(decoded, (decoded ? "" : decoded.error().message));
  CHECK(decoded->warnings.empty());
  CHECK(decoded->data.indices == mesh->indices);
  REQUIRE(decoded->data.vertices.size() == mesh->vertices.size());

  for (std::size_t index = 0; index < mesh->vertices.size(); ++index) {
    CHECK(decoded->data.vertices[index].position == mesh->vertices[index].position);
    CHECK(decoded->data.vertices[index].color == mesh->vertices[index].color);
    CHECK(decoded->data.vertices[index].normal == mesh->vertices[index].normal);
    CHECK(decoded->data.vertices[index].texCoord == mesh->vertices[index].texCoord);
  }
}

TEST_CASE("assets.surface_format_preserves_required_normal_and_uv_attributes") {
  auto mesh = Dot::Graphics::quad().value();
  mesh.vertices[0].normal = {0, 0, 1};
  mesh.vertices[0].texCoord = {-2.5F, 3};
  auto encoded = encodeMesh(mesh).value();
  auto json = Json::parse(encoded);
  CHECK_FALSE(json.contains("version"));
  auto decoded = decodeMesh(encoded, MeshFormat::DotMesh);
  REQUIRE(decoded);
  CHECK(decoded->data.vertices[0].normal == mesh.vertices[0].normal);
  CHECK(decoded->data.vertices[0].texCoord == mesh.vertices[0].texCoord);

  for (const auto* field : {"normal", "texCoord"}) {
    auto incomplete = json;
    incomplete["vertices"][0].erase(field);
    CHECK_FALSE(decodeMesh(incomplete.dump(), MeshFormat::DotMesh));
  }

  auto invalid = json;
  invalid["vertices"][0]["normal"] = {0, 0, 2};
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  invalid = json;
  invalid["vertices"][0]["texCoord"] = {0, 1, 2};
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
}

TEST_CASE("assets.obj_splits_hard_normal_and_uv_seams_without_losing_authored_values") {
  const auto source
      = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt -2 3\nvt 1 1\n"
        "vn 0 0 2\nvn 0 2 0\nf 1/1/1 2/1/1 3/1/1\nf 1/2/2 3/1/2 2/1/2\n";
  auto result = decodeMesh(source, MeshFormat::Obj);
  REQUIRE(result);
  REQUIRE(result->data.vertices.size() == 6);
  CHECK(result->warnings.empty());
  CHECK(result->data.vertices[0].position == result->data.vertices[3].position);
  CHECK(result->data.vertices[0].normal == Dot::Math::Vec3{0, 0, 1});
  CHECK(result->data.vertices[3].normal == Dot::Math::Vec3{0, 1, 0});
  CHECK(result->data.vertices[0].texCoord == Dot::Math::Vec2{-2, 3});
  CHECK(result->data.vertices[3].texCoord == Dot::Math::Vec2{1, 1});
  auto zero
      = decodeMesh("v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 0 0 0\nf 1//1 2//1 3//1\n", MeshFormat::Obj);
  CHECK_FALSE(zero);
  CHECK_FALSE(decodeMesh("vt 0 0 2\n" + std::string(triangleObj), MeshFormat::Obj));
  CHECK_FALSE(decodeMesh("v 0 0 0\nv 1 0 0\nv 0 1 0\n", MeshFormat::Obj));
}

TEST_CASE("assets.mesh_json_preserves_extreme_finite_positions") {
  auto mesh = Dot::Graphics::cube();
  REQUIRE(mesh);
  mesh->vertices[0].position.x = std::numeric_limits<float>::max();
  mesh->vertices[1].position.y = std::numeric_limits<float>::denorm_min();
  auto encoded = encodeMesh(*mesh);
  REQUIRE(encoded);
  auto decoded = decodeMesh(*encoded, MeshFormat::DotMesh);
  REQUIRE(decoded);
  CHECK(decoded->data.vertices[0].position.x == mesh->vertices[0].position.x);
  CHECK(decoded->data.vertices[1].position.y == mesh->vertices[1].position.y);
}

TEST_CASE("assets.mesh_json_rejects_unknown_metadata_types_and_fields") {
  auto mesh = Dot::Graphics::quad();
  REQUIRE(mesh);
  auto encoded = encodeMesh(*mesh);
  REQUIRE(encoded);
  auto source = Json::parse(*encoded);

  auto invalid = source;
  invalid["extra"] = 1;
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  invalid = source;
  invalid["vertices"][0]["color"] = {1, 1, 1};
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  invalid = source;
  invalid["vertices"][0]["position"][0] = true;
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  invalid = source;
  invalid["vertices"][0]["position"][0] = 1e100;
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  invalid = source;
  invalid["vertices"][0]["color"][2] = -1;
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  invalid = source;
  invalid["vertices"][0]["extra"] = 0;
  CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
}

TEST_CASE("assets.mesh_json_rejects_bad_indices_and_topology") {
  auto mesh = Dot::Graphics::quad();
  REQUIRE(mesh);
  auto encoded = encodeMesh(*mesh);
  REQUIRE(encoded);
  auto source = Json::parse(*encoded);

  for (auto index : {Json(-1), Json(4), Json(1.0), Json(true), Json(4294967296ULL)}) {
    auto invalid = source;
    invalid["indices"][0] = index;
    CHECK_FALSE(decodeMesh(invalid.dump(), MeshFormat::DotMesh));
  }

  source["indices"] = {0, 1};
  CHECK_FALSE(decodeMesh(source.dump(), MeshFormat::DotMesh));
  CHECK_FALSE(encodeMesh({}));
}

TEST_CASE("assets.mesh_json_rejects_duplicates_and_excessive_nesting") {
  auto duplicate = decodeMesh(R"({"vertices":[],"vertices":[],"indices":[]})", MeshFormat::DotMesh);
  REQUIRE_FALSE(duplicate);
  CHECK(duplicate.error().message.find("Duplicate") != std::string::npos);
  auto deep = decodeMesh(std::string(70, '[') + "0" + std::string(70, ']'), MeshFormat::DotMesh);
  REQUIRE_FALSE(deep);
  CHECK(deep.error().message.find("64") != std::string::npos);
  CHECK_FALSE(decodeMesh("{", MeshFormat::DotMesh));
  CHECK_FALSE(decodeMesh("null", MeshFormat::DotMesh));
}

TEST_CASE("assets.obj_triangulates_quads_and_resolves_negative_indices") {
  auto mesh = decodeMesh("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf -4 -3 -2 -1\n", MeshFormat::Obj);
  REQUIRE_MESSAGE(mesh, (mesh ? "" : mesh.error().message));
  CHECK(mesh->data.vertices.size() == 4);
  CHECK(mesh->data.indices.size() == 6);
  CHECK(mesh->warnings.empty());
  CHECK(Dot::Graphics::validateMesh(mesh->data));
}

TEST_CASE("assets.obj_vertex_colors_and_optional_weight") {
  auto mesh
      = decodeMesh("v +0 0 0 1 0 0\nv 1 0 0 0 1 0\nv 0 1 0 0 0 1\nf 1 2 3\n", MeshFormat::Obj);
  REQUIRE(mesh);
  CHECK(mesh->data.vertices[0].color.x == 1);
  CHECK(mesh->data.vertices[1].color.y == 1);
  CHECK(mesh->data.vertices[2].color.z == 1);
  CHECK(decodeMesh("v 0 0 0 1\nv 1 0 0 1\nv 0 1 0 1\nf 1 2 3\n", MeshFormat::Obj));
  CHECK_FALSE(decodeMesh("v 0 0 0 2\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", MeshFormat::Obj));
}

TEST_CASE("assets.obj_surface_attributes_are_retained_and_bad_references_fail") {
  const std::string prefix = "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvn 0 0 1\n";
  auto mesh = decodeMesh(prefix + "f 1/1/1 2/1/1 3/1/1\n", MeshFormat::Obj);
  REQUIRE(mesh);
  CHECK(mesh->warnings.empty());
  CHECK(mesh->data.vertices[0].normal == Dot::Math::Vec3{0, 0, 1});
  CHECK(mesh->data.vertices[0].texCoord == Dot::Math::Vec2{0, 0});
  CHECK(decodeMesh(prefix + "f 1//1 2//1 3//1\n", MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(prefix + "f 1/2/1 2/1/1 3/1/1\n", MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(prefix + "f 1/1/2 2/1/1 3/1/1\n", MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(prefix + "f 1//-2 2//1 3//1\n", MeshFormat::Obj));
  auto grouped = decodeMesh("o triangle\ns 1\n" + std::string(triangleObj), MeshFormat::Obj);
  REQUIRE(grouped);
  CHECK_FALSE(grouped->warnings.empty());
  CHECK_FALSE(decodeMesh("s garbage\n" + std::string(triangleObj), MeshFormat::Obj));
}

TEST_CASE("assets.obj_rejects_numeric_fallbacks_and_malformed_faces") {
  for (const auto source :
       {"v nope 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "v 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n",
        "v 1e100 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1x 2 3\n",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 2 3\n", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 4\n",
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/ 2/ 3/\n", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2\n"}) {
    CHECK_FALSE(decodeMesh(source, MeshFormat::Obj));
  }
}

TEST_CASE("assets.obj_rejects_materials_and_nontriangle_primitives") {
  for (const auto suffix :
       {"mtllib outside.mtl\n", "usemtl material\n", "l 1 2\n", "p 1\n", "curv 0 1 1 2 3\n"}) {
    auto mesh = decodeMesh(std::string(triangleObj) + suffix, MeshFormat::Obj);
    REQUIRE_FALSE(mesh);
    CHECK(mesh.error().code == Dot::Core::ErrorCode::Unavailable);
  }
}

TEST_CASE("assets.source_size_nul_and_format_boundaries") {
  CHECK_FALSE(decodeMesh("", MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(std::string(64 * 1024 * 1024 + 1, ' '), MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(std::string(triangleObj) + '\0', MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(triangleObj, static_cast<MeshFormat>(999)));
  CHECK_FALSE(decodeMesh("v nan 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", MeshFormat::Obj));
  CHECK_FALSE(decodeMesh(std::string(1024 * 1024 + 1, ' ') + '\n' + triangleObj, MeshFormat::Obj));
  std::string hugeFace = std::string(triangleObj) + "f";

  for (int index = 0; index < 65; ++index) {
    hugeFace += " 1";
  }

  CHECK_FALSE(decodeMesh(hugeFace, MeshFormat::Obj));
}

TEST_CASE("assets.atomic_save_load_and_failed_save_preserves_previous_asset") {
  Directory directory;
  auto files = Dot::Core::FileSystem::open(directory.root);
  auto mesh = Dot::Graphics::cube();
  REQUIRE(files);
  REQUIRE(mesh);
  const auto name = Fs::path(u8"几何.DOTMESH");
  REQUIRE(saveMesh(*files, name, *mesh));
  auto decoded = loadMesh(*files, name);
  REQUIRE(decoded);
  CHECK(decoded->data.indices == mesh->indices);
  auto before = files->readText(name);
  REQUIRE(before);
  CHECK_FALSE(saveMesh(*files, name, {}));
  auto after = files->readText(name);
  REQUIRE(after);
  CHECK(*after == *before);
  CHECK_FALSE(saveMesh(*files, "missing/cube.dotmesh", *mesh));
  CHECK_FALSE(saveMesh(*files, "cube.obj", *mesh));
  auto missing = loadMesh(*files, "missing.dotmesh");
  REQUIRE_FALSE(missing);
  CHECK(missing.error().code == Dot::Core::ErrorCode::NotFound);
  CHECK_FALSE(loadMesh(*files, "cube.bin"));
  CHECK_FALSE(loadMesh(*files, "../outside.dotmesh"));
  REQUIRE(files->writeTextAtomic("triangle.OBJ", triangleObj));
  CHECK(loadMesh(*files, "triangle.OBJ"));
  CHECK(std::distance(Fs::directory_iterator(directory.root), Fs::directory_iterator{}) == 2);
}
