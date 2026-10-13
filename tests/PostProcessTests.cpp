#include <doctest/doctest.h>

#include <limits>

import Dot.Graphics.PostProcess;
import Dot.Graphics.Image;
import Dot.Core.Result;

using namespace Dot::Graphics;

TEST_CASE("post_process.exposure_and_enum_boundaries_are_explicit") {
  CHECK(validatePostProcess({}));

  for (const auto mapper : {ToneMap::LinearClamp, ToneMap::Reinhard, ToneMap::AcesFitted}) {
    CHECK(validatePostProcess({-16, mapper, ColorSpace::Linear}));
    CHECK(validatePostProcess({16, mapper, ColorSpace::Srgb}));
  }

  CHECK_FALSE(validatePostProcess({-16.01F}));
  CHECK_FALSE(validatePostProcess({16.01F}));
  CHECK_FALSE(validatePostProcess({std::numeric_limits<float>::quiet_NaN()}));
  CHECK_FALSE(validatePostProcess({std::numeric_limits<float>::infinity()}));
  CHECK_FALSE(validatePostProcess({0, static_cast<ToneMap>(99)}));
  CHECK_FALSE(validatePostProcess({0, ToneMap::AcesFitted, static_cast<ColorSpace>(99)}));
}
