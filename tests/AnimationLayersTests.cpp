#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import Dot.Animation.Layers;

namespace {
  using namespace Dot::Animation;

  auto motion() -> Clip {
    return Clip::create(
               {"Motion",
                {{0,
                  Path::Translation,
                  Interpolation::Linear,
                  {{0, {0, 0, 0, 0}}, {2, {4, 0, 0, 0}}}},
                 {1, Path::Scale, Interpolation::Linear, {{0, {1, 1, 1, 0}}, {2, {-1, 1, 1, 0}}}}}})
        .value();
  }

  auto stack(bool playing = true) -> Layers {
    const std::array definitions{
        LayerDefinition{"Action", motion(), {{0, Path::Translation, .5}}, .6, 1, false, playing}};
    return Layers::create(definitions).value();
  }
}  // namespace

TEST_CASE("animation_layers.weight_fades_use_unscaled_time_pause_interrupt_and_retire") {
  auto value = stack()
                   .updated("Action", ConfigureLayer{0, false})
                   .value()
                   .updated("Action", WeightLayer{0})
                   .value()
                   .updated("Action", FadeLayer{1, 2, false})
                   .value()
                   .advanced(.5)
                   .value();
  CHECK(value.states()->front().playback.time == 0);
  CHECK(value.states()->front().playback.weight == .25);
  CHECK(value.running().value());
  auto paused = value.updated("Action", PauseLayer{}).value().advanced(1).value();
  CHECK(paused.states()->front().playback.weight == .25);
  auto next = paused.updated("Action", PlayLayer{})
                  .value()
                  .updated("Action", FadeLayer{0, 1, true})
                  .value()
                  .advanced(.5)
                  .value();
  CHECK(next.states()->front().playback.weight == .125);
  next = next.advanced(.5).value();
  CHECK(next.sample()->empty());
  CHECK_FALSE(next.states()->front().playback.active);
  CHECK_FALSE(next.states()->front().playback.fade);
  CHECK(next.states()->front().playback.time == 0);
  CHECK_FALSE(value.updated("Action", FadeLayer{1, 1, true}));
  CHECK_FALSE(value.updated("Action", FadeLayer{0, -1, true}));
  CHECK_FALSE(value.updated("Action", FadeLayer{2, 1, false}));
}

TEST_CASE("animation_layers.strict_controls_and_complete_snapshot_validation") {
  CHECK(std::holds_alternative<PlayLayer>(
      decodeLayerControl(R"({"action":"play","restart":true})").value()));
  CHECK(std::holds_alternative<FadeLayer>(
      decodeLayerControl(R"({"action":"fade","weight":0,"seconds":0.2,"stop":true})").value()));

  for (const auto* json :
       {R"({"action":"play","restart":1})", R"({"action":"pause","extra":0})",
        R"({"action":"weight","weight":true})", R"({"action":"seek","seconds":"1"})",
        R"({"action":"configure","speed":1,"looping":1})",
        R"({"action":"fade","weight":0,"seconds":1,"stop":0})",
        R"({"action":"stop","action":"pause"})", R"({"action":"unknown"})"}) {
    CHECK_FALSE(decodeLayerControl(json));
  }

  auto value = stack().updated("Action", FadeLayer{1, 1, false}).value().advanced(.5).value();
  const auto clock = value.states()->front().playback;
  const std::array clocks{clock};
  CHECK(value.restored(clocks)->states()->front().playback == clock);
  CHECK_FALSE(value.restored({}));
  auto invalid = clock;
  invalid.time = 3;
  CHECK_FALSE(value.restored(std::array{invalid}));
  invalid = clock;
  invalid.weight = .123;
  CHECK_FALSE(value.restored(std::array{invalid}));
  invalid = clock;
  invalid.active = false;
  CHECK_FALSE(value.restored(std::array{invalid}));
  CHECK(value.states()->front().playback == clock);
}

TEST_CASE("animation_layers.ordered_masked_samples_independent_clocks_and_owned_snapshots") {
  auto first = stack();
  auto next = first.advanced(1).value();
  const auto samples = next.sample().value();
  REQUIRE(samples.size() == 1);
  CHECK(samples[0].channel.node == 0);
  CHECK(samples[0].channel.value.x == 2);
  CHECK(samples[0].weight == doctest::Approx(.3));
  CHECK(first.states()->front().playback.time == 0);
  auto snapshot = next.states().value();
  auto paused = next.updated("Action", PauseLayer{}).value().advanced(1).value();
  CHECK(paused.states()->front().playback.time == 1);
  CHECK(paused.sample()->front().channel.value.x == 2);
  auto stopped = paused.updated("Action", StopLayer{}).value();
  CHECK(stopped.sample()->empty());
  CHECK_FALSE(stopped.states()->front().playback.active);
  CHECK(snapshot.front().playback.playing);
  CHECK(snapshot.front().playback.time == 1);
  CHECK(samples.front().channel.value.x == 2);
  CHECK(stopped.channels()->size() == 1);
}

TEST_CASE("animation_layers.reverse_wrap_terminal_holds_and_zero_speed") {
  auto value = stack(false)
                   .updated("Action", ConfigureLayer{-1, true})
                   .value()
                   .updated("Action", PlayLayer{true})
                   .value();
  CHECK(value.states()->front().playback.time == 2);
  value = value.advanced(2.5).value();
  CHECK(value.states()->front().playback.time == 1.5);
  value = value.updated("Action", ConfigureLayer{-1, false}).value().advanced(2).value();
  CHECK(value.states()->front().playback.time == 0);
  CHECK_FALSE(value.states()->front().playback.playing);
  CHECK(value.states()->front().playback.active);
  CHECK_FALSE(value.running().value());
  value = value.updated("Action", SeekLayer{1}).value();
  CHECK(value.sample()->front().channel.value.x == 2);
  value = value.updated("Action", ConfigureLayer{0, true})
              .value()
              .updated("Action", PlayLayer{})
              .value()
              .advanced(86400)
              .value();
  CHECK(value.states()->front().playback.time == 1);
  CHECK_FALSE(value.running().value());
}

TEST_CASE("animation_layers.zero_weights_and_unmasked_invalid_tracks_are_not_sampled") {
  auto source = motion();
  const std::array definitions{
      LayerDefinition{"First", source, {{0, Path::Translation}}, 1, 1, true, true},
      LayerDefinition{"Second", source, {{1, Path::Scale}}, 0, 1, true, true}};
  auto value = Layers::create(definitions).value().advanced(1).value();
  CHECK(value.sample()->size() == 1);
  auto invalid = value.updated("Second", WeightLayer{1}).value();
  CHECK_FALSE(invalid.sample());
  CHECK(value.sample()->size() == 1);
  CHECK(value.states()->at(1).playback.weight == 0);
  auto valid = invalid.updated("Second", SeekLayer{.5}).value();
  auto samples = valid.sample().value();
  REQUIRE(samples.size() == 2);
  CHECK(samples[0].channel.path == Path::Translation);
  CHECK(samples[1].channel.path == Path::Scale);
  CHECK(samples[1].channel.value.x == .5F);
}

TEST_CASE("animation_layers.rejected_controls_preserve_input_and_moves_retire_source") {
  auto value = stack();
  const auto before = value.states()->front().playback;
  CHECK_FALSE(value.updated("Missing", PlayLayer{}));
  CHECK_FALSE(value.updated("Action", SeekLayer{-1}));
  CHECK_FALSE(value.updated("Action", SeekLayer{3}));
  CHECK_FALSE(value.updated("Action", ConfigureLayer{17, true}));
  CHECK_FALSE(value.updated("Action", WeightLayer{-1}));
  CHECK_FALSE(value.updated("Action", WeightLayer{std::numeric_limits<double>::quiet_NaN()}));
  CHECK_FALSE(value.advanced(-1));
  CHECK_FALSE(value.advanced(std::numeric_limits<double>::infinity()));
  CHECK(value.states()->front().playback == before);
  auto moved = std::move(value);
  CHECK_FALSE(value.sample());
  CHECK_FALSE(value.states());
  CHECK_FALSE(value.channels());
  CHECK_FALSE(value.running());
  CHECK_FALSE(value.stopped());
  CHECK_FALSE(value.advanced(0));
  CHECK_FALSE(value.updated("Action", PauseLayer{}));
  CHECK(moved.stopped()->sample()->empty());
  auto copied = Layers::create({}).value();
  copied = moved;
  CHECK(copied.states().value() == moved.states().value());
  auto* alias = &copied;
  copied = *alias;
  copied = std::move(*alias);
  CHECK(copied.states().value() == moved.states().value());
  auto replacement = Layers::create({}).value();
  replacement = std::move(copied);
  CHECK_FALSE(copied.sample());
  CHECK(replacement.states().value() == moved.states().value());
}

TEST_CASE("animation_layers.rejects_invalid_names_capacities_masks_and_clips") {
  const auto source = motion();
  LayerDefinition definition{"Valid", source, {{0, Path::Translation}}};

  for (const std::string name : {"", "1bad", "bad name", "bad/part"}) {
    definition.name = name;
    CHECK_FALSE(Layers::create(std::array{definition}));
  }

  definition.name = "Valid";
  CHECK_FALSE(Layers::create(std::array{definition, definition}));
  auto invalid = definition;
  invalid.mask = {{9, Path::Translation}};
  CHECK_FALSE(Layers::create(std::array{invalid}));
  invalid.mask = {{0, Path::Translation}, {0, Path::Translation}};
  CHECK_FALSE(Layers::create(std::array{invalid}));
  invalid.mask = {};
  CHECK_FALSE(Layers::create(std::array{invalid}));
  invalid = definition;
  invalid.weight = 2;
  CHECK_FALSE(Layers::create(std::array{invalid}));
  invalid = definition;
  invalid.clip = {};
  CHECK_FALSE(Layers::create(std::array{invalid}));
  std::vector<LayerDefinition> many;

  for (int i = 0; i < 9; ++i) {
    many.push_back({"Layer" + std::to_string(i), source, {{0, Path::Translation}}});
  }

  CHECK_FALSE(Layers::create(many));
  many.pop_back();
  CHECK(Layers::create(many));
  invalid = definition;
  invalid.name = std::string(64, 'A');
  CHECK(Layers::create(std::array{invalid}));
  invalid.name.push_back('A');
  CHECK_FALSE(Layers::create(std::array{invalid}));
  invalid = definition;
  invalid.mask.resize(30001, {0, Path::Translation});
  CHECK_FALSE(Layers::create(std::array{invalid}));
  CHECK(Layers::create({})->sample()->empty());
}

TEST_CASE("animation_layers.zero_duration_clips_hold_and_still_complete_weight_fades") {
  auto clip
      = Clip::create({"Hold", {{0, Path::Translation, Interpolation::Linear, {{0, {4, 0, 0, 0}}}}}})
            .value();
  const std::array definitions{
      LayerDefinition{"Hold", clip, {{0, Path::Translation}}, 1, -1, false, true}};
  auto layers = Layers::create(definitions).value().advanced(1).value();
  CHECK_FALSE(layers.states()->front().playback.playing);
  CHECK(layers.states()->front().playback.active);
  CHECK(layers.sample()->front().channel.value.x == 4);
  layers = layers.updated("Hold", FadeLayer{0, .5, true}).value().advanced(.5).value();
  CHECK(layers.sample()->empty());
  CHECK_FALSE(layers.states()->front().playback.active);
}

TEST_CASE(
    "animation_layers.additive_reference_is_fixed_owned_and_validated_only_for_masked_tracks") {
  LayerDefinition definition{"Offset", motion(), {{0, Path::Translation}}, 1, 1,
                             false,    true,     LayerMode::Additive,      1};
  auto layers = Layers::create(std::array{definition}).value();
  CHECK(layers.states()->front().mode == LayerMode::Additive);
  CHECK(layers.states()->front().referenceTime == 1);
  const auto before = layers.sample().value();
  REQUIRE(before.size() == 1);
  REQUIRE(before.front().reference);
  CHECK(before.front().reference->x == 2);
  CHECK(before.front().channel.value.x == 0);
  definition.clip = {};
  auto next = layers.advanced(2).value();
  CHECK(next.sample()->front().channel.value.x == 4);
  CHECK(next.sample()->front().reference->x == 2);
  auto moved = std::move(next);
  CHECK_FALSE(next.sample());
  CHECK(moved.sample()->front().reference->x == 2);
  CHECK(before.front().reference->x == 2);
  definition.clip = motion();
  definition.referenceTime = 2;
  CHECK(Layers::create(std::array{definition}));

  for (double time : {-1., 2.01, std::numeric_limits<double>::infinity()}) {
    definition.referenceTime = time;
    CHECK_FALSE(Layers::create(std::array{definition}));
  }

  definition.referenceTime = 1;
  definition.mask = {{1, Path::Scale, 0}};
  CHECK_FALSE(Layers::create(std::array{definition}));
  definition.referenceTime = 0;
  CHECK(Layers::create(std::array{definition}));
  definition.mode = static_cast<LayerMode>(99);
  CHECK_FALSE(Layers::create(std::array{definition}));
  CHECK(layerModeName(definition.mode) == "unknown");
  definition.mode = LayerMode::Override;
  definition.referenceTime = .5;
  CHECK_FALSE(Layers::create(std::array{definition}));
  definition.referenceTime = 0;
  CHECK(Layers::create(std::array{definition})->sample()->empty());
}
