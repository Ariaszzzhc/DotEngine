#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <filesystem>
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
  auto before = files->readBytes(name);
  REQUIRE(before);
  CHECK_FALSE(saveMesh(*files, name, {}));
  auto after = files->readBytes(name);
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
  REQUIRE(files->writeAtomic(name, std::array<std::byte, 16>{}));
  auto corrupt = loadMesh(*files, name);
  REQUIRE_FALSE(corrupt);
  CHECK(corrupt.error().message.find(".DOTMESH") != std::string::npos);
}
