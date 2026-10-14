struct Sky {
  clipToWorld: mat4x4f,
  position: vec4f,
  direction: vec4f,
  values: vec4f
}

@group(0) @binding(0) var<uniform> sky: Sky;
@group(0) @binding(1) var radianceEnvironment: texture_cube<f32>;
@group(0) @binding(2) var skySampler: sampler;

struct Output {
  @builtin(position) position: vec4f,
  @location(0) clip: vec2f
}

@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> Output {
  let positions = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var output: Output;
  output.position = vec4f(positions[index], 1.0, 1.0);
  output.clip = positions[index];
  return output;
}

/** A point inside the clip-depth interval avoids infinite-far projection singularity.
 * 选用裁剪深度区间内部点，避免无限远投影的齐次奇点。 */
@fragment
fn fragmentMain(input: Output) -> @location(0) vec4f {
  let point = sky.clipToWorld * vec4f(input.clip, 0.5, 1.0);
  var ray = point.xyz / point.w - sky.position.xyz;

  if (sky.direction.w != 0.0) {
    ray = sky.direction.xyz;
  }

  let c = sky.values.y;
  let s = sky.values.z;
  let direction = normalize(vec3f(c * ray.x - s * ray.z, ray.y, s * ray.x + c * ray.z));
  let radiance = textureSampleLevel(radianceEnvironment, skySampler, direction, 0.0).rgb;
  return vec4f(clamp(radiance * sky.values.x, vec3f(0.0), vec3f(65504.0)), 1.0);
}
