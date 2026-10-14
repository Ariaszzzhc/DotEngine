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
per RGB channel, added before exposure; alpha is unaffected. Transparency blending,
image-based lighting and shadows are not delivered here; direct physical material
slots and lighting are described below.

Run the development-only native acceptance with:

```sh
build/debug/bin/DotRuntimeAcceptance --texture-test tests/fixtures
```

It loads actual PNG/HDR files, samples a retained texture through a material into a
persistent GPU target, checks actual RGBA pixels and confirms mesh/texture/target
release. Focused tests cover sRGB versus linear sampling, alpha, filtering, UV row
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

```sh
build/debug/bin/DotRuntimeAcceptance --hdr-test build/debug/tests/file_acceptance
```

This development-only case renders three cubes with increasing emissive radiance,
checks actual pre-mapping highlights above one, writes the GPU display image to
`hdr-tonemap.ppm`, presents through the native window, resizes and checks resource
release. Cube face colors visualize authored normals; this is not a lighting/PBR
or game-art showcase. Focused tests cover default gray references/highlight
separation, Reinhard exposure, single input/output sRGB conversion, unchanged alpha,
emissive draw/depth ownership, invalid input retaining both images, padded readback,
thread/foreign/moved owners and working-buffer reuse. Existing camera/tint/physics
pixel tests explicitly select linear diagnostics to keep their assertions focused.

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
applies to tangent X/Y before normalization. This implementation requires normal
source B>=128, keeping decoded Z positive through mip/filtering. Occlusion and
transparency blending are not implemented by this direct-light stage.

Lit meshes require complete unit normals; a requested normal texture also requires
nondegenerate position/UV triangles, UV magnitudes <=1e6, UV determinant >=1e-12
in magnitude and relative determinant >1e-6. Missing normals or unusable UVs fail
before drawing. Normal-map tangent frames derive from world-position/UV screen
derivatives, including mirrored handedness; stored/imported author tangents are
not yet supported. World normals use the inverse transpose, direction-preserving
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

```sh
build/debug/bin/DotRuntimeAcceptance --pbr-test build/debug/tests/file_acceptance
```

The native acceptance saves and loads a real scene, sphere mesh, fifteen material
assets and a normal-map image before drawing dielectric/gold/copper roughness rows
under two persisted lights. It checks HDR highlights,
writes `pbr-direct.ppm`, presents/resizes and verifies shared resource release.
Focused GPU tests compare an analytic frontal BRDF value, metal/dielectric response,
roughness peaks, point distance/range, spot direction, dark/emissive frames, packed
texture channels, normal/emissive maps, inverse-transpose/mirrored transformations,
UV handedness, orthographic reflections, the full 64-light array and invalid-input
retention. The sphere primitive supplies radial normals and explicit UV seams/poles.

Material assets, persisted scene lights and basic shared inspector/agent authoring
now use this renderer in preview, Play and exported games. Dedicated material tooling,
the requested project/game visual upgrades, IBL, soft shadows, SSAO, glTF import,
author tangents and large-scene performance remain incomplete; the gallery does
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
  "emissiveTexture": null
}
```

Each non-null texture slot contains `path` and a complete `sampling` object:
`colorSpace` (`linear`/`srgb`), `filter` (`nearest`/`linear`), `addressU`/`addressV`
(`clamp`/`repeat`/`mirror`), boolean `mipmaps`, integer `maxAnisotropy` (1..16).
Nearest filtering requires anisotropy one. Paths are portable, lowercase-suffix
`.png`/`.jpg`/`.jpeg` paths relative to the project root, including when the material
lives in a subdirectory. Color slots may be sRGB/linear; normal and packed MR slots
must be linear and require the physical model. Normal images require source B>=128.
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

```sh
build/debug/bin/DotRuntimeAcceptance --environment-test \
  /absolute/DotEngine/assets/environments/studio_small_09_1k.hdr \
  /existing/output-directory
```

This development-only driver verifies real preprocessing, HDR retention, BRDF
readback/shared release and writes a diagnostic Reinhard/sRGB cube cross. It opens
no desktop window. Input is the unmodified CC0 [Studio Small 09](https://polyhaven.com/a/studio_small_09)
resource with source/author/hash recorded in assets/environments/README.md. Runtime
fetches no assets/API. Unit tests check bounds/face axes; offscreen tests check
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

The offscreen environment acceptance also renders `environment-lighting.ppm`: six
actual HDRI-lit spheres with no direct lights (copper above, blue dielectric below;
roughness .06/.3/.8 left to right), plus sky, HDR readback and GPU ACES output.
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
Its existing native standalone acceptance validates that delivery path. This
provides authored IBL; soft shadows, SSAO, async baking/streaming and advanced
lighting/editor tools remain part of the active capability roadmap.
