#pragma once

#include "../Serialization/Json.hpp"

namespace Dot::UI::Detail {
  using Json = Serialization::Detail::Json;

  template <class Vector>
  auto xy(const Json& value) -> Vector {
    Serialization::Detail::requireFields(value, {"x", "y"});
    return {Serialization::Detail::number(value["x"]), Serialization::Detail::number(value["y"])};
  }

  template <class Vector>
  auto rgba(const Json& value) -> Vector {
    Serialization::Detail::requireFields(value, {"x", "y", "z", "w"});
    return {Serialization::Detail::number(value["x"]), Serialization::Detail::number(value["y"]),
            Serialization::Detail::number(value["z"]), Serialization::Detail::number(value["w"])};
  }

  /** Shared strict native text-box wire shape for resources and transient HUDs.
   * Templates keep module-owned types outside global module fragments.
   * 资源与瞬时 HUD 共用的严格原生文字框格式；模板避免全局模块片段依赖模块类型。 */
  template <class Box>
  auto textBox(const Json& value, bool button = false) -> Box {
    if (button) {
      Serialization::Detail::requireFields(
          value, {"text", "position", "size", "fontSize", "padding", "color", "background",
                  "visible", "enabled"});
    } else {
      Serialization::Detail::requireFields(value, {"text", "position", "size", "fontSize",
                                                   "padding", "color", "background", "visible"});
    }

    return {value["text"].get<std::string>(),
            xy<decltype(Box::position)>(value["position"]),
            xy<decltype(Box::size)>(value["size"]),
            Serialization::Detail::number(value["fontSize"]),
            Serialization::Detail::number(value["padding"]),
            rgba<decltype(Box::color)>(value["color"]),
            rgba<decltype(Box::background)>(value["background"]),
            value["visible"].get<bool>()};
  }

  template <class Box>
  auto textJson(const Box& value) -> Json {
    return {
        {"text", value.text},
        {"position", {{"x", value.position.x}, {"y", value.position.y}}},
        {"size", {{"x", value.size.x}, {"y", value.size.y}}},
        {"fontSize", value.fontSize},
        {"padding", value.padding},
        {"color",
         {{"x", value.color.x}, {"y", value.color.y}, {"z", value.color.z}, {"w", value.color.w}}},
        {"background",
         {{"x", value.background.x},
          {"y", value.background.y},
          {"z", value.background.z},
          {"w", value.background.w}}},
        {"visible", value.visible}};
  }
}  // namespace Dot::UI::Detail
