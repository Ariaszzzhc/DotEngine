/** Packed engine vertices use sixteen contiguous floats, without vec3 padding.
 * 引擎顶点是十六个连续浮点数，不使用 vec3 隐式填充。 */
alias PackedVertex = array<f32, 16>;

struct Influence {
  firstJoints: vec4u,
  lastJoints: vec4u,
  firstWeights: vec4f,
  lastWeights: vec4f
}

@group(0) @binding(0) var<storage, read> sourceVertices: array<PackedVertex>;
@group(0) @binding(1) var<storage, read> influences: array<Influence>;
@group(0) @binding(2) var<storage, read> palette: array<mat4x4f>;
@group(0) @binding(3) var<storage, read_write> posedVertices: array<PackedVertex>;
@group(0) @binding(4) var<storage, read_write> report: array<atomic<u32>, 8>;
@group(0) @binding(5) var<storage, read> sourceIndices: array<u32>;
@group(0) @binding(6) var<uniform> parameters: vec4u;

fn largest(value: vec3f) -> f32 {
  return max(max(abs(value.x), abs(value.y)), abs(value.z));
}

fn finite(value: vec3f) -> bool {
  return all(abs(value) <= vec3f(3.4028234663852886e38));
}

fn unit(value: vec3f) -> vec3f {
  let scale = largest(value);

  if (scale == 0.0) {
    return vec3f(0.0);
  }

  let scaled = value / scale;
  return scaled * inverseSqrt(dot(scaled, scaled));
}

/** Monotonic integer keys let atomic min/max reduce signed finite positions.
 * 单调整数编码用于原子 min/max 归约有符号有限坐标。 */
fn ordered(value: f32) -> u32 {
  let bits = bitcast<u32>(value);
  return select(bits ^ 0x80000000u, ~bits, (bits & 0x80000000u) != 0u);
}

@compute @workgroup_size(64)
fn skinVertices(@builtin(global_invocation_id) invocation: vec3u) {
  let index = invocation.x;

  if (index >= arrayLength(&sourceVertices)) {
    return;
  }

  let influence = influences[index];
  var mixed: mat4x4f;

  for (var slot = 0u; slot < 8u; slot++) {
    var joint: u32;
    var weight: f32;

    if (slot < 4u) {
      joint = influence.firstJoints[slot];
      weight = influence.firstWeights[slot];
    } else {
      joint = influence.lastJoints[slot - 4u];
      weight = influence.lastWeights[slot - 4u];
    }

    if (weight > 0.0) {
      mixed += palette[joint] * weight;
    }
  }

  let scale = max(max(largest(mixed[0].xyz), largest(mixed[1].xyz)), largest(mixed[2].xyz));

  if (scale == 0.0) {
    atomicOr(&report[0], 1u);
    return;
  }

  let linear = mat3x3f(mixed[0].xyz / scale, mixed[1].xyz / scale, mixed[2].xyz / scale);
  let cofactor = mat3x3f(cross(linear[1], linear[2]), cross(linear[2], linear[0]),
    cross(linear[0], linear[1]));
  let determinant = dot(linear[0], cofactor[0]);

  if (abs(determinant) <= 0.000001) {
    atomicOr(&report[0], 1u);
    return;
  }

  let original = sourceVertices[index];
  var result = original;
  /** Scale before multiplication so overflow is rejected before it happens.
   * 乘法前缩放，先拒绝溢出，避免依赖驱动对非有限运算的传播行为。 */
  let inputPosition = vec3f(original[0], original[1], original[2]);
  let positionScale = max(1.0, largest(inputPosition));
  let scaledPosition = (mixed * vec4f(inputPosition / positionScale, 1.0 / positionScale)).xyz;

  if (any(abs(scaledPosition) > vec3f(3.4028234663852886e38 / positionScale))) {
    atomicOr(&report[0], 1u);
    return;
  }

  let position = scaledPosition * positionScale;
  let sourceNormal = vec3f(original[7], original[8], original[9]);
  var normal = vec3f(0.0);

  if (any(sourceNormal != vec3f(0.0))) {
    normal = unit(cofactor * sourceNormal) * sign(determinant);
  }

  var tangent = vec3f(0.0);

  if (original[15] != 0.0) {
    let direction = linear * vec3f(original[12], original[13], original[14]);
    tangent = unit(direction - normal * dot(normal, direction));

    if (dot(tangent, tangent) < 0.5) {
      atomicOr(&report[0], 1u);
      return;
    }
  }

  if (!finite(position) || !finite(normal) || !finite(tangent)) {
    atomicOr(&report[0], 1u);
    return;
  }

  result[0] = position.x;
  result[1] = position.y;
  result[2] = position.z;
  result[7] = normal.x;
  result[8] = normal.y;
  result[9] = normal.z;
  result[12] = tangent.x;
  result[13] = tangent.y;
  result[14] = tangent.z;
  result[15] = original[15] * sign(determinant);
  posedVertices[index] = result;
  atomicMin(&report[1], ordered(position.x));
  atomicMin(&report[2], ordered(position.y));
  atomicMin(&report[3], ordered(position.z));
  atomicMax(&report[4], ordered(position.x));
  atomicMax(&report[5], ordered(position.y));
  atomicMax(&report[6], ordered(position.z));
}

fn corner(index: u32) -> PackedVertex {
  if (parameters.x == 0u) {
    return posedVertices[index];
  }

  return posedVertices[sourceIndices[index]];
}

@compute @workgroup_size(64)
fn validateFaces(@builtin(global_invocation_id) invocation: vec3u) {
  let face = invocation.x;

  if (face >= parameters.y || atomicLoad(&report[0]) != 0u) {
    return;
  }

  let a = corner(face * 3u);
  let b = corner(face * 3u + 1u);
  let c = corner(face * 3u + 2u);

  if (a[15] != b[15] || a[15] != c[15]) {
    atomicOr(&report[0], 2u);
  }

  let pa = vec3f(a[0], a[1], a[2]);
  let pb = vec3f(b[0], b[1], b[2]);
  let pc = vec3f(c[0], c[1], c[2]);
  let magnitude = max(max(largest(pa), largest(pb)), largest(pc));

  if (magnitude == 0.0) {
    atomicOr(&report[7], 1u);
    return;
  }

  let first = pb / magnitude - pa / magnitude;
  let second = pc / magnitude - pa / magnitude;
  let edge = max(largest(first), largest(second));

  if (edge == 0.0 || largest(cross(first / edge, second / edge)) == 0.0) {
    atomicOr(&report[7], 1u);
  }
}
