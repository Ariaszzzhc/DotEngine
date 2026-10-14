struct Environment {
  values: vec4f
}

@group(2) @binding(0) var<uniform> environment: Environment;
@group(2) @binding(1) var diffuseEnvironment: texture_cube<f32>;
@group(2) @binding(2) var specularEnvironment: texture_cube<f32>;
@group(2) @binding(3) var environmentBrdf: texture_2d<f32>;
@group(2) @binding(4) var environmentSampler: sampler;

/** Split-sum single-scattering GGX with Schlick energy-split diffuse; all input
 * cubes are linear radiance and diffuse stores irradiance/pi. Environment yaw
 * rotates world rays into panorama space consistently with the sky background.
 * 分离积分的单次散射 GGX 与 Schlick 能量分配漫反射；输入立方体为线性辐射，漫反射
 * 存辐照度/pi；环境朝向将世界视线旋至全景空间，与天空背景一致。 */
fn environmentDirection(direction: vec3f) -> vec3f {
  let c = environment.values.y;
  let s = environment.values.z;
  return vec3f(c * direction.x - s * direction.z, direction.y,
    s * direction.x + c * direction.z);
}

fn environmentRadiance(n: vec3f, v: vec3f, base: vec3f, metallic: f32, roughness: f32,
  visibility: f32) -> vec3f {
  if (dot(v, v) == 0.0) {
    return vec3f(0.0);
  }

  let nv = clamp(dot(n, v), 0.0, 1.0);
  let f0 = mix(vec3f(0.04), base, vec3f(metallic));
  let fresnel = f0 + (vec3f(1.0) - f0) * pow(1.0 - nv, 5.0);
  let diffuse = textureSampleLevel(diffuseEnvironment, environmentSampler,
    environmentDirection(n), 0.0).rgb;
  let reflection = textureSampleLevel(specularEnvironment, environmentSampler,
    environmentDirection(reflect(-v, n)), roughness * environment.values.w).rgb;
  let brdf = textureSampleLevel(environmentBrdf, environmentSampler, vec2f(nv, roughness), 0.0).rg;
  let radiance = (vec3f(1.0) - fresnel) * (1.0 - metallic) * base * diffuse * visibility
    + reflection * (f0 * brdf.x + vec3f(brdf.y));
  return radiance * environment.values.x;
}
