@group(0) @binding(0) var<storage, read> transforms: array<mat4x4f>;

@vertex
fn vertexMain(@location(0) position: vec3f, @builtin(instance_index) instance: u32)
  -> @builtin(position) vec4f {
  return transforms[instance] * vec4f(position, 1.0);
}
