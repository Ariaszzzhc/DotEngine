# Rendering development

## Delivered surface resources

`Dot.Graphics.Image` owns packed RGBA8 and linear floating-point HDR pixels.
`Dot.Assets.Image` decodes PNG/JPEG into RGBA8 and Radiance RGBE into RGBA floats;
HDR brightness above one is retained. EXR and 16-bit PNG are explicit unsupported
inputs rather than implicit precision/color conversions. Input is at most 64 MiB;
dimensions are 1..8192 with at most 16,777,216 pixels. Dimension preflight occurs
before decoded pixel allocation; returned RGBA8/HDR arrays are at most 64/256 MiB.
Project loading uses the validated root-relative file service and preserves codec
path context. The pinned stb dependency supplies the decoder; no toolchain settings
or machine-specific include paths are added.

`Renderer::uploadTexture` owns an immutable shared texture and sampler independently
from its source pixels. `TextureOptions::colorSpace` explicitly selects sRGB color
or linear data. GPU sRGB sampling converts RGB to linear light while alpha remains
linear. Mips average linear light and retain odd-size edges; each level has dimensions
`max(1, previous / 2)`. Samplers support nearest/linear filtering, clamp/repeat/mirror
addressing and anisotropy 1..16 (nearest requires one). UV (0,0) addresses the source's
top-left texel. No image-flip or decoder gamma setting is changed process-wide.

`downsampleImage` exposes the same validated, device-independent CPU mip operation.
It returns owned pixels, copies a 1x1 source, keeps alpha linear and rejects unknown
color spaces. A shared immutable 256-value sRGB decode table uses the original
transfer function; averaging order, output transfer and rounding are unchanged.
Texture upload borrows level zero for its queue copy and owns only generated levels,
without first duplicating the complete source image. No persistent source/mip cache
or image-format change is introduced.

`RendererConfig` limits asset texture residency before upload/mip allocation,
by resource count and logical bytes including every mip level. Defaults are
4096 textures and 512 MiB; these are configurable up to those bounds. Pipeline
constant texels do not consume asset-residency counters. Logical bytes are not
physical driver allocation measurements. Shared copies count once, and the final
owner's destruction releases the counters. Queries/operations require the creating
thread; quiescent resource destruction may happen on another thread or after device
shutdown. Foreign, empty and moved resources fail explicitly.

Every mesh draw carries a `Material`: linear base-color factor and an optional
shared base-color texture. Fragment output multiplies sampled texels, vertex color,
object tint and the material factor. No texture selects a constant white texel;
an explicitly invalid texture is an error. SurfaceModel selects unlit or
metallic/roughness shading through the same current HDR pipeline.
The material also has finite nonnegative linear emissive radiance in [0,65503]
per RGB channel, added before exposure; alpha is unaffected. Transparency blending remains planned; direct physical materials, HDRI lighting
and explicit shadow maps are described below.

GPU regressions load actual PNG/HDR files, sample retained textures through materials
into persistent GPU targets, check RGBA pixels and verify mesh/texture/target release.
Focused tests cover sRGB versus linear sampling, alpha, filtering, UV row
orientation, addressing outside the unit square, odd-edge mip minification,
source independence, sharing/moves, foreign handles, invalid inputs, residency
limits and recovery. Native surface/GUI regressions use the same current pipeline.

## Delivered HDR and display pipeline

Immediate drawing, mesh drawing, persistent viewport targets and native scene
presentation all render into linear RGBA16F with depth before a separate full-screen
post pass. Geometry is not drawn into a clipped eight-bit scene target. Persistent
targets own HDR, RGBA8 display and depth storage (16 logical bytes per pixel);
`readHdrTarget` reads the completed scene before exposure/mapping as owned float
RGBA, while `readTarget` reads its completed display pixels. Raw HDR readback also
validates finite radiance/alpha and handles padded GPU rows. The half-float scene
range is [0,65504]; invalid clear/emissive inputs fail before changing a target.

`PostProcessSettings` chooses exposure in stops [-16,16], LinearClamp, Reinhard or
AcesFitted, and linear/sRGB output encoding. Default scene output uses AcesFitted
and sRGB. Exposure is applied once as `2^exposureEv`, then tone mapping, then display
encoding; alpha is not exposed, mapped or gamma-converted. AcesFitted uses the
published [Stephen Hill RRT/ODT fit](https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl),
including its input/output color matrices; this is an approximation, not full ACES
processing. Its MIT attribution/license is included in runtime/editor packages.
Native sRGB attachment encoding is accounted for so shader/device conversion does
not apply twice. Linear output is an explicit diagnostic choice through the same
HDR pipeline, not a separate renderer or a compatibility path.

`RendererConfig::postProcess` initializes these settings. `setPostProcess` validates
before replacing future-frame settings; existing completed targets keep their
pixels until rendered again. Calls require the creating thread and are forbidden
inside a GUI callback. GUI widgets are not scene tone-mapped; `guiImage` displays the
completed mapped viewport texture. Camera/scene authoring and agent controls for
exposure are subsequent editor integration work, not delivered by this API stage.

The renderer reuses internal HDR/depth working attachments between immediate/native
frames, replacing them only when dimensions change and allocation succeeds. These
are separately reported as `frameBytes` (12 logical bytes per pixel, bounded to
256 MiB); persistent asset targets retain their own count/byte budget. Allocation
failures return errors and retain the previous working storage.

## Delivered direct PBR rendering

`SurfaceModel::MetallicRoughness` uses GGX distribution, height-correlated Smith
visibility, Schlick Fresnel and an energy-split Lambert diffuse term. Dielectric
F0 is 0.04; metallic F0 uses the linear base color and has no diffuse lobe. The
[Filament material model](https://google.github.io/filament/main/filament.html)
provides the mathematical reference. Roughness/metallic factors are [0,1]; effective
roughness is at least 0.045 for stable microfacet evaluation. Unlit surfaces remain
an explicit material choice for debug/emissive content, not an old renderer.

Texture conventions follow [glTF material data](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials):
base/emissive colors are normally sRGB; packed roughness G/metallic B and tangent
normal RGB are linear data. Factors multiply sampled values. Normal scale [0,8]
applies to tangent X/Y before normalization. RGB decodes to signed XYZ, preserving
negative Z samples rather than clamping source data. Filtered directions with
squared length at most 1e-12 retain the geometric normal. Occlusion and
transparency blending are not implemented by this direct-light stage.

Lit meshes require complete unit normals; a requested normal texture also requires
nondegenerate position triangles and UV magnitudes <=1e6. Without authored tangents,
UV determinant must be >=1e-12 in magnitude and relative determinant >1e-6.
Missing normals or an unavailable normal-map frame fail
before drawing. Normal maps use complete authored tangent frames when present;
otherwise world-position/UV screen derivatives derive the frame, including mirrored
handedness. Authored tangents retain their baked basis rather than being replaced. World normals use the inverse transpose, direction-preserving
rescaling and per-fragment normalization. Lit world transforms must be finite
invertible affine matrices; inverse-normal coefficients are bounded to 1e12 and
the infinity-norm condition bound to 1e20. Transformed geometry stays within
+/-1e6 world meters. Reflected transforms adjust front winding; single-sided back
faces are discarded, while double-sided normals face the observer.

`Lighting` owns view position, optional orthographic view-ray direction and at most
64 direct lights. Perspective reflections use position-dependent view rays;
orthographic reflections use parallel rays. Directional/spot directions normalize
when stored. Directional intensity is lux, point/spot intensity candela; light RGB
is linear [0,1], intensity [0,1e6], point/spot range [0.01,1e6] meters. Point/spot
lights use inverse-square attenuation with a 1-cm near-distance floor and smooth
range falloff. Spot inner/outer cones differ by at least 0.1 degree and fade smoothly.
Empty light arrays supply no ambient substitute: lit surfaces retain emission only.
Final scene radiance explicitly saturates at the RGBA16F ceiling 65504.

`RendererConfig::lighting` initializes owned data; `setLighting` validates/copies
transactionally for future frames and `lighting` returns an independent snapshot.
Texture slots are shared immutable resources and every draw has independent model,
normal, material and texture data. Frame light buffers are owned by submitted
commands. Operations remain confined to the renderer thread.

Focused GPU tests compare an analytic frontal BRDF value, metal/dielectric response,
roughness peaks, point distance/range, spot direction, dark/emissive frames, packed
texture channels, normal/emissive maps, inverse-transpose/mirrored transformations,
UV handedness, orthographic reflections, the full 64-light array and invalid-input
retention. The sphere primitive supplies radial normals and explicit UV seams/poles.

Material assets, persisted scene lights and basic shared inspector/agent authoring
now use this renderer in preview, Play and exported games. Dedicated material tooling,
the requested project/game visual upgrades, temporal AO, full animated glTF import,
large-scene performance remains incomplete; the gallery does
not establish those capabilities.

## Persisted scene materials and lights

`Dot.Assets.Material` owns GPU-free authoring data, with one strict `.dotmaterial`
JSON structure (no version dispatch). Files are limited to 1 MiB and reject unknown,
missing/duplicate fields, invalid factors and unsafe paths. Material files and
parents must be regular files/directories without symlinks. Save is atomic and
requires existing parents. Referenced textures are resolved by rendering/export,
not replaced by sample or missing-resource colors.

```json
{
  "model": "metallicRoughness",
  "baseColor": [0.955, 0.638, 0.538, 1],
  "emissive": [0, 0, 0],
  "metallic": 1,
  "roughness": 0.4,
  "normalScale": 1,
  "doubleSided": false,
  "baseColorTexture": null,
  "metallicRoughnessTexture": null,
  "normalTexture": null,
  "emissiveTexture": null,
  "occlusionStrength": 1,
  "occlusionTexture": null,
  "alphaMode": "opaque",
  "alphaCutoff": 0.5
}
```

Each non-null texture slot contains `path` and a complete `sampling` object:
`colorSpace` (`linear`/`srgb`), `filter` (`nearest`/`linear`), `addressU`/`addressV`
(`clamp`/`repeat`/`mirror`), boolean `mipmaps`, integer `maxAnisotropy` (1..16).
Nearest filtering requires anisotropy one. Paths are portable, lowercase-suffix
`.png`/`.jpg`/`.jpeg` paths relative to the project root, including when the material
lives in a subdirectory. Color slots may be sRGB/linear; normal and packed MR slots
must be linear and require the physical model. Signed normal RGB is preserved at
import, authoring preview, Play and export; sampled near-zero directions retain
the geometric normal instead of creating a zero shading frame.
A null slot deliberately uses its documented constant; an invalid present slot fails.

An entity's `material` component binds `{ "asset": "assets/copper.dotmaterial" }`.
The `light` component describes directional/point/spot kind, linear color, intensity,
range, inner/outer cone degrees and enabled state. World pose comes from the entity
hierarchy: local -Z is the emitted ray, position is transformed origin, and range is
in world meters without scale multiplication. Camera position/orthographic rays
come from the selected camera's current world matrix. Disabled lights are skipped;
more than 64 enabled lights fails without submitting a new target frame.

`SceneRenderer` shares meshes, materials and textures by path and full sampling
options. Its byte budget includes logical mesh/texture GPU bytes; material and
texture counts are bounded by 4096. New textures enter the cache only after all
slots load successfully. Nonreferenced visible-scene resources are pruned;
immutable assets reload via `clearCache`. Saved editor resources request that same
refresh. Cache counters include materials/textures/texture bytes; failure retains
completed target pixels, and clear/destruction releases owned resources. Material
loads and camera/light preparation report contextual errors without fake surfaces.
Built-in cube/quad geometry now carries unit normals and full-face UVs; cubes use
24 split face-corner vertices. Authored assets keep their actual attributes: missing
normals/normal-map frames fail rather than receiving invented data.

Export validates referenced material definitions, images/normal encoding and every
physical mesh's normal/UV requirements through the same CPU surface analysis used
by GPU upload. It stages and publishes only after validation; bad dependencies do
not leave a published destination. Component descriptions survive scene save/load,
editor undo/redo, Play cloning and exported package loading. Current inspector
fields and agent component edits share document validation; dedicated visual
material graphs/light gizmos and material/light script accessors remain planned.
Feature Lab includes a copper cube, a normal-mapped quad and a directional headlight.

## Required rendering end state

The [capability roadmap](EngineRoadmap.md) includes PBR, HDRI IBL, soft shadow maps,
SSAO, HDR tone mapping and populated-scene scalability. Work remains incremental;
the following are verification requirements, not descriptions of current features.

- Metallic/roughness PBR: GGX specular, energy-conserving diffuse, base-color,
  metallic/roughness, normal, emissive and occlusion texture slots; correct linear
  versus color spaces, tangent-space normals and inverse-transpose world normals.
  Test recognizable dielectric/metal and smooth/rough surfaces under changing light.
- HDRI IBL: load an actual [Poly Haven environment](https://polyhaven.com/hdris),
  retain HDR radiance, compute diffuse irradiance, specular prefilter mip levels and
  the BRDF integration lookup. Verify orientation/rotation, intensity and roughness
  response on a material-sphere scene, plus lifetime and invalid-input recovery.
- Soft shadows: directional cascades and point/spot shadow maps, consistent bias,
  filtered penumbrae, stable camera movement and proper receiver/occluder handling.
  Validate contact regions, off-camera casters, nonuniform transforms and resizing.
- SSAO: depth/normal reconstruction, bounded world/view-space radius, edge-aware
  filtering and stable camera/resize behavior. Compare enabled/disabled contact
  detail without treating dark halos or flicker as acceptable quality.
- HDR pipeline: linear floating-point scene targets retain emissive/specular values
  above one; exposure/tone mapping and output encoding occur once at the end.
  Read back pre/post-mapping evidence to catch premature clipping or double gamma.
- Large-scene rendering: visibility/culling, instancing, batching, resource streaming
  and measured CPU/GPU frame costs on a populated scene, with explicit content,
  resolution, device and memory accounting. Passing an empty scene is insufficient.

Each stage must reach runtime, persistent viewport and authoring/agent paths before
claiming an editable production feature. Planned rendering cannot be presented as
implemented just because resource or unit tests pass.

## GPU environment preprocessing foundation

`Dot.Graphics.Environment` validates opaque linear 2:1 latitude-longitude HDR
panoramas and bounded power-of-two processing dimensions. Top is +Y, center -Z,
longitude grows toward +X. Cube faces use native WebGPU +X,-X,+Y,-Y,+Z,-Z ordering.
RGBA16F outputs require finite radiance no larger than 65504; unsupported radiance
is an explicit error, with no exposure/clamping substituted during preprocessing.

`Renderer::bakeEnvironment` uploads float32 source/mips and computes immutable GPU
radiance cubes, cosine-sampled diffuse irradiance/pi, GGX importance-prefiltered
specular mips and a correlated-Smith split-sum BRDF LUT. Source mips use latitude
solid-angle weighting; GPU source reads interpolate linear radiance, wrap longitude
and clamp latitude without optional float32-filtering features. Footprint-selected
source LOD reduces undersampled highlights. Roughness zero samples the source;
other specular mips correspond to linear perceptual roughness 0..1. Numerical pole
handling defines an otherwise undefined longitude and avoids atan2(0,0).
The physical integration follows the [Filament IBL derivation](https://google.github.io/filament/Filament.html),
with independently implemented shader code and analytic energy/moment tests.

Defaults are radiance 256, diffuse 32, specular 128, BRDF 128, 256 samples. Radiance/
specular allow 16..512; diffuse 4..128; BRDF 16..256; dimensions are powers of two,
samples 32..1024. At most 32 live environment maps are permitted. Output residency
and temporary RGBA32F source/mips share maxTextureBytes with ordinary textures;
bytes are logical texels, not driver allocation. Each dispatch owns separate
parameter data. Device error scopes and bounded GPU completion precede publication
and temporary source release; incomplete timed-out work faults the device for
future operations. Preprocessing is synchronous and CPU source-mip work is bounded
by image dimensions, not a hard wall-clock guarantee. Background import/disk bake
caching and streaming are still planned.

EnvironmentMap copies share all four GPU textures/sampler and count once in
resourceStats.environments/environmentBytes. Info/read operations require the
creating thread/live device. Foreign, absent, expired, invalid face/mip maps fail;
quiescent destruction can outlive the renderer. readEnvironment returns actual
floating-point device pixels with no implicit convolution or display conversion.

Unit tests check bounds/face axes; offscreen tests check
constant energy, all faces/mips, independent linear-field diffuse moments,
roughness blur, BRDF's smooth/rough limits, actual HDRI, budget/failure/lifetime.

Preprocessing and renderer PBR/sky consumption are implemented. Persistent scene environments, bounded HDRI asset caching and editor/Play/export
authoring now consume this subsystem. Async import/disk bake caching and streaming
remain planned.

## Built-in WGSL sources

The renderer's material, tone-mapping and environment shaders live in separate
`src/Engine/Graphics/Shaders/*.wgsl` files. Use two-space indentation, spaces around
operators, one statement per line, a 100-character line limit, and blank lines
between functions/types/control blocks, without padding block interiors.
C++ clang-format does not format WGSL; these files are reviewed separately.

CMake embeds the exact source into a generated private `BuiltinShaders.hpp` before
compiling the engine. Each WGSL file is an explicit build dependency: editing it
regenerates the header and recompiles the renderer implementation. Runtime/editor
executables and relocated distributions require no shader source files. Dawn
compiles the actual embedded WGSL on the native device; GPU tests verify the
material, HDR/tone-mapping and environment pipelines.

## HDRI surface lighting and sky

`Renderer::setEnvironment` transactionally retains a shared EnvironmentMap for
future mesh frames; null clears it. Intensity is linear [0,64], yaw is degrees
[-360,360]; absent/foreign maps, nonfinite settings and wrong-thread access fail
without replacing the previous binding. Copies from `environment()` retain the
map independently. Clearing a binding preserves existing target pixels until
redrawn. Pipeline creation is checked before publishing the binding.

Physical surfaces sample the cosine-convolved irradiance/pi along their mapped
world normal and GGX-prefiltered radiance along the reflected camera ray.
Perceptual roughness selects specular mip LOD; the correlated-Smith LUT supplies
Schlick split-sum scale/bias. Dielectrics use F0=.04; metals use base color and
suppress diffuse. Diffuse uses the Schlick energy split, with single-scattering
GGX specular. This is a split-sum approximation, not path-traced lighting or a
multiple-scattering compensation model. A zero view ray contributes no reflected
lighting, consistently with direct-light BRDF handling. Unlit surfaces retain
their base color/emission; binding no environment supplies no invented light.
Normal/data/emissive/base textures and direct lights continue to share the same
physical surface pipeline.

When background is enabled, a fullscreen triangle reconstructs world rays from
the current draw's inverse view-projection and Lighting camera pose. Orthographic
cameras use their constant emitted direction. Singular matrices, homogeneous
horizons or a perspective camera on the reconstruction plane return contextual
errors and preserve the previous frame. Environment yaw rotates both reflection
and sky rays into the same panorama space. Sky writes linear HDR color without
changing depth; geometry drawn afterward occludes it. Intensity, exposure, ACES/
Reinhard and display encoding apply through the existing shared HDR pipeline.
Persistent targets, readback meshes and native mesh presentation share this path.
Immediate unlit vertex rendering is independent of environment mesh rendering.

SceneRenderer already forwards its scene camera pose/projection to this renderer
path. Native GPU tests verify saved environment settings light decoded scene material
resources without direct lights, reuse/reload bakes and release owned cache maps
without detaching another service's binding. Scene files persist HDRI references/
settings and the editor exposes the configuration through its Environment pane
and transaction commands.

Independent constant-energy tests check diffuse and the analytical rough-metal
limit, intensity/unbinding/unlit behavior; directional fields check perspective
rays, yaw, orthographic sky and geometry depth. This evidence establishes the
renderer consumer; saved scene authoring and its integration are described below.

## Saved scene environments

A scene's optional root `environment` owns a portable project-relative `.hdr`
reference, linear intensity [0,64], Y-axis rotation [-360,360], background flag and
processing dimensions/sample count. Null/absence means no environment. All settings
are validated on the CPU before publication; strict serialization rejects unknown
fields, wrong types and unbounded/nonintegral work values. Resource existence,
opaque 2:1 panorama validity and residency are checked by their actual consumers.

SceneRenderer treats the current scene as authoritative: no environment clears a
previous scene binding. It lazily loads the actual HDR panorama and bakes a shared
map within its existing cache budget. Intensity/yaw/background changes reuse the
map; asset or processing changes bake a new map. Transactional replacement needs
the old and new maps to fit the cache peak; a failure retains the previous GPU
binding and rendered target. `stats()` includes environment count/logical bytes.
Replacing source bytes at the same path requires `clearCache`, as with other
immutable assets. Cache clearing/destruction detach only this service's bound map
(by shared resource identity), preserving another service's later binding. Device
failure may defer a retained renderer binding's release until renderer teardown.

The editor Environment pane edits HDRI, intensity, yaw, sky background and preview/
balanced/high quality presets; custom saved processing values are retained until a
preset is explicitly selected. Drafts require Apply or Reset and participate in
save/Play/export/close protection; save-close commits them through the document
transaction before saving. Undo/redo, reopening and Play's isolated scene copy all
retain the same configuration. Asset import/save refreshes the shared scene cache.
Export validates actual referenced HDR pixels and processing contracts before
publishing; copied HDR bytes and scene references survive relocation. No online
asset fetch or absolute authoring path is required by the exported player.

Feature Lab now includes its local CC0 Studio Small 09 panorama in the authored
scene/project/template so editor, Play and native exports show the same environment.
This provides authored IBL; directional shadows and the renderer SSAO foundation are
described below, while temporal AO,
async baking/streaming and advanced
lighting/editor tools remain part of the active capability roadmap.

## Directional and local soft shadows

`Light::shadows` explicitly requests shadows independently for each of the frame's
at most 64 lights. Null means no maps for that light. A mixed-resolution depth atlas
retains every requested source/face; exceeding its configured budget fails without
selecting or dropping lights silently. Shadow coverage shares opaque/masked and
skinned geometry with the surface pass. Contact-hardening PCSS, temporal
filtering and GPU-driven culling remain planned. CPU batching/instancing is implemented.
Immediate unlit vertex rendering is independent of mesh shadow rendering.

`Dot.Graphics.Shadows` validates work on CPU and fits a finite camera frustum with
at most 10000 world caster AABBs. Practical logarithmic/uniform splits cover the
configured camera depth, up to four cascades. Square sphere fits and light-space
texel snapping stabilize the XY projection; cascades overlap ten percent and the
fragment shader blends the overlap. Caster bounds overlapping the footprint extend
the depth interval even when outside the camera view. Singular/infinite/horizon
frustums, invalid work, non-affine/out-of-bounds casters or unrepresentable draw
matrices fail before submission, preserving the previous target frame.

Defaults: resolution 1024, four cascades, distance 100 m, splitLambda .65, normalized
depthBias .0002/slopeBias .001, normalBias .01 m and filterRadius 2 texels. Resolution
is power-of-two 128..2048, cascades 1..4, distance .1..10000 (directional coverage must
exceed camera near), splitLambda [0,1], depth/slope biases [0,.02], normalBias [0,.2] and radius [0,8].
A fixed-radius sixteen-tap PCF kernel plus bilinear depth comparisons produces soft
edges; directional/spot radius zero uses one hardware comparison sample. World-space
softness grows with cascade texel size; this is not a physical contact-hardening area-light model.

Receiver-plane gradients come from world-position derivatives computed before
nonuniform fragment control flow. Each tap's reference depth follows the receiving
plane; bias includes the full bilinear neighbor footprint. Geometric normals drive
normal/slope offsets independently of normal-map perturbation. This avoids the
self-shadowed sloping floor found by the initial native tests. Only the selected
direct light is attenuated; IBL, emission and other lights retain their contributions.
MeshDraw castsShadow/receivesShadow are independent and default true.

The renderer caches validated shader variants and a Depth32Float array atlas.
`Dot.Graphics.ShadowAtlas` plans this storage on CPU without a device.
Largest-first quadtree packing retains each face's requested 128..2048 resolution.
Pages start at the largest requested resolution and grow only when needed to fit
the device's array-layer limit. Integral tiles do not overlap; hardware comparison
footprints clamp inside their own tile and point texel loads remap cube faces before
adding the destination tile offset. Each physical layer clears once, then each
projection uses its own viewport/scissor, avoiding later lights erasing earlier ones.
Independent per-face instance storage prevents queue-write aliasing. Read-only GPU
storage holds 64 light records, indexed by the actual light index; unshadowed slots
have zero map count. Reordering/removing lights rebuilds their current mapping.

`RendererConfig.maxShadowBytes` is 1 byte..512 MiB, default 256 MiB, counting whole
allocated pages/padding at four bytes per texel. `resourceStats.shadowBytes` records
current logical storage, rather than the sum of occupied tile areas or driver
memory. Matching page dimensions/count reuse storage; changing those reallocates it,
and removing all requests releases it. Driver replacement/in-flight allocations can
overlap. Budget failures in setLighting preserve the previous lights, maps and
completed target. These working buffers remain separate from asset residency.

World caster bounds are validated once per frame. Local faces conservatively cull
transformed mesh bounds against their own frustum, independently of main-camera
visibility; directional fits retain off-camera overlapping casters. Every face has
independent batch/instance storage when its physical page changes. Large scenes still
need pooled submission and GPU-driven/spatial local-light work. Range/cone attenuation of
zero, or zero intensity, skips shadow filtering without changing light contribution.

Saved scene lights retain the strict shadows object with all eight settings; mesh
components retain castsShadow/receivesShadow. The inspector can enable shadows on
all three light kinds and edit the nested settings; integral work controls retain
integer JSON. Document transactions/undo/saving, preview, Play and export use the same scene path.
Feature Lab's saved directional headlight now requests cascades.

CPU tests cover fits/splits/texel stability/occluders;
GPU tests check moving shadows, casting/receiving, measured penumbrae, all four
cascades including off-camera casters, resource resize/release, other lights, IBL/
emission and input failure retention. Saved-scene and document tests verify authoring.

Point lights render six 90-degree perspective faces (+X,-X,+Y,-Y,+Z,-Z), independent
of the camera. Spot lights render one perspective map with field of view twice the
outer half-cone, which must be strictly less than 90 degrees when shadows are enabled.
Both use radial coverage `min(light.range, shadows.distance)`; the perspective near
plane is `min(.05, coverage*.01)` meters. `cascades` and `splitLambda` apply only to
directional projection. Resolutions, PCF radius and bias controls retain their common
bounds. Each point source contributes six tiles, each spot one, and each directional
source one through four. The atlas uses the current requested set on every frame.

Spot and point receiver-plane derivatives use the perspective quotient rule.
Point footprints wholly inside a cube face use hardware bilinear comparisons (one
sample at radius zero, sixteen soft taps). Only cube-edge footprints remap each
bilinear neighbor across faces, intersect its actual texel ray with the receiving
geometric plane and compare that face's depth (four/64 depth loads). Both paths
retain receiver-plane bias. This avoids clamped cube seams and expensive neighbor
reconstruction across the whole image. Shaders access individual light records by
index, rather than passing full matrix arrays by value through sampling helpers.
Filtering/submission optimization remains unfinished. Radial coverage
limits affect shadowing only; the light's existing smooth range/cone attenuation
continues independently. IBL, emission and other lights are not shadowed by this map.

### Retained shadow depth pages

Each physical atlas layer retains validated depth between mesh submissions. Exact
face tile/projection and relevant caster inputs determine reuse: shared mesh identity,
successful geometry revision, world transform, and mask mode/opacity/cutoff/base-color
texture identity. Local faces compare only their conservative frustum candidates;
directional faces retain their existing off-camera caster policy. Lighting color/
intensity, receiver flags, roughness and filter/bias changes do not redraw unchanged
depth. Projection, layout, geometry or mask-coverage changes invalidate affected pages.
A dirty page clears and redraws all of its tiles together, preserving unchanged
neighbour tiles. Reused pages perform no depth clear, instance-buffer upload or draw.

Identity uses weak ownership, distinguishing an expired resource from an absent
texture without retaining meshes/textures or relying on hash equality. Skin updates
advance geometry revisions only after the full deformation batch validates, including
ping-pong buffer reuse. SceneRenderer keeps the last validated joint palette per
owner: exact unchanged palettes reuse deformation across drawing, bounds and picking;
changed palettes publish only after successful atomic GPU updates. Animated poses
continue to invalidate their actual shadow pages.

Cache candidates are published after successful GPU submission/error validation.
Abandoned encoding or failed submission leaves depth uncertified, so the next mesh
pass rebuilds it. Atlas replacement or removal discards retained dependencies.
The CPU state is bounded by 10,000 draw records and 384 face index lists; it stores
one draw table per snapshot rather than copies of geometry per face. Projection,
input validation, classification and batch planning still run each frame. This
reduces submitted depth work, without claiming streaming, a static/dynamic split,
per-tile clears or measured CPU/GPU frame time.

`MeshFrameStats.shadowPagesUpdated`/`shadowPagesReused` report actual physical layers
redrawn/retained. Shadow instance/draw counters count newly submitted work only;
a fully reused frame reports zero shadow draws while still receiving shadows.
The Rendering pane and live editor inspection expose the same counters. Durable
offscreen regressions cover pixel equality, independent local updates, shared-page
neighbours, resource expiry, camera/coverage changes, skin buffer reuse and abandoned
encoding recovery.

### Renderer CPU and GPU profiling

`Renderer::setProfiling` enables optional mesh sampling, disabled by default.
Changing enablement clears history; repeating the current value is a no-op.
Each enabled interval has a monotonically identified session. `profiling()` returns
at most 120 CPU and 120 GPU samples, sorted by submitted serial within that session,
with adapter/backend, pixel dimensions and the exact work report for each sample.
Input/encoding/submission-validation failures publish no CPU sample; failures
after validated submission, such as pixel mapping, may retain a submitted-work
sample. Picking, standalone GUI passes, target
readback and other renderer calls do not create mesh samples.

CPU nanoseconds are steady-clock wall durations for renderer draw preparation,
command encoding and post-encoding finish/readback setup/queue validation. Presentation
calls also include presentation in the last interval. Scene loading, separate skin
updates, scripts/physics, surface acquisition and pixel-map waiting are excluded.
These values are neither thread CPU utilization nor complete application frame time.

When the adapter supports WebGPU `timestamp-query`, device creation requests that
feature and enabled profiling uses render/compute pass-boundary writes. Query results
are [nanoseconds specified by WebGPU](https://gpuweb.github.io/gpuweb/#timestamp),
with implementation-dependent precision/quantization. Each physical pass gets a
complete start/end pair; the four ordered categories sum shadow depth, SSAO
geometry/filtering, HDR surfaces/sky and tone mapping.
GPU stage work may overlap, so these sums are not mutually exclusive slices. Total GPU
duration spans the earliest physical-pass start through the latest end, excluding
query resolution, pixel copies, GUI and separate skin work. Null range duration
means no pass ran, including fully cached shadows; a present zero is valid.

Four query/readback slots collect results asynchronously using process-event
callbacks. Mesh submissions and inspection poll completed work without waiting
for a timestamp result. A busy ring drops GPU samples and increments `dropped`,
while CPU sampling continues. Each outstanding callback owns its readback state,
without borrowing a renderer or profiler. Disabling/destruction cancels readbacks
and releases query storage; late results cannot enter a later profiling session.
Each slot bounds 384 shadow pages, three AO passes and two color/post passes,
using 778 counters. The four slots use 49,792 logical data-buffer bytes, reported
separately as `gpuBufferBytes`; this excludes opaque driver query allocations.

Unsupported devices explicitly report `gpuSupported=false`, with CPU samples and
no fabricated GPU durations. Empty raw counter records, readback/timestamp faults
and overflowing sums remain observable as `error`; results are asynchronous, so
CPU and GPU latest serials may differ. These scoped
measurements do not establish game FPS, application-wide throughput or production
readiness. The editor exposes the same operation and samples described below.

点光使用独立于相机的六面透视阴影，聚光使用外半角两倍的透视视角（投影时半角须小于
90 度）。局部阴影径向覆盖不超过光源 range 与 shadows.distance 的较小值；级数及
分段混合仅用于方向光。点光内部使用硬件 PCF；仅跨面边缘逐纹素重映射，与接收平面
求交后比较真实投影深度，避免接缝漏光及全画面重建邻点的开销。每帧最多 64 盏光按各自分辨率排入共享图集，默认逻辑
预算 256 MiB，超限明确失败；图集含页填充，局部面独立裁剪，性能优化仍待继续。

## Geometric ambient occlusion foundation

Renderer::setOcclusion enables full-resolution geometric SSAO for future mesh
frames. An explicit geometry pass records Depth32Float and world normals; bounded
radial horizon searches reconstruct positions from actual device depth. Quadratic
steps and a deterministic pixel rotation sample the near field. A bilateral filter
uses world distance and normal agreement to preserve silhouettes/background.
The normal-aware horizon approach is informed by [NVIDIA's SSAO presentation](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/SSAO11.pdf);
this implementation is a radial approximation, not a full HBAO+/GTAO implementation.

Occlusion attenuates only diffuse environment lighting before HDR tone mapping.
Direct lights, environment specular, emission and unlit colors retain their values.
AO geometric guides omit normal-map microdetail, which must not invent occluders
on a flat plane. Perspective and orthographic projections use the same world radius.
Background and offscreen samples contribute no fabricated occlusion.

Settings bound radius to .05..5 meters, bias to 0..0.2 meters, strength to 0..4,
directions to 4/8/16, steps to 2..8 and bilateral radius to 0..4 pixels. The default
is .5/.02/1, eight directions, four steps and two filter pixels. Four working
textures use 24 logical bytes/pixel, at most 256 MiB, with dimensions up to 4096;
3840x2160 fits while 4096x4096 does not. Reallocation/in-flight driver memory can
overlap; resourceStats.occlusionBytes describes the current working texture set.
Null settings release the textures. readOcclusion returns the grayscale visibility
of the last committed mesh frame and fails before a frame or after disabling.
Input validation failures preserve settings, target frame counts and prior images.

Focused CPU/GPU tests cover finite
reconstruction, bounds, flat planes, contact/lifted objects, normal maps, background,
orthographic camera motion, diffuse-only composition, invalid inputs, thread
ownership and resizing/release. Saved optional scene settings and the editor Rendering pane now share strict codec,
document transactions/undo, preview, isolated Play and relocated game export.
SceneRenderer makes scene settings authoritative and tracks binding generations;
clearCache/destruction detach only its own request, even when another consumer
binds equal settings. AO working storage belongs to Renderer, outside asset budgets. Temporal
reprojection, offscreen geometry, thickness-aware multi-layer depth, bent normals
and scalable geometry submission remain planned. Source WGSL is independently
maintained under src/Engine/Graphics/Shaders with two-space indentation, spacing,
100-column lines and separated functions/control blocks. CMake embeds it privately;
the shipped engine needs no shader source files or filesystem search paths.

## Independent views and coverage-aware picking

`Dot.Graphics.View` validates a scene-independent camera and computes its view
projection. `SceneView` explicitly chooses primary game camera, scene camera entity
or independent pose/lens. Lighting, sky, shadows and AO use the chosen camera.
Mesh world bounds are cached alongside actual mesh assets and transformed through
all eight corners; no geometry copies or substitute boxes are used for selection.

`Renderer.pickMeshes` crops the chosen framebuffer pixel center to a 1x1 R32Uint
color target with Depth24Plus and Less comparison, matching covered surface depth.
Per-draw uniforms retain independent IDs and the physical backface policy. Readback
returns an optional draw index; SceneRenderer maps it to the full EntityId. Picking
leaves the visible target, HDR/AO publication and submitted frame counters intact.
Scratch targets and uniforms are released after readback; lifetime/thread/device
validation follows normal renderer ownership. It currently reads back synchronously
and conservatively prunes candidates against the cropped pixel frustum; spatial
GPU-driven indexing remains planned; alpha-aware picking is implemented below.

## Conservative camera clipping / 保守相机裁剪

`Dot.Graphics.Frustum` extracts six homogeneous clip planes for right-handed [0,1]
depth. It tests the eight transformed local AABB corners without dividing by W.
Reflection, shear and projective models are retained correctly; boundary contact
and float-roundoff uncertainty stay as candidates. A false result proves an object
is outside a clip plane; a true result may conservatively include invisible geometry.
Non-finite/reversed bounds and invalid/degenerate cameras fail with context.
采用齐次六平面与变换后的局部包围盒角点，不除 W；保留边界接触及浮点误差裕量，
反射/错切/投影模型不被假包围盒替代。false 确定在外，true 为保守候选。

Renderer validates every submitted mesh/material/texture/transform before pruning.
Main HDR color and SSAO geometry/depth passes skip outside candidates before compact instance uploads and draw emission. Shadow fitting and cascade draws retain off-camera casters,
which can still affect visible receivers. Picking prunes against the actual cropped
1x1 pixel frustum and preserves original draw IDs, so entity selection does not shift
when earlier draws are excluded. The same renderer path serves standalone games,
scene previews, deferred GUI viewports and native surfaces.
输入仍完整校验；主 HDR 与 SSAO 几何阶段在逐绘制资源分配前裁剪，阴影保持画外
投射体；拾取使用单像素裁剪视锥且保留原始索引，运行时与编辑器共用同一路径。

`Renderer.visibilityStats` independently retains the latest successful mesh and pick
calls. Null means no successful call yet. Counts are taken from the actual encoding
loops and published after successful submission/validation; errors and Deferred
presentation preserve prior reports. Mesh counts exclude sky/fullscreen/GUI passes;
instance quantities count every cascade; GPU draw counts are reported separately.
`visibleInstances` counts conservative submitted candidates, not guaranteed rasterized objects. Rendering
panel and `workspace.view.draws` expose these real counters without invented FPS.
Input validation and bounds generation remain linear with a 10000-instance bound.
BVH spatial classification is described below. Streaming, occlusion culling and
per-cascade caster pruning remain unimplemented; camera clipping alone does not
establish large-game scalability.

## Instanced mesh submission / 网格实例化提交

`Dot.Graphics.DrawBatches` owns a stable compact source-index list and consecutive
compatible runs. Disabled entries are skipped; enabled entries are never reordered.
Surface runs share mesh/texture identities while all transforms, tints, emission,
PBR parameters, sidedness and reflection policies remain instance attributes. SSAO,
shadow and picking runs need shared geometry plus base texture identity for masks/blends. This preserves opaque equal-depth
source ordering and integer picking identity. No compatibility rendering path exists.
稳定相邻批次跳过禁用项但不重排启用项；主表面共享网格/纹理绑定，变换、颜色及
PBR/背面策略均保留实例数据。几何、阴影、拾取按共享网格合批，整数身份不改变。

Scene instance records use bounded read-only storage buffers (288 bytes per surface
record, 128 for picking and shadow coverage). Shaders index with instance_index
and pass a flat integer to fragments; the compact record still stores the original
source ID for picking. Main/SSAO share one ordered solid attribute upload. Shadows store
per-cascade matrices and issue instanced runs with the proper base instance. Buffer
sizes are checked against actual device binding/buffer limits before allocation.
Submitted command buffers retain GPU resources; failed calls preserve prior reports.

Mesh inputs and CPU shadow fitting now accept up to 10000 instances, matching scene
entity capacity (a primary camera consumes one entity). Validation, camera clipping,
asset budgets and transform bounds remain. Renderer statistics explicitly distinguish
input/visible/culled/caster instances from actual color/shadow/SSAO/picking draw
commands. The editor displays both quantities; no frame-rate claim is inferred.
输入/阴影拟合上限为 10000，完整校验和资源预算保持；统计区分实例数量与真实 GPU
绘制命令，不把合批数量冒充 FPS。

Batching remains consecutive and input preparation linear. Streaming, GPU-driven
visibility, transparency sorting, animation and production
content still require dedicated implementation; instancing alone does not establish
large-game readiness.

## Spatial visibility index / 空间可见性索引

`Dot.Graphics.SpatialIndex` owns a balanced BVH over at most 10000 finite world
AABBs. Count changes build a replacement tree; same-count changes refit affected
branches. Invalid input or allocation failure preserves the prior tree. Queries
restore original source order, reject outside subtrees and accept strictly inside
subtrees without individual plane tests. Boundary contact remains intersecting.
空间索引拥有世界 AABB；数量变化重建，同数量变化重拟合，失败保留旧树。查询
恢复原始顺序，整棵内部子树免逐对象测试，边界接触保持相交。

Renderer validates every input before classification. Affine corner transforms use
double intermediates and outward-rounded float bounds. Projective models or bounds
that cannot be represented conservatively in float use homogeneous tests directly.
Intersecting BVH candidates retain local-box/model tests, preserving tight rotated
bounds. Camera changes reuse the tree, and fresh world bounds incorporate movement,
parent changes and deletion. Picking uses its cropped camera and retains original
integer source IDs. Shadow casters remain independent of camera visibility.
完整输入校验先于裁剪；世界边界向外舍入，特殊边界直接齐次测试。视锥边界候选
仍精确测试，移动/层级/删除反映在新边界；拾取身份与画外阴影投射体保留。

Completed mesh/picking reports include indexed instances, node/bounds/exact tests,
unindexed tests, changed bounds, refitted nodes and rebuild status. Failures retain
the previous published report. The Rendering pane and `workspace.view.draws.spatial`
expose real mesh counters. Validation, world-bound generation and update comparison
still visit all inputs; this is not a full sublinear scene traversal or streaming
system. Refit preserves topology, so extensive movement can reduce tree efficiency;
adaptive rebuilding, separate per-camera caches and GPU-driven visibility remain
future work. Counters measure classification work, not frame-rate improvement.

## Authored tangent frames / 作者切线坐标框架

Vertices and DotMesh retain tangent XYZ and handedness W. Tangents are unit,
orthogonal to complete normals and W is +1/-1; all-zero denotes absence. A mesh
requires all or no authored tangents. GPU surface shading transforms tangents with
the model matrix and normals with inverse transpose, orthogonalizes the interpolated
frame, and accounts for reflected model handedness. Facing a double-sided normal
toward the observer preserves the UV bitangent orientation. Derivative reconstruction is
used only when tangents are absent. Authored frames support constant UVs without a
division by a degenerate UV determinant; positions must still form nondegenerate
faces, triangle corners share tangent handedness (split seams), and UV magnitudes
remain bounded. Flat-normal generation explicitly clears
tangents because its changed normals invalidate the original frame.
顶点与资产保留切线及手性；作者坐标框架直接进入 GPU，而缺失时才重建。模型反射
和非均匀缩放使用正确变换；生成平面法线明确清除失效切线，静态 glTF 导入在后续段落说明。

Baked occlusion is a fifth linear texture slot: R encodes visibility, strength
[0,1] mixes it with one. It multiplies indirect HDR/IBL alongside SSAO; direct lights
and emission remain unaffected. Renderer, scene cache, current material codec and
game packaging validate/bind this slot. Instance storage is 288 bytes with occlusion
parameters at offset 256; GPU surface/geometry layouts share that stride. Actual
texture identities participate in consecutive batching. Existing authoring materials
use the current complete schema including occlusionStrength/occlusionTexture.

## Material alpha coverage

`AlphaMode` explicitly selects Opaque, Mask or Blend. Source alpha is the product
of vertex color, tint, base-color factor and texture alpha (always linear). Opaque
ignores it and outputs one; Mask discards below a finite nonnegative cutoff and
outputs one for survivors; cutoff above one discards everything. Blend performs
straight-alpha over in linear HDR before tone mapping, tests existing depth and
never writes depth. Solid source order is stable; blended instance centers sort
back-to-front with stable source-order ties. Intersecting/large geometry can still
need subdivision or order-independent techniques; no refraction is implemented.

Surface, SSAO depth/normal guides, shadow depth and integer picking use the same
WGSL coverage helper. Mask holes do not write depth or cast shadow and do not pick.
Blend is excluded from shadow/SSAO geometry; zero-alpha fragments do not pick and
nonzero blended fragments pick the nearest actual triangle. IDs remain original
source IDs after sorting. Opaque coverage passes ignore texture alpha and retain
geometry-only grouping; masks/blends group by actual base texture/sampler identity.

Current material persistence includes required alphaMode/alphaCutoff fields. Static
glTF/GLB import retains texture/vertex/factor alpha and converts all three modes.
Opaque semantics are applied at rendering, so authored alpha survives later edits.
Rendering/inspection report solid and blended instance/draw quantities only after
successful submission. Feature Lab has actual Glass and checker Cutout materials.

`SceneRenderer::previewMaterial` owns one explicit unsaved material override by
portable asset path. It applies to every matching scene binding without file/scene
mutation. Description validation precedes cache invalidation; actual rendering uses
the existing bounded texture loading and reports image/normal/budget failures.
`clearCache` reloads textures while retaining the explicit override. Passing null
restores disk materials and invalidates only affected material entries. This is
used by material authoring, not by the game session's saved-asset path.

`Renderer::guiImage(Texture)` retains same-device texture ownership until GUI
submission/error cleanup, matching render-target images. It performs no implicit
encoding conversion: preview readback bytes are uploaded as Linear GPU textures to
preserve their existing display encoding. Actual GUI pixel tests verify this path
and foreign/empty texture rejection/lifetime. `SceneRenderer::resolveMaterial`
reuses its bounded shared texture loading, color-space/normal checks and complete-
load publication for independent mesh draws without scene or lighting mutation.
Editor AssetPreview owns a separate stable renderer/resolver and explicit studio
lights/optional baked HDRI, returning bounded display-encoded CPU images.

Editor PreviewWorker owns the isolated AssetPreview provider on its background
thread, including creation, studio decode/bake, render/readback and destruction.
Only owned request descriptions and display pixels cross the queue. GUI upload
and its renderer remain on the main thread. Same-slot tickets/generation checks,
bounded completions and joined shutdown preserve device/thread ownership; actual
worker GPU regressions cover the provider path. Inspect GUI integration through
the actual editor. Active driver calls are not forcibly
canceled, so shutdown may wait for their completion.

## GPU skin meshes

[Skinning](Skinning.md) provides native controllers sharing static mesh uploads and
immutable eight-slot influences. updateSkins publishes complete posed buffers and
exact GPU bounds only after batch validation; ordinary mesh views feed existing
physical color, shadow, SSAO and picking paths. readSkin is explicit owned diagnostic
readback; regular updates map only compact bounds/status. The status wait is
synchronous and has no populated-scene performance claim. Automatic imported-skin
scene resources, editor binding and animation mixing remain unfinished.
