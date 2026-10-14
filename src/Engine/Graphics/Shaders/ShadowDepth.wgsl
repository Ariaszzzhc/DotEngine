struct CoverageData {
  transform: mat4x4f,
  tint: vec4f,
  flags: vec4f,
  alpha: vec4f,
  identifier: vec4u
}

@group(0) @binding(0) var<storage, read> instances: array<CoverageData>;
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var baseColorSampler: sampler;

struct Output {
  @builtin(position) position: vec4f,
  @location(0) color: vec4f,
  @location(1) uv: vec2f,
  @location(2) @interpolate(flat) instance: u32
}

@vertex
fn vertexMain(@location(0) position: vec3f, @location(1) color: vec4f,
  @location(2) uv: vec2f, @builtin(instance_index) instance: u32) -> Output {
  var output: Output;
  output.position = instances[instance].transform * vec4f(position, 1.0);
  output.color = color * instances[instance].tint;
  output.uv = uv;
  output.instance = instance;
  return output;
}

@fragment
fn fragmentMain(input: Output) {
  let alpha = input.color.a * textureSample(baseColorTexture, baseColorSampler, input.uv).a;

  if (!covered(alpha, instances[input.instance].alpha)) {
    discard;
  }
}
