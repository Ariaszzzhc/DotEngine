/** No requested shadow source means unobstructed direct illumination.
 * 未请求投影光源时，直接光不应用阴影遮挡。 */
fn lightVisibility(index: u32, position: vec3f, normal: vec3f, noL: f32,
  dp1: vec3f, dp2: vec3f) -> f32 {
  return 1.0;
}
