struct Settings {
  viewProjection: mat4x4f,
  inverse: mat4x4f,
  right: vec4f,
  up: vec4f,
  values: vec4f,
  work: vec4u
}

@group(0) @binding(0) var<uniform> settings: Settings;
@group(0) @binding(1) var depths: texture_depth_2d;
@group(0) @binding(2) var normals: texture_2d<f32>;
@group(0) @binding(3) var source: texture_2d<f32>;
@group(0) @binding(4) var output: texture_storage_2d<rgba8unorm, write>;

fn world(pixel: vec2i, depth: f32) -> vec3f {
  let uv = (vec2f(pixel) + 0.5) / vec2f(textureDimensions(depths));
  let point = settings.inverse * vec4f(uv * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), depth, 1.0);
  return point.xyz / point.w;
}

/** Spatial, world-position and geometric-normal weights stop filtered occlusion
 * from bleeding over silhouettes or into untouched background pixels.
 * 空间、世界位置及几何法线权重避免遮蔽跨轮廓扩散，也不污染无几何的背景像素。 */
@compute @workgroup_size(8, 8, 1)
fn filterOcclusion(@builtin(global_invocation_id) id: vec3u) {
  let size = vec2i(textureDimensions(depths));
  let pixel = vec2i(id.xy);

  if (any(pixel >= size)) {
    return;
  }

  let depth = textureLoad(depths, pixel, 0);
  let normal = textureLoad(normals, pixel, 0).xyz;
  var visibility = 1.0;

  if (depth < 1.0 && dot(normal, normal) > 0.5) {
    let center = world(pixel, depth);
    let radius = i32(settings.work.z);
    let worldSigma = max(settings.values.x * 0.2, 0.01);
    var sum = 0.0;
    var weight = 0.0;

    for (var y = -radius; y <= radius; y++) {
      for (var x = -radius; x <= radius; x++) {
        let neighbor = pixel + vec2i(x, y);

        if (any(neighbor < vec2i(0)) || any(neighbor >= size)) {
          continue;
        }

        let neighborDepth = textureLoad(depths, neighbor, 0);

        if (neighborDepth >= 1.0) {
          continue;
        }

        let n = textureLoad(normals, neighbor, 0).xyz;
        let normalWeight = pow(max(dot(normal, n), 0.0), 16.0);
        let delta = world(neighbor, neighborDepth) - center;
        let spatial = f32(x * x + y * y) / max(f32(radius * radius), 1.0);
        let w = normalWeight * exp(-dot(delta, delta) / (2.0 * worldSigma * worldSigma) - spatial);
        sum += textureLoad(source, neighbor, 0).x * w;
        weight += w;
      }
    }

    visibility = sum / max(weight, 0.00001);
  }

  textureStore(output, pixel, vec4f(visibility, visibility, visibility, 1.0));
}
