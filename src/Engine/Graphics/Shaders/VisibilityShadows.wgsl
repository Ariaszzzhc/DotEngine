struct Shadows {
  matrices: array<mat4x4f, 4>,
  splits: vec4f,
  origin: vec4f,
  forward: vec4f,
  bias: vec4f,
  mapInfo: vec4f
}

@group(3) @binding(0) var<uniform> shadows: Shadows;
@group(3) @binding(1) var shadowMap: texture_depth_2d_array;
@group(3) @binding(2) var shadowSampler: sampler_comparison;

/** Sixteen comparison taps plus hardware bilinear PCF soften depth-map edges.
 * Each layer has independent storage and coverage; out-of-range depth is lit.
 * 十六次深度比较及硬件双线性 PCF 柔化边缘；各层独立保存范围，深度范围外明确不遮挡。 */
fn cascadeVisibility(cascade: u32, position: vec3f, normal: vec3f, noL: f32,
  dp1: vec3f, dp2: vec3f) -> f32 {
  let projected = shadows.matrices[cascade] * vec4f(position + normal * shadows.bias.w, 1.0);
  let coordinate = projected.xyz / projected.w;

  if (coordinate.z < 0.0 || coordinate.z > 1.0 || any(abs(coordinate.xy) > vec2f(1.0))) {
    return 1.0;
  }

  let uv = coordinate.xy * vec2f(0.5, -0.5) + 0.5;
  let dx = (shadows.matrices[cascade] * vec4f(dp1, 0.0)).xyz * vec3f(0.5, -0.5, 1.0);
  let dy = (shadows.matrices[cascade] * vec4f(dp2, 0.0)).xyz * vec3f(0.5, -0.5, 1.0);
  let determinant = dx.x * dy.y - dx.y * dy.x;
  var gradient = vec2f(0.0);

  if (abs(determinant) > 0.000000000001) {
    gradient = vec2f(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z) / determinant;
  }

  /** Bilinear comparison can reach a full texel from the fractional tap center.
   * 双线性比较的邻点与分数纹素采样位置最远相隔一个纹素，须包含该平面深度差。 */
  let planeBias = dot(abs(gradient), vec2f(1.0 / shadows.mapInfo.y));
  let reference = coordinate.z - shadows.bias.x - shadows.bias.y * (1.0 - noL) - planeBias;

  if (shadows.bias.z == 0.0) {
    return textureSampleCompareLevel(shadowMap, shadowSampler, uv, i32(cascade), reference);
  }
  var visibility = 0.0;

  for (var y = 0; y < 4; y++) {
    for (var x = 0; x < 4; x++) {
      let offset = (vec2f(f32(x), f32(y)) - 1.5) / 1.5 * shadows.bias.z / shadows.mapInfo.y;
      visibility += textureSampleCompareLevel(shadowMap, shadowSampler, uv + offset,
        i32(cascade), reference + dot(gradient, offset));
    }
  }

  return visibility / 16.0;
}

fn lightVisibility(index: u32, position: vec3f, normal: vec3f, noL: f32,
  dp1: vec3f, dp2: vec3f) -> f32 {
  if (index != u32(shadows.mapInfo.x) || noL <= 0.0) {
    return 1.0;
  }

  let depth = dot(position - shadows.origin.xyz, shadows.forward.xyz);
  let count = u32(shadows.mapInfo.z);

  if (depth < shadows.origin.w || depth > shadows.splits[count - 1u]) {
    return 1.0;
  }

  var cascade = 0u;

  for (var layer = 0u; layer + 1u < count; layer++) {
    if (depth > shadows.splits[layer]) {
      cascade = layer + 1u;
    }
  }

  let visibility = cascadeVisibility(cascade, position, normal, noL, dp1, dp2);

  if (cascade + 1u == count) {
    return visibility;
  }

  var near = shadows.origin.w;

  if (cascade > 0u) {
    near = shadows.splits[cascade - 1u];
  }

  let far = shadows.splits[cascade];
  let blend = smoothstep(far - (far - near) * 0.1, far, depth);

  if (blend == 0.0) {
    return visibility;
  }

  return mix(visibility,
    cascadeVisibility(cascade + 1u, position, normal, noL, dp1, dp2), blend);
}
