struct InstanceData {
  transform: mat4x4f,
  tint: vec4f,
  emissive: vec4f,
  model: mat4x4f,
  normalMatrix: mat4x4f,
  material: vec4f,
  flags: vec4f,
  occlusion: vec4f
}

struct View {
  position: vec4f,
  counts: vec4f
}

@group(0) @binding(0) var<storage, read> instances: array<InstanceData>;
@group(1) @binding(0) var<uniform> view: View;

struct Output {
  @builtin(position) position: vec4f,
  @location(0) worldPosition: vec3f,
  @location(1) normal: vec3f,
  @location(2) @interpolate(flat) instance: u32
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
fn vertexMain(@location(0) position: vec3f, @location(3) normal: vec3f,
  @builtin(instance_index) instance: u32) -> Output {
  let camera = instances[instance];
  var output: Output;
  output.instance = instance;
  output.position = camera.transform * vec4f(position, 1.0);
  output.worldPosition = (camera.model * vec4f(position, 1.0)).xyz;
  output.normal = (camera.normalMatrix * vec4f(normal, 0.0)).xyz;
  return output;
}

/** Geometry guides match opaque coverage; AO uses smooth geometric normals without
 * normal-map microdetail, avoiding false occlusion on a flat normal-mapped surface.
 * 几何引导数据匹配不透明覆盖；AO 使用平滑几何法线，不将平面法线贴图细节误判为遮挡。 */
@fragment
fn fragmentMain(input: Output, @builtin(front_facing) frontFacing: bool) -> @location(0) vec4f {
  let camera = instances[input.instance];
  let geometric = unit(cross(dpdx(input.worldPosition), dpdy(input.worldPosition)));
  let front = frontFacing == (camera.flags.w > 0.0);

  if (camera.flags.x != 0.0 && !front && camera.flags.z == 0.0) {
    discard;
  }

  var normal = unit(input.normal);

  if (camera.flags.x == 0.0 || dot(normal, normal) < 0.5) {
    normal = geometric;
  }

  let ray = view.counts.yzw;
  let direction = select(unit(view.position.xyz - input.worldPosition), -ray, dot(ray, ray) > 0.0);

  if (dot(normal, direction) < 0.0) {
    normal = -normal;
  }

  return vec4f(normal, 1.0);
}
