/** No enabled geometric AO contributes full ambient visibility.
 * 未启用几何 AO 时，环境可见性为一。 */
fn ambientVisibility(pixel: vec2i) -> f32 {
  return 1.0;
}
