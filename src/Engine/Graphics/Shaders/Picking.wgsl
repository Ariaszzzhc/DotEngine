struct Camera {
  transform: mat4x4f,
  flags: vec4f,
  identifier: vec4u
}

@group(0) @binding(0) var<uniform> camera: Camera;

@vertex
fn vertexMain(@location(0) position: vec3f) -> @builtin(position) vec4f {
  return camera.transform * vec4f(position, 1.0);
}

/** Opaque coverage follows the surface pass's physical backface policy.
 * IDs are per-draw integers; the CPU retains full scene/entity identities.
 * 不透明覆盖遵循表面 pass 的物理背面策略；GPU ID 为绘制整数，CPU 保留完整场景/实体标识。 */
@fragment
fn fragmentMain(@builtin(front_facing) frontFacing: bool) -> @location(0) u32 {
  let front = frontFacing == (camera.flags.w > 0.0);

  if (camera.flags.x != 0.0 && !front && camera.flags.z == 0.0) {
    discard;
  }

  return camera.identifier.x;
}
