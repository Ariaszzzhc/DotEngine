/** GPU convolution: diffuse stores E/pi; specular stores normalized GGX radiance.
 * GPU 卷积：漫反射存储 E/pi，镜面反射存储归一化 GGX 辐射。 */
struct Params {
  mode: u32,
  side: u32,
  samples: u32,
  levels: u32,
  roughness: f32,
  pad0: f32,
  pad1: f32,
  pad2: f32
};

@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var panorama: texture_2d<f32>;
@group(0) @binding(2) var output: texture_storage_2d_array<rgba16float, write>;
const pi: f32 = 3.141592653589793;

fn sequence(i: u32) -> vec2<f32> {
  return vec2<f32>(f32(i) / f32(params.samples), f32(reverseBits(i)) * 2.3283064365386963e-10);
}

fn faceDirection(face: u32, uv: vec2<f32>) -> vec3<f32> {
  let p = uv * 2.0 - 1.0;

  switch face {
    case 0u: {
      return normalize(vec3<f32>(1, -p.y, -p.x));
    }

    case 1u: {
      return normalize(vec3<f32>(-1, -p.y, p.x));
    }

    case 2u: {
      return normalize(vec3<f32>(p.x, 1, p.y));
    }

    case 3u: {
      return normalize(vec3<f32>(p.x, -1, -p.y));
    }

    case 4u: {
      return normalize(vec3<f32>(p.x, -p.y, 1));
    }

    default: {
      return normalize(vec3<f32>(-p.x, -p.y, -1));
    }
  }
}

fn texel(p: vec2<i32>, level: i32) -> vec3<f32> {
  let size = vec2<i32>(textureDimensions(panorama, u32(level)));
  return textureLoad(panorama,
    vec2<i32>(((p.x % size.x) + size.x) % size.x, clamp(p.y, 0, size.y - 1)), level).rgb;
}

fn bilinear(direction: vec3<f32>, level: i32) -> vec3<f32> {
  var longitude = 0.0;

  if (dot(direction.xz, direction.xz) > 0.000000000001) {
    longitude = atan2(direction.x, -direction.z);
  }

  let uv = vec2<f32>(longitude / (2.0 * pi) + 0.5, acos(clamp(direction.y, -1.0, 1.0)) / pi);
  let p = uv * vec2<f32>(textureDimensions(panorama, u32(level))) - 0.5;
  let first = vec2<i32>(floor(p));
  let f = fract(p);
  return mix(
    mix(texel(first, level), texel(first + vec2<i32>(1, 0), level), f.x),
    mix(texel(first + vec2<i32>(0, 1), level), texel(first + vec2<i32>(1, 1), level), f.x), f.y);
}

fn radiance(direction: vec3<f32>, lod: f32) -> vec3<f32> {
  let level = clamp(lod, 0.0, f32(params.levels - 1u));
  let lower = i32(floor(level));
  return mix(bilinear(direction, lower),
    bilinear(direction, min(lower + 1, i32(params.levels) - 1)), fract(level));
}

fn oriented(local: vec3<f32>, normal: vec3<f32>) -> vec3<f32> {
  let axis = select(vec3<f32>(0, 0, 1), vec3<f32>(1, 0, 0), abs(normal.z) > 0.99);
  let tangent = normalize(cross(axis, normal));
  let bitangent = cross(normal, tangent);
  return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

fn ggxHalf(xi: vec2<f32>, roughness: f32) -> vec3<f32> {
  let a = roughness * roughness;
  let a2 = a * a;
  let z = sqrt((1.0 - xi.y) / (1.0 + (a2 - 1.0) * xi.y));
  let r = sqrt(max(0.0, 1.0 - z * z));
  let phi = 2.0 * pi * xi.x;
  return vec3<f32>(r * cos(phi), r * sin(phi), z);
}

fn sampleLod(direction: vec3<f32>, pdf: f32) -> f32 {
  let size = textureDimensions(panorama);
  let texelAngle = 2.0 * pi * pi
    * max(sqrt(max(0.0, 1.0 - direction.y * direction.y)), 0.0001) / (f32(size.x) * f32(size.y));
  return 0.5 * log2(1.0 / (f32(params.samples) * max(pdf, 0.000001) * texelAngle));
}

fn brdf(uv: vec2<f32>) -> vec3<f32> {
  let nv = max(uv.x, 0.00001);
  let roughness = uv.y;
  let a = roughness * roughness;
  let a2 = a * a;
  let v = vec3<f32>(sqrt(max(0.0, 1.0 - nv * nv)), 0, nv);
  var sum = vec2<f32>(0);

  for (var i = 0u; i < params.samples; i++) {
    let h = ggxHalf(sequence(i), roughness);
    let vh = max(dot(v, h), 0.0);
    let l = 2.0 * vh * h - v;
    let nl = max(l.z, 0.0);
    let nh = max(h.z, 0.00001);

    if (nl > 0.0) {
      let vis = 0.5
        / max(nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2),
          0.000001);
      let weight = 4.0 * vis * nl * vh / nh;
      let fresnel = pow(1.0 - vh, 5.0);
      sum += vec2<f32>(1.0 - fresnel, fresnel) * weight;
    }
  }

  return vec3<f32>(sum / f32(params.samples), 0);
}

@compute @workgroup_size(8, 8, 1)
fn bake(@builtin(global_invocation_id) id: vec3<u32>) {
  if (id.x >= params.side || id.y >= params.side || (params.mode == 3u && id.z > 0u)) {
    return;
  }

  let uv = (vec2<f32>(id.xy) + 0.5) / f32(params.side);
  var value = vec3<f32>(0);

  if (params.mode == 3u) {
    value = brdf(uv);
  } else {
    let n = faceDirection(id.z, uv);

    if (params.mode == 0u) {
      let size = textureDimensions(panorama);
      value = radiance(n,
        0.5 * log2(f32(size.x) * f32(size.y) / (3.0 * pi * f32(params.side * params.side))));
    } else if (params.mode == 2u && params.roughness == 0.0) {
      value = radiance(n, 0);
    } else {
      var weight = 0.0;

      for (var i = 0u; i < params.samples; i++) {
        let xi = sequence(i);

        if (params.mode == 1u) {
          let phi = 2.0 * pi * xi.x;
          let r = sqrt(xi.y);
          let l = oriented(vec3<f32>(r * cos(phi), r * sin(phi), sqrt(1.0 - xi.y)), n);
          let nl = max(dot(n, l), 0.00001);
          value += radiance(l, sampleLod(l, nl / pi));
          weight += 1.0;
        } else {
          let h = oriented(ggxHalf(xi, params.roughness), n);
          let nh = max(dot(n, h), 0.00001);
          let l = normalize(2.0 * nh * h - n);
          let nl = max(dot(n, l), 0.0);

          if (nl > 0.0) {
            let a = params.roughness * params.roughness;
            let a2 = a * a;
            let d = a2 / (pi * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
            value += radiance(l, sampleLod(l, d / 4.0)) * nl;
            weight += nl;
          }
        }
      }

      value /= max(weight, 0.00001);
    }
  }

  textureStore(output, vec2<i32>(id.xy), i32(id.z), vec4<f32>(value, 1));
}
