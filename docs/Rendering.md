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

The native acceptance draws dielectric/gold/copper sphere rows with increasing
roughness, a normal-mapped sample and two direct lights; it checks HDR highlights,
writes `pbr-direct.ppm`, presents/resizes and verifies shared resource release.
Focused GPU tests compare an analytic frontal BRDF value, metal/dielectric response,
roughness peaks, point distance/range, spot direction, dark/emissive frames, packed
texture channels, normal/emissive maps, inverse-transpose/mirrored transformations,
UV handedness, orthographic reflections, the full 64-light array and invalid-input
retention. The sphere primitive supplies radial normals and explicit UV seams/poles.

These are renderer API capabilities. Material assets, persisted scene lights,
editor/agent authoring and the requested project/game upgrades remain separate
integration work. IBL, soft shadows, SSAO, glTF import, author tangents and
large-scene performance are not established by this material gallery.

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
