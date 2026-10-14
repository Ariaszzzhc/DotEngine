@group(0) @binding(0) var<uniform> transform: mat4x4f;

@vertex
fn vertexMain(@location(0) position: vec3f) -> @builtin(position) vec4f {
  return transform * vec4f(position, 1.0);
}
