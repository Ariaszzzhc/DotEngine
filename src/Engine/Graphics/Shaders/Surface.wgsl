/** Metallic/roughness microfacet shading: GGX, correlated Smith visibility,
 * Schlick Fresnel and energy-split Lambert diffuse; all calculations are linear.
 * 金属度/粗糙度微表面着色使用 GGX、相关 Smith 可见性、Schlick 菲涅耳与能量分配的
 * Lambert 漫反射；所有计算在线性空间进行。 */
struct InstanceData {
  transform: mat4x4f,
  tint: vec4f,
  emissive: vec4f,
  model: mat4x4f,
  normalMatrix: mat4x4f,
  material: vec4f,
  flags: vec4f
}

struct Light {
  directionKind: vec4f,
  positionRange: vec4f,
  colorIntensity: vec4f,
  cone: vec4f
}

struct Lighting {
  viewPosition: vec4f,
  counts: vec4f,
  lights: array<Light, 64>
}

@group(0) @binding(0) var<storage, read> instances: array<InstanceData>;
@group(0) @binding(1) var baseColorTexture: texture_2d<f32>;
@group(0) @binding(2) var baseColorSampler: sampler;
@group(0) @binding(3) var metallicRoughnessTexture: texture_2d<f32>;
@group(0) @binding(4) var metallicRoughnessSampler: sampler;
@group(0) @binding(5) var normalTexture: texture_2d<f32>;
@group(0) @binding(6) var normalSampler: sampler;
@group(0) @binding(7) var emissiveTexture: texture_2d<f32>;
@group(0) @binding(8) var emissiveSampler: sampler;
@group(1) @binding(0) var<uniform> lighting: Lighting;

struct Output {
  @builtin(position) position: vec4f,
  @location(0) color: vec4f,
  @location(1) uv: vec2f,
  @location(2) worldPosition: vec3f,
  @location(3) normal: vec3f,
  @location(4) @interpolate(flat) instance: u32
}

fn unit(value: vec3f) -> vec3f {
  let scale = max(max(abs(value.x), abs(value.y)), abs(value.z));

  if (scale == 0.0) {
    return vec3f(0.0);
  }

  let scaled = value / scale;
  return scaled * inverseSqrt(dot(scaled, scaled));
}

@vertex
fn vertexMain(@location(0) position: vec3f, @location(1) color: vec4f,
  @location(2) uv: vec2f, @location(3) normal: vec3f,
  @builtin(instance_index) instance: u32) -> Output {
  let camera = instances[instance];
  var output: Output;
  output.instance = instance;
  output.position = camera.transform * vec4f(position, 1.0);
  output.color = color * camera.tint;
  output.uv = uv;
  output.worldPosition = (camera.model * vec4f(position, 1.0)).xyz;
  output.normal = (camera.normalMatrix * vec4f(normal, 0.0)).xyz;
  return output;
}

fn brdf(n: vec3f, v: vec3f, l: vec3f, base: vec3f, metallic: f32, roughness: f32) -> vec3f {
  let noL = max(dot(n, l), 0.0);
  let noV = max(dot(n, v), 0.0);

  if (noL == 0.0 || noV == 0.0) {
    return vec3f(0.0);
  }

  let h = unit(v + l);
  let noH = max(dot(n, h), 0.0);
  let voH = clamp(dot(v, h), 0.0, 1.0);
  let alpha = roughness * roughness;
  let a2 = alpha * alpha;
  let denominator = noH * noH * a2 + (1.0 - noH * noH);
  let distribution = a2 / (3.14159265359 * denominator * denominator);
  let visibility = 0.5 / max(noL * sqrt(noV * noV * (1.0 - a2) + a2)
    + noV * sqrt(noL * noL * (1.0 - a2) + a2), 0.000001);
  let f0 = mix(vec3f(0.04), base, vec3f(metallic));
  let fresnel = f0 + (vec3f(1.0) - f0) * pow(1.0 - voH, 5.0);
  let diffuse = (vec3f(1.0) - fresnel) * (1.0 - metallic) * base / 3.14159265359;
  return (diffuse + distribution * visibility * fresnel) * noL;
}

@fragment
fn fragmentMain(input: Output, @builtin(front_facing) frontFacing: bool)
  -> @location(0) vec4f {
  let camera = instances[input.instance];
  let base = input.color * textureSample(baseColorTexture, baseColorSampler, input.uv);
  let mr = textureSample(metallicRoughnessTexture, metallicRoughnessSampler, input.uv);
  let encodedNormal = textureSample(normalTexture, normalSampler, input.uv).xyz;
  let emission = camera.emissive.xyz
    * textureSample(emissiveTexture, emissiveSampler, input.uv).xyz;
  let dp1 = dpdx(input.worldPosition);
  let dp2 = dpdy(input.worldPosition);
  let duv1 = dpdx(input.uv);
  let duv2 = dpdy(input.uv);

  if (camera.flags.x == 0.0) {
    return vec4f(base.xyz + emission, base.a);
  }

  let front = frontFacing == (camera.flags.w > 0.0);

  if (!front && camera.flags.z == 0.0) {
    discard;
  }

  let viewRay = lighting.counts.yzw;
  let v = select(unit(lighting.viewPosition.xyz - input.worldPosition), -viewRay,
    dot(viewRay, viewRay) > 0.0);
  var n = unit(input.normal);

  if (camera.flags.z != 0.0 && dot(n, v) < 0.0) {
    n = -n;
  }

  let shadowNormal = n;

  if (camera.flags.y != 0.0) {
    let det = duv1.x * duv2.y - duv1.y * duv2.x;
    let tangent = (dp1 * duv2.y - dp2 * duv1.y) / det;
    let bitangent = (dp2 * duv1.x - dp1 * duv2.x) / det;
    let t = unit(tangent - n * dot(n, tangent));
    let crossB = unit(cross(n, t));
    let b = crossB * select(-1.0, 1.0, dot(crossB, bitangent) >= 0.0);
    let mapped = (encodedNormal * 2.0 - vec3f(1.0))
      * vec3f(camera.material.z, camera.material.z, 1.0);
    n = unit(t * mapped.x + b * mapped.y + n * mapped.z);
  }

  let metallic = clamp(camera.material.x * mr.b, 0.0, 1.0);
  let roughness = clamp(camera.material.y * mr.g, 0.045, 1.0);
  var radiance = emission + environmentRadiance(n, v, base.xyz, metallic, roughness,
    ambientVisibility(vec2i(input.position.xy)));

  for (var index = 0u; index < u32(lighting.counts.x); index++) {
    let light = lighting.lights[index];
    var l = -light.directionKind.xyz;
    var attenuation = 1.0;

    if (light.directionKind.w != 0.0) {
      let delta = light.positionRange.xyz - input.worldPosition;
      let distance2 = dot(delta, delta);
      l = unit(delta);
      let ratio2 = distance2 / (light.positionRange.w * light.positionRange.w);
      let fade = clamp(1.0 - ratio2 * ratio2, 0.0, 1.0);
      attenuation = fade * fade / max(distance2, 0.0001);

      if (light.directionKind.w == 2.0) {
        let cone = clamp((dot(-l, light.directionKind.xyz) - light.cone.y)
          / (light.cone.x - light.cone.y), 0.0, 1.0);
        attenuation *= cone * cone * (3.0 - 2.0 * cone);
      }
    }

    var visibility = 1.0;

    if (camera.material.w != 0.0) {
      visibility = lightVisibility(index, input.worldPosition, shadowNormal,
        max(dot(shadowNormal, l), 0.0), dp1, dp2);
    }

    radiance += brdf(n, v, l, base.xyz, metallic, roughness)
      * light.colorIntensity.xyz * light.colorIntensity.w * attenuation * visibility;
  }

  return vec4f(clamp(radiance, vec3f(0.0), vec3f(65504.0)), base.a);
}
