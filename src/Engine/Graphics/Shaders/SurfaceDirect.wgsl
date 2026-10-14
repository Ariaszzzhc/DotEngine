/** With no bound environment, surfaces receive direct lighting and emission only.
 * 未绑定环境时，表面仅接收直接光及自发光。 */
fn environmentRadiance(n: vec3f, v: vec3f, base: vec3f, metallic: f32, roughness: f32) -> vec3f {
  return vec3f(0.0);
}
