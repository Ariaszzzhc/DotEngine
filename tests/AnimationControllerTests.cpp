#include <doctest/doctest.h>

#include <array>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import Dot.Animation.Controller;
import Dot.Animation.ControllerFiles;

namespace {
  using namespace Dot::Animation;

  auto description() -> ControllerDescription {
    return {"Movement",
            "Idle",
            {{"Speed", 0.0}, {"Grounded", true}},
            {{"Idle", "assets/idle.dotanimation"},
             {"Walk", "assets/walk.dotanimation"},
             {"Air", "assets/air.dotanimation", -1, false}},
            {{"Jump", "Idle", "Air", {{"Grounded", ConditionOp::Equal, false}}, .06},
             {"Move", "Idle", "Walk", {{"Speed", ConditionOp::GreaterEqual, .15}}, .12},
             {"Stop", "Walk", "Idle", {{"Speed", ConditionOp::Less, .15}}, .12},
             {"Land", "Air", "Idle", {{"Grounded", ConditionOp::Equal, true}}, .12, .1, .8}}};
  }
}  // namespace

TEST_CASE("animation_controller.ordered_typed_candidates_and_owned_observations") {
  auto model = Controller::create(description()).value();
  auto changed = model
                     .withParameters(std::array{ControllerParameter{"Speed", 3.0},
                                                ControllerParameter{"Grounded", false}})
                     .value();
  auto jumped = changed.advanced(.02, 0, 1).value();
  CHECK(jumped.state().value()->name == "Air");
  CHECK(jumped.snapshot()->lastTransition == "Jump");
  CHECK(jumped.snapshot()->elapsedSeconds == .02);
  CHECK(jumped.snapshot()->transitions == 1);
  CHECK(model.snapshot()->parameters.front().value == ParameterValue{0.0});
  CHECK(model.state().value()->name == "Idle");
  CHECK(changed.state().value()->name == "Idle");
  auto snapshot = jumped.snapshot().value();
  snapshot.state = "Unknown";
  snapshot.parameters.front().value = 999.0;
  CHECK(jumped.state().value()->name == "Air");
  CHECK(jumped.snapshot()->parameters.front().value == ParameterValue{3.0});
  CHECK(jumped.advanced(0, .5, 1)->snapshot()->transitions == 1);
}

TEST_CASE("animation_controller.exit_time_is_directional_and_delay_uses_simulation_seconds") {
  auto model = Controller::create(description())
                   .value()
                   .withParameters(std::array{ControllerParameter{"Grounded", false}})
                   .value()
                   .advanced(.02, 0, 1)
                   .value()
                   .withParameters(std::array{ControllerParameter{"Grounded", true}})
                   .value();
  CHECK(model.advanced(.08, .1, 1)->state().value()->name == "Air");
  model = model.advanced(.08, .1, 1).value();
  CHECK(model.snapshot()->elapsedSeconds == doctest::Approx(.1));
  CHECK(model.advanced(.02, .3, 1)->state().value()->name == "Air");
  auto landed = model.advanced(.02, .1, 1).value();
  CHECK(landed.state().value()->name == "Idle");
  CHECK(landed.snapshot()->lastTransition == "Land");
  CHECK(landed.snapshot()->elapsedSeconds == .02);
  CHECK(landed.snapshot()->transitions == 2);
  CHECK(model.advanced(.02, 0, 0)->state().value()->name == "Idle");
}

TEST_CASE("animation_controller.cycles_have_one_edge_per_tick_and_no_zero_delta_effects") {
  auto spec = description();
  spec.transitions = {{"ToWalk", "Idle", "Walk", {}, 0}, {"ToIdle", "Walk", "Idle", {}, 0}};
  auto model = Controller::create(spec).value();
  CHECK(model.advanced(0, 0, 0)->snapshot()->transitions == 0);
  model = model.advanced(.1, 0, 0).value();
  CHECK(model.state().value()->name == "Walk");
  CHECK(model.snapshot()->transitions == 1);
  model = model.advanced(.1, 0, 0).value();
  CHECK(model.state().value()->name == "Idle");
  CHECK(model.snapshot()->transitions == 2);
}

TEST_CASE("animation_controller.bad_parameter_batches_and_clocks_leave_candidates_intact") {
  const auto model = Controller::create(description()).value();

  for (const auto& batch :
       {std::vector<ControllerParameter>{{"Speed", true}},
        std::vector<ControllerParameter>{{"Grounded", 1.0}},
        std::vector<ControllerParameter>{{"Missing", false}},
        std::vector<ControllerParameter>{{"Speed", 1.0}, {"Speed", 2.0}},
        std::vector<ControllerParameter>{{"Speed", 1.0}, {"Grounded", 0.0}},
        std::vector<ControllerParameter>{{"Speed", std::numeric_limits<double>::infinity()}},
        std::vector<ControllerParameter>{{"Speed", 1000001.0}}}) {
    CHECK_FALSE(model.withParameters(batch));
    CHECK(model.snapshot()->parameters.front().value == ParameterValue{0.0});
  }

  CHECK_FALSE(model.advanced(-.1, 0, 1));
  CHECK_FALSE(model.advanced(1.1, 0, 1));
  CHECK_FALSE(model.advanced(std::numeric_limits<double>::quiet_NaN(), 0, 1));
  CHECK_FALSE(model.advanced(.1, 2, 1));
  CHECK_FALSE(model.advanced(.1, 0, -1));
  CHECK_FALSE(model.advanced(.1, 0, 86401));
  CHECK(model.snapshot()->elapsedSeconds == 0);
}

TEST_CASE("animation_controller.validation_rejects_ill_typed_graphs_and_capacity_edges") {
  const auto original = description();
  auto spec = original;
  spec.initial = "Missing";
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.parameters.push_back(spec.parameters.front());
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.states.front().motion = "../clip.dotanimation";
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.states.front().speed = 17;
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.front().conditions.front().operation = ConditionOp::Less;
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.front().conditions.front().value = 1.0;
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.front().to = "Missing";
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.front().fadeSeconds = -1;
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.front().exitTime = 1.01;
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.front().conditions.resize(9);
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.states.clear();
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.states.resize(65);
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.parameters.resize(65);
  CHECK_FALSE(Controller::create(spec));
  spec = original;
  spec.transitions.resize(513);
  CHECK_FALSE(Controller::create(spec));
}

TEST_CASE("animation_controller.copy_move_reset_keep_configuration_and_retire_sources") {
  auto original = Controller::create(description()).value();
  auto copy = original;
  copy = copy.withParameters(std::array{ControllerParameter{"Speed", 1.0}})
             .value()
             .advanced(.1, 0, 1)
             .value();
  auto reset = copy.reset().value();
  CHECK(reset.state().value()->name == "Idle");
  CHECK(reset.snapshot()->transitions == 0);
  CHECK(reset.snapshot()->elapsedSeconds == 0);
  CHECK(reset.snapshot()->parameters.front().value == ParameterValue{0.0});
  auto moved = std::move(copy);
  CHECK_FALSE(copy.state());
  CHECK_FALSE(copy.description());
  CHECK_FALSE(copy.snapshot());
  CHECK_FALSE(copy.advanced(.1, 0, 1));
  CHECK_FALSE(copy.withParameters({}));
  CHECK_FALSE(copy.reset());
  copy = moved;
  CHECK(copy.state().value()->name == "Walk");
  auto* alias = &copy;
  copy = *alias;
  copy = std::move(*alias);
  CHECK(copy.state().value()->name == "Walk");
  CHECK(original.state().value()->name == "Idle");
}

TEST_CASE("animation_controller.maximum_definition_budgets_compile_and_select_one_edge") {
  ControllerDescription spec{"有类型的状态机", "S0", {}, {}, {}};

  for (int i = 0; i < 64; ++i) {
    spec.parameters.push_back({"P" + std::to_string(i), 0.0});
    spec.states.push_back({"S" + std::to_string(i), "assets/motion.dotanimation"});
  }

  for (int i = 0; i < 512; ++i) {
    ControllerTransition edge{"T" + std::to_string(i), "S0", "S1", {}};

    for (int condition = 0; condition < 8; ++condition) {
      edge.conditions.push_back({"P0", ConditionOp::GreaterEqual, 0.0});
    }

    spec.transitions.push_back(std::move(edge));
  }

  auto model = Controller::create(spec);
  REQUIRE(model);
  CHECK(model->advanced(.1, 0, 1)->snapshot()->lastTransition == "T0");
  auto invalid = spec;
  invalid.parameters.push_back({"P64", 0.0});
  CHECK_FALSE(Controller::create(invalid));
  invalid = spec;
  invalid.states.push_back({"S64", "assets/motion.dotanimation"});
  CHECK_FALSE(Controller::create(invalid));
  invalid = spec;
  invalid.transitions.push_back({"Extra", "S0", "S1", {}});
  CHECK_FALSE(Controller::create(invalid));
  const auto name = std::string(64, 'A');
  ControllerDescription named{"Named", name, {}, {{name, "assets/motion.dotanimation"}}, {}};
  CHECK(Controller::create(named));
  named.states[0].name.push_back('A');
  named.initial = named.states[0].name;
  CHECK_FALSE(Controller::create(named));
}

TEST_CASE("animation_controller.codec_round_trips_definitions_without_runtime_state") {
  auto model = Controller::create(description()).value();
  const auto encoded = encodeController(model).value();
  const auto decoded = decodeController(encoded).value();
  CHECK(decoded.description().value()->name == "Movement");
  CHECK(*decoded.description().value() == *model.description().value());
  model = model.withParameters(std::array{ControllerParameter{"Speed", 2.0}})
              .value()
              .advanced(.1, 0, 1)
              .value();
  CHECK(model.state().value()->name == "Walk");
  CHECK(encodeController(model).value() == encoded);
  CHECK(decodeController(encoded)->state().value()->name == "Idle");
  const auto values = decodeControllerParameters(R"({"Speed":3,"Grounded":false})").value();
  CHECK(model.withParameters(values)->snapshot()->parameters[0].value == ParameterValue{3.0});

  for (const auto* json : {R"({"Speed":true,"Speed":false})", R"({"Speed":"3"})",
                           R"({"Speed":null})", R"({"Speed":1000001})", "[]"}) {
    CHECK_FALSE(decodeControllerParameters(json));
  }

  CHECK_FALSE(decodeControllerParameters(std::string(16 * 1024 + 1, ' ')));
  auto json = nlohmann::json::parse(encoded);
  json["runtime"] = "Walk";
  CHECK_FALSE(decodeController(json.dump()));
  json.erase("runtime");
  json["states"][0]["speed"] = true;
  CHECK_FALSE(decodeController(json.dump()));
  json = nlohmann::json::parse(encoded);
  json["transitions"][0]["conditions"][0]["value"] = 0;
  CHECK_FALSE(decodeController(json.dump()));
  json = nlohmann::json::parse(encoded);
  json["transitions"][0]["conditions"][0]["operation"] = "less";
  CHECK_FALSE(decodeController(json.dump()));
  CHECK_FALSE(decodeController(R"({"name":"A","name":"B"})"));
  CHECK_FALSE(decodeController(std::string(1024 * 1024 + 1, ' ')));
  CHECK_FALSE(decodeController(std::string("{}\0ignored", 10)));
}

TEST_CASE("animation_controller.exit_thresholds_include_loop_wrap_and_reverse_crossings") {
  auto spec = description();
  spec.transitions = {{"End", "Idle", "Walk", {}, 0, 0, 1}};
  auto model = Controller::create(spec).value();
  CHECK(model.advanced(.1, .85, 1)->state().value()->name == "Idle");
  CHECK(model.advanced(.2, .85, 1)->state().value()->name == "Walk");
  spec.states.front().speed = -2;
  model = Controller::create(spec).value();
  CHECK(model.advanced(.05, .2, 1)->state().value()->name == "Idle");
  CHECK(model.advanced(.1, .2, 1)->state().value()->name == "Walk");
  CHECK(model.advanced(0, 0, 1)->state().value()->name == "Idle");
}

TEST_CASE("animation_controller.blend_motion_is_typed_immutable_and_round_trips_strict_forms") {
  auto spec = description();
  spec.states.front().motion = ControllerBlend1D{
      "Speed", {{0, "assets/idle.dotanimation"}, {2, "assets/walk.dotanimation", .5}}};
  const auto graph = Controller::create(spec).value();
  CHECK(std::get<double>(graph.parameter("Speed").value()) == 0);
  CHECK_FALSE(graph.parameter("Unknown"));
  const auto text = encodeController(graph).value();
  CHECK(decodeController(text)->description().value()->states == spec.states);
  const auto changed = graph.withParameters(std::array{ControllerParameter{"Speed", 1.0}}).value();
  CHECK(std::get<double>(changed.parameter("Speed").value()) == 1);
  CHECK(std::get<double>(graph.parameter("Speed").value()) == 0);
  auto form = nlohmann::json::parse(text);
  form["states"][0]["motion"]["points"][0]["extra"] = true;
  CHECK_FALSE(decodeController(form.dump()));
  form = nlohmann::json::parse(text);
  form["states"][0]["motion"]["points"][0]["phaseOffset"] = true;
  CHECK_FALSE(decodeController(form.dump()));
  form = nlohmann::json::parse(text);
  form["states"][0]["motion"]["kind"] = "unknown";
  CHECK_FALSE(decodeController(form.dump()));
  form = nlohmann::json::parse(text);
  form["states"][0]["clip"] = "assets/idle.dotanimation";
  CHECK_FALSE(decodeController(form.dump()));
}

TEST_CASE("animation_controller.blend_axes_points_and_asset_boundaries_fail_before_creation") {
  auto spec = description();
  const ControllerBlend1D original{
      "Speed", {{0, "assets/idle.dotanimation"}, {2, "assets/walk.dotanimation"}}};
  auto invalid = original;
  invalid.parameter = "Grounded";
  spec.states[0].motion = invalid;
  CHECK_FALSE(Controller::create(spec));
  invalid.parameter = "Unknown";
  spec.states[0].motion = invalid;
  CHECK_FALSE(Controller::create(spec));
  invalid = original;
  invalid.points[1].threshold = 0;
  spec.states[0].motion = invalid;
  CHECK_FALSE(Controller::create(spec));
  invalid = original;
  invalid.points[1].phaseOffset = 1.1;
  spec.states[0].motion = invalid;
  CHECK_FALSE(Controller::create(spec));
  invalid = original;
  invalid.points[1].clip = "../missing.dotanimation";
  spec.states[0].motion = invalid;
  CHECK_FALSE(Controller::create(spec));
  invalid = original;
  invalid.points.resize(17, invalid.points.back());
  spec.states[0].motion = invalid;
  CHECK_FALSE(Controller::create(spec));
}
