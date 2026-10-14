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
@group(0) @binding(3) var output: texture_storage_2d<rgba16float, write>;

fn world(pixel: vec2i, depth: f32) -> vec3f {
  let uv = (vec2f(pixel) + 0.5) / vec2f(textureDimensions(depths));
  let point = settings.inverse * vec4f(uv * vec2f(2.0, -2.0) + vec2f(-1.0, 1.0), depth, 1.0);
  return point.xyz / point.w;
}

/** Normal-aware radial horizon search uses bounded world distance, not depth-buffer
 * subtraction. Pixel rotation and quadratic steps distribute near-field samples.
 * 法线感知径向地平线搜索以有界世界距离判断遮挡，不直接相减设备深度；像素旋转与二次步长
 * 分布近场采样。屏幕外或无几何信息的方向不虚构遮挡。 */
@compute @workgroup_size(8, 8, 1)
fn compute(@builtin(global_invocation_id) id: vec3u) {
  let size = vec2i(textureDimensions(depths));
  let pixel = vec2i(id.xy);

  if (any(pixel >= size)) {
    return;
  }

  let depth = textureLoad(depths, pixel, 0);
  let encoded = textureLoad(normals, pixel, 0);
  var visibility = 1.0;

  if (depth < 1.0 && encoded.w > 0.5 && dot(encoded.xyz, encoded.xyz) > 0.5) {
    let position = world(pixel, depth);
    let normal = normalize(encoded.xyz);
    let radius = settings.values.x;
    let center = settings.viewProjection * vec4f(position, 1.0);
    let edgeX = settings.viewProjection * vec4f(position + settings.right.xyz * radius, 1.0);
    let edgeY = settings.viewProjection * vec4f(position + settings.up.xyz * radius, 1.0);
    let radiusPixels = vec2f(
      length((edgeX.xy / edgeX.w - center.xy / center.w) * 0.5 * vec2f(size)),
      length((edgeY.xy / edgeY.w - center.xy / center.w) * 0.5 * vec2f(size)));
    let noise = fract(f32((id.x & 3u) * 7u + (id.y & 3u) * 13u) * 0.61803398875);
    var sum = 0.0;

    for (var direction = 0u; direction < settings.work.x; direction++) {
      let angle = (f32(direction) + noise) * 6.28318530718 / f32(settings.work.x);
      let ray = vec2f(cos(angle), sin(angle));
      var horizon = 0.0;

      for (var step = 0u; step < settings.work.y; step++) {
        let fraction = (f32(step) + 0.5) / f32(settings.work.y);
        let samplePixel = pixel + vec2i(round(ray * radiusPixels * fraction * fraction));

        if (any(samplePixel < vec2i(0)) || any(samplePixel >= size)) {
          continue;
        }

        let sampleDepth = textureLoad(depths, samplePixel, 0);

        if (sampleDepth >= 1.0) {
          continue;
        }

        let delta = world(samplePixel, sampleDepth) - position;
        let distance = length(delta);

        if (distance > 0.00001 && distance < radius) {
          let angleWeight = max((dot(normal, delta) - settings.values.y) / distance, 0.0);
          let falloff = 1.0 - smoothstep(0.0, 1.0, distance / radius);
          horizon = max(horizon, angleWeight * angleWeight * falloff);
        }
      }

      sum += horizon;
    }

    if (settings.values.z != 0.0) {
      visibility = pow(clamp(1.0 - sum / f32(settings.work.x), 0.0, 1.0), settings.values.z);
    }
  }

  textureStore(output, pixel, vec4f(visibility, visibility, visibility, 1.0));
}
