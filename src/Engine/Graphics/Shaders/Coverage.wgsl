/** Coverage policy is shared by surface, depth guides, shadows and integer picking.
 * Alpha is linear, independently of the RGB texture color space.
 * 表面、几何深度、阴影与整数拾取共用覆盖策略；alpha 始终在线性空间。 */
fn covered(alpha: f32, policy: vec4f) -> bool {
  if (policy.x == 1.0) {
    return alpha >= policy.y;
  }

  return policy.x != 2.0 || alpha > 0.0;
}
