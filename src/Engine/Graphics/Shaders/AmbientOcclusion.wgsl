@group(2) @binding(5) var ambientOcclusion: texture_2d<f32>;

fn ambientVisibility(pixel: vec2i) -> f32 {
  return textureLoad(ambientOcclusion, pixel, 0).x;
}
