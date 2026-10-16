struct Shadows {
  matrices: array<mat4x4f, 6>,
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
fn mapVisibility(cascade: u32, position: vec3f, normal: vec3f, noL: f32,
  dp1: vec3f, dp2: vec3f) -> f32 {
  let projected = shadows.matrices[cascade] * vec4f(position + normal * shadows.bias.w, 1.0);

  if (projected.w <= 0.0) {
    return 1.0;
  }

  let coordinate = projected.xyz / projected.w;

  if (coordinate.z < 0.0 || coordinate.z > 1.0 || any(abs(coordinate.xy) > vec2f(1.0))) {
    return 1.0;
  }

  let uv = coordinate.xy * vec2f(0.5, -0.5) + 0.5;
  /** Quotient-rule derivatives also cover perspective spot projection.
   * 商法则导数同时支持聚光的透视投影。 */
  let projectedDx = shadows.matrices[cascade] * vec4f(dp1, 0.0);
  let projectedDy = shadows.matrices[cascade] * vec4f(dp2, 0.0);
  let dx = (projectedDx.xyz - coordinate * projectedDx.w) / projected.w * vec3f(0.5, -0.5, 1.0);
  let dy = (projectedDy.xyz - coordinate * projectedDy.w) / projected.w * vec3f(0.5, -0.5, 1.0);
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

/** Cube layer selection and bases match the CPU +X,-X,+Y,-Y,+Z,-Z projections.
 * 立方体图层选择与基底匹配 CPU 的正负 X/Y/Z 投影。 */
fn pointFace(ray: vec3f) -> u32 {
  let size = abs(ray);

  if (size.x >= size.y && size.x >= size.z) {
    return select(1u, 0u, ray.x >= 0.0);
  }

  if (size.y >= size.z) {
    return select(3u, 2u, ray.y >= 0.0);
  }

  return select(5u, 4u, ray.z >= 0.0);
}

fn pointRay(face: u32, uv: vec2f) -> vec3f {
  let directions = array<vec3f, 6>(vec3f(1, 0, 0), vec3f(-1, 0, 0),
    vec3f(0, 1, 0), vec3f(0, -1, 0), vec3f(0, 0, 1), vec3f(0, 0, -1));
  let forward = directions[face];
  let up = select(vec3f(0, 1, 0), vec3f(0, 0, 1), abs(forward.y) > 0.99);
  let right = cross(forward, up);
  return forward + right * (uv.x * 2.0 - 1.0) + cross(right, forward) * (1.0 - uv.y * 2.0);
}

/** Remap each bilinear neighbor across cube faces, then intersect its actual ray
 * with the receiver plane. This avoids clamped seams and perspective depth errors.
 * 每个双线性邻点跨立方体面重映射，再将实际纹素射线与接收平面求交，避免钳位接缝及透视深度误差。 */
fn pointCompare(face: u32, pixel: vec2f, position: vec3f, plane: vec3f, bias: f32) -> f32 {
  let ray = pointRay(face, (pixel + 0.5) / shadows.mapInfo.y);
  let layer = pointFace(ray);
  let clip = shadows.matrices[layer] * vec4f(ray, 0.0);
  let uv = clip.xy / clip.w * vec2f(0.5, -0.5) + 0.5;
  let texel = clamp(vec2i(floor(uv * shadows.mapInfo.y)), vec2i(0),
    vec2i(i32(shadows.mapInfo.y) - 1));
  let sampleRay = pointRay(layer, (vec2f(texel) + 0.5) / shadows.mapInfo.y);
  let denominator = dot(sampleRay, plane);

  if (abs(denominator) < 0.00000001) {
    return 1.0;
  }

  let distance = dot(position - shadows.origin.xyz, plane) / denominator;

  if (distance <= 0.0) {
    return 1.0;
  }

  let projected = shadows.matrices[layer] * vec4f(shadows.origin.xyz + sampleRay * distance, 1.0);

  if (projected.w <= 0.0) {
    return 1.0;
  }

  let reference = projected.z / projected.w;

  if (reference < 0.0 || reference > 1.0) {
    return 1.0;
  }

  return select(0.0, 1.0, reference - bias <= textureLoad(shadowMap, texel, i32(layer), 0));
}

fn pointBilinear(face: u32, uv: vec2f, position: vec3f, plane: vec3f, bias: f32) -> f32 {
  let pixel = uv * shadows.mapInfo.y - 0.5;
  let base = floor(pixel);
  let fraction = fract(pixel);
  return mix(
    mix(pointCompare(face, base, position, plane, bias),
      pointCompare(face, base + vec2f(1, 0), position, plane, bias), fraction.x),
    mix(pointCompare(face, base + vec2f(0, 1), position, plane, bias),
      pointCompare(face, base + vec2f(1, 1), position, plane, bias), fraction.x), fraction.y);
}

fn pointVisibility(position: vec3f, normal: vec3f, noL: f32, dp1: vec3f, dp2: vec3f) -> f32 {
  let distance = length(position - shadows.origin.xyz);

  if (distance < shadows.origin.w || distance > shadows.splits.x) {
    return 1.0;
  }

  let receiver = position + normal * shadows.bias.w;

  if (length(receiver - shadows.origin.xyz) < shadows.origin.w) {
    return 1.0;
  }

  let face = pointFace(receiver - shadows.origin.xyz);
  let projected = shadows.matrices[face] * vec4f(receiver, 1.0);
  let uv = projected.xy / projected.w * vec2f(0.5, -0.5) + 0.5;
  let geometric = cross(dp1, dp2);
  var plane = normal;

  if (dot(geometric, geometric) > 0.00000000000000000001) {
    plane = normalize(geometric);
  }

  let bias = shadows.bias.x + shadows.bias.y * (1.0 - noL);

  if (shadows.bias.z == 0.0) {
    return pointBilinear(face, uv, receiver, plane, bias);
  }

  var visibility = 0.0;

  for (var y = 0; y < 4; y++) {
    for (var x = 0; x < 4; x++) {
      let offset = (vec2f(f32(x), f32(y)) - 1.5) / 1.5 * shadows.bias.z / shadows.mapInfo.y;
      visibility += pointBilinear(face, uv + offset, receiver, plane, bias);
    }
  }

  return visibility / 16.0;
}

fn lightVisibility(index: u32, position: vec3f, normal: vec3f, noL: f32,
  dp1: vec3f, dp2: vec3f) -> f32 {
  if (index != u32(shadows.mapInfo.x) || noL <= 0.0) {
    return 1.0;
  }

  if (shadows.mapInfo.w == 1.0) {
    return pointVisibility(position, normal, noL, dp1, dp2);
  }

  if (shadows.mapInfo.w == 2.0) {
    if (length(position - shadows.origin.xyz) > shadows.splits.x) {
      return 1.0;
    }

    return mapVisibility(0u, position, normal, noL, dp1, dp2);
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

  let visibility = mapVisibility(cascade, position, normal, noL, dp1, dp2);

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
    mapVisibility(cascade + 1u, position, normal, noL, dp1, dp2), blend);
}
