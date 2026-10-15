#include <doctest/doctest.h>

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

import Dot.Animation.Clip;
import Dot.Animation.Files;
import Dot.Core.Files;
import Dot.Core.StagedDirectory;

namespace {
  using namespace Dot;
  using namespace Dot::Animation;

  auto clip() -> Clip {
    return Clip::create({"测试片段",
                         {{0,
                           Path::Translation,
                           Interpolation::CubicSpline,
                           {{0, {0, 0, 0, 0}, {}, {1, 0, 0, 0}}, {2, {2, 0, 0, 0}, {}, {}}}}}})
        .value();
  }
}  // namespace

TEST_CASE("animation.files_strict_codec_roundtrip_rejects_unknown_duplicates_types_and_bounds") {
  using Json = nlohmann::json;
  const auto source = encodeClip(clip()).value();
  auto decoded = decodeClip(source);
  REQUIRE(decoded);
  CHECK(encodeClip(*decoded).value() == source);
  CHECK(decoded->sample(1)->at(0).value.x == clip().sample(1)->at(0).value.x);
  CHECK_FALSE(encodeClip(Clip{}));

  for (const auto* bad : {"", "{}", "[]", "null", "{\"name\":\"a\",\"name\":\"b\"}", "\xc0\x80"}) {
    CHECK_FALSE(decodeClip(bad));
  }

  auto root = Json::parse(source);
  auto bad = root;
  bad["extra"] = 1;
  CHECK_FALSE(decodeClip(bad.dump()));
  bad = root;
  bad["tracks"][0]["node"] = 1.0;
  CHECK_FALSE(decodeClip(bad.dump()));
  bad = root;
  bad["tracks"][0]["node"] = -1;
  CHECK_FALSE(decodeClip(bad.dump()));
  bad = root;
  bad["tracks"][0]["path"] = "weights";
  CHECK_FALSE(decodeClip(bad.dump()));
  bad = root;
  bad["tracks"][0]["keys"][0].erase("inTangent");
  CHECK_FALSE(decodeClip(bad.dump()));
  bad = root;
  bad["tracks"][0]["interpolation"] = "linear";
  CHECK_FALSE(decodeClip(bad.dump()));
  bad = root;
  bad["tracks"][0]["keys"][1]["time"] = 0;
  CHECK_FALSE(decodeClip(bad.dump()));
  CHECK_FALSE(decodeClip(std::string(65, '[') + "0" + std::string(65, ']')));
}

TEST_CASE("animation.files_atomic_rooted_paths_and_failed_save_preserve_previous_asset") {
  auto storage = Core::StagedDirectory::create(
                     Core::FileSystem::open(std::filesystem::temp_directory_path()).value())
                     .value();
  auto files = storage.files().value();
  REQUIRE(saveClip(files, "walk.dotanimation", clip()));
  const auto before = files.readText("walk.dotanimation").value();
  REQUIRE(loadClip(files, "walk.dotanimation"));
  CHECK_FALSE(saveClip(files, "walk.dotanimation", Clip{}));
  CHECK(files.readText("walk.dotanimation").value() == before);
  CHECK_FALSE(saveClip(files, "../outside.dotanimation", clip()));
  CHECK_FALSE(saveClip(files, "walk.json", clip()));
  CHECK_FALSE(saveClip(files, "absent/walk.dotanimation", clip()));
  CHECK_FALSE(loadClip(files, "missing.dotanimation"));
  REQUIRE(files.createDirectories("directory.dotanimation"));
  CHECK_FALSE(loadClip(files, "directory.dotanimation"));
  CHECK_FALSE(saveClip(files, "directory.dotanimation", clip()));
  std::error_code error;
  std::filesystem::create_symlink(files.root() / "walk.dotanimation",
                                  files.root() / "link.dotanimation", error);

  REQUIRE_FALSE(error);
  CHECK_FALSE(loadClip(files, "link.dotanimation"));
  CHECK_FALSE(saveClip(files, "link.dotanimation", clip()));

  CHECK(files.readText("walk.dotanimation").value() == before);
}
