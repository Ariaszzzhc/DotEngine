/** ACES fit adapted from Stephen Hill's BakingLab implementation (MIT).
 * See packaged ACES-NOTICE.txt. Linear HDR is mapped and encoded only here.
 * ACES 拟合改编自 Stephen Hill 的 BakingLab 实现（MIT），许可随包提供；
 * 线性 HDR 仅在此映射及编码。 */
struct Settings {
  exposure: f32,
  toneMap: u32,
  encodeSrgb: u32,
  padding: u32
}

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var<uniform> settings: Settings;

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
  let positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  return vec4f(positions[index], 0.0, 1.0);
}

fn acesFitted(rgb: vec3f) -> vec3f {
  let inputMatrix = mat3x3f(
    vec3f(0.59719, 0.07600, 0.02840),
    vec3f(0.35458, 0.90834, 0.13383),
    vec3f(0.04823, 0.01566, 0.83777));
  let outputMatrix = mat3x3f(
    vec3f(1.60475, -0.10208, -0.00327),
    vec3f(-0.53108, 1.10813, -0.07276),
    vec3f(-0.07367, -0.00605, 1.07602));
  let v = inputMatrix * rgb;
  let a = v * (v + vec3f(0.0245786)) - vec3f(0.000090537);
  let b = v * (0.983729 * v + vec3f(0.432951)) + vec3f(0.238081);
  return clamp(outputMatrix * (a / b), vec3f(0.0), vec3f(1.0));
}

fn srgb(rgb: vec3f) -> vec3f {
  return select(1.055 * pow(rgb, vec3f(1.0 / 2.4)) - vec3f(0.055),
    12.92 * rgb, rgb <= vec3f(0.0031308));
}

@fragment
fn fragmentMain(@builtin(position) position: vec4f) -> @location(0) vec4f {
  let pixel = textureLoad(source, vec2i(position.xy), 0);
  var rgb = pixel.rgb * settings.exposure;

  if (settings.toneMap == 1u) {
    rgb = rgb / (vec3f(1.0) + rgb);
  } else if (settings.toneMap == 2u) {
    rgb = acesFitted(rgb);
  }

  rgb = clamp(rgb, vec3f(0.0), vec3f(1.0));

  if (settings.encodeSrgb == 1u) {
    rgb = srgb(rgb);
  }

  if (settings.encodeSrgb == 2u) {
    rgb = select(pow((rgb + vec3f(0.055)) / 1.055, vec3f(2.4)),
      rgb / 12.92, rgb <= vec3f(0.04045));
  }

  return vec4f(rgb, pixel.a);
}
