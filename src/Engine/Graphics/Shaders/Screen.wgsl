struct Settings {
  rect: vec4f,
  hardwareSrgb: u32,
  a: u32,
  b: u32,
  c: u32
}

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var sampling: sampler;
@group(0) @binding(2) var<uniform> settings: Settings;

struct Output {
  @builtin(position) position: vec4f,
  @location(0) uv: vec2f
}

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> Output {
  let corners = array<vec2f, 6>(vec2f(0, 0), vec2f(1, 0), vec2f(0, 1),
    vec2f(0, 1), vec2f(1, 0), vec2f(1, 1));
  let uv = corners[index];
  let p = settings.rect.xy + uv * settings.rect.zw;
  return Output(vec4f(p.x * 2 - 1, 1 - p.y * 2, 0, 1), uv);
}

@fragment
fn fragmentMain(input: Output) -> @location(0) vec4f {
  var color = textureSample(source, sampling, input.uv);

  if (settings.hardwareSrgb == 1u) {
    color = vec4f(select(pow((color.rgb + vec3f(.055)) / 1.055, vec3f(2.4)),
      color.rgb / 12.92, color.rgb <= vec3f(.04045)), color.a);
  }

  return color;
}
