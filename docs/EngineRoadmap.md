# Engine capability roadmap

The current project/file/script/native-service framework does not yet meet the
requested large-3D-game or mature-editor target. Existing passing tests establish
their specific contracts, not missing rendering, content-production or editor
capabilities. This roadmap keeps those requirements explicit across incremental
commits; its planned entries are not implemented features.

The engine is unpublished: change current structures directly, update all callers,
assets and tests together, and remove superseded implementations. Do not add old
format loaders, version dispatch or compatibility wrappers. Engine modules reserve
the `dot:` namespace; project imports use relative paths without extensions.

| Required end state | Work and evidence needed |
| --- | --- |
| PBR materials and textures | Retained normal/UV/tangent data; bounded image/model import; owned GPU textures/samplers; authored metallic/roughness PBR assets, GGX surface response and texture slots; serialization, asset references, editor inspection and actual rendered validation |
| Lighting and soft shadows | Authored directional/point/spot lights, physically consistent surface response, cascaded directional shadow maps, point/spot shadow resources and soft filtering, environment/exposure controls; dynamic edits, lifetime/failure tests and visual/native GPU evidence |
| HDRI image-based lighting | Bounded HDR import; environment rotation/intensity; diffuse irradiance, GGX specular prefilter and BRDF integration; real [Poly Haven HDRI](https://polyhaven.com/hdris) scenes with metallic/roughness response and owned GPU resources |
| HDR pipeline and tone mapping | Linear floating-point render targets, exposure, tone mapping and correct final display encoding; retain highlights above one, explicit color/data texture spaces, visual/native GPU evidence |
| SSAO | Depth/normal-based ambient occlusion, controllable radius/intensity, edge-aware filtering and stable resize/camera motion; quality and populated-scene GPU cost comparisons |
| Large 3D game production | Scalable resource loading, scene organization, animation, rendering queues/culling/instancing, scene and asset streaming; meaningful populated-content performance/lifetime evidence, not merely an empty entity-count test |
| Mature editor workflow | Docked scene/project/inspector views, independent editor camera, picking/gizmos, multiselection and transactions, asset/material/light tools, clear diagnostics, shortcuts and Play workflow; shared agent operations and actual visual/interaction review |
| External source editor | Open script/source diagnostics in the external default editor; retain agent source-file operations, compilation and hot reload; remove the embedded code-editor UI |
| Engine script namespace | All engine-provided modules use registered `dot:` names, starting with `dot:engine`; engine SDK ownership/type declarations cannot depend on confusing user-relative paths; unknown engine names fail explicitly |
| Project module syntax | Relative imports omit extensions; compiler and VM agree on deterministic resolution, including ambiguity/failure cases, immutable generations, hot reload and independent exported playback |
| Capability showcase | Feature Lab stays an independently editable editor project with real native features and complete SDK/component acceptance; demonstrations consume delivered engine capabilities |
| High-quality 3D example | Prism Relay becomes a realistic science-fiction game with coherent assets/materials/light, camera/HUD, polished gameplay/feedback and independent deployment; actual played and rendered evidence is required |

The engine script namespace and extensionless project-module contracts are
implemented with a shared registry/resolver, injected declarations, static linking
during Check, and actual native reload/export/distribution tests. This closes those
two rows. External source editing is now implemented through default-app opening,
project diagnostics, retained agent drafts/hot reload and removal of the embedded
code page. Material/light scripting accessors and the other listed work remain.

HDR environment GPU preprocessing is implemented and verified independently:
radiance cubes, diffuse convolution, GGX reflection mips and BRDF integration with
real Poly Haven input. Renderer PBR split-sum sampling and camera sky now consume
these resources, with a visually checked lit material gallery and scene-camera
tests. Scene-owned HDRI settings, shared asset caching and editor/Play/export
authoring now consume those resources. Background baking/disk caches, advanced
lighting controls and scene-production scalability remain planned.

Development order follows dependencies: surface geometry and resource ownership,
texture/material rendering, authored light/shadow services and editor integration,
then content-production/scalability/game polish. Script namespace/resolution and
external-editor work are separate cohesive changes. Every stage needs runnable
acceptance and failure/boundary/lifetime checks before consumers rely on it.
Only arm64 macOS is locally validated; Windows/Linux implementation remains required.

Directional cascaded depth shadows now render through saved lights and mesh
casting/receiving controls, with stabilized fits, filtered comparisons, overlap
blending, receiver-plane bias correction and actual pixel/lifetime evidence.
Lighting/shadows remain open for point/spot maps, contact-hardening, advanced
authoring and submission/scalability work. The explicit SSAO renderer API now has
CPU/offscreen/acceptance evidence, saved scene/editor controls and shared
preview/Play/export consumption. Temporal quality and populated-scene performance
remain planned. The other listed requirements
remain pending; this stage does not establish large-game or mature-editor completion.


Independent editor navigation, mesh world-bound framing and opaque GPU picking now
have CPU, offscreen, native-input and runnable editor acceptance coverage. Gizmos,
persisted user layouts, asset thumbnails and production scene tools remain planned.
Dark/light editor references guide compact chrome and hierarchy/viewport/inspector/
asset placement, while implemented engine behavior remains authoritative; see
[EditorDesign.md](EditorDesign.md).


Reference-guided docked hierarchy, Scene/Game, Inspector/environment/rendering and
Assets/Console windows now have real theme, search, queued docking and reset controls.
Pinned Sarasa UI SC Regular provides Inter Western/CJK text. GUI viewport rendering
resolves after actual layout with owned targets and failure/lifetime checks, preserving
first-frame/resize/undo/Play behavior. This improves the production workspace; persisted
profiles, manipulators, thumbnails, populated-scene scalability and realistic game
content remain unfinished.

World/local translation handles now edit selected entities through a disposable
preview scene and one release-time history transaction. Meter snapping, cancellation,
stale/conflict boundaries and real native/GPU workflows have dedicated coverage.
Rotation/scale and plane handles, multi-selection, persisted user profiles, thumbnails,
large-scene scalability and realistic polished game production remain unfinished.

Rotation rings and axis scale handles now share the translation gesture transaction,
with degrees/factor snap units, real affine parent frames and strict world-to-local
TRS failure handling. Exact agent samples and native ring/endpoint interactions are
covered. Plane/uniform-scale handles, multi-selection, persisted user profiles, asset
thumbnails, scalable production scenes and realistic polished Prism Relay remain
unimplemented. The full engine/editor/game objective remains active.

Conservative CPU frustum clipping now skips main/SSAO draw submissions while retaining
off-camera shadow casters and stable pixel-picking identities. Actual encoding-loop
counts and dense-scene/camera-change cases verify reduced submissions. This remains
linear preparation and a 4096-draw input bound; spatial acceleration, instancing,
streaming and large-scene production remain unfinished, alongside richer editor
production tools and the realistic polished Prism Relay game.

Stable instanced runs now share one compact surface upload across main/SSAO passes,
retain per-object parameters and original pick IDs, and batch per-cascade shadows.
The input/CPU caster bound matches the 10000-entity scene capacity; counters distinguish
instances from actual GPU commands. CPU scene traversal remains linear; spatial
indexing, streaming, production import/animation/editor tools and the polished
realistic Prism Relay game remain unfinished.


BVH spatial classification now reuses camera-independent world bounds, refits
changed branches and rebuilds on count changes. Dense offscreen inputs prune whole
subtrees; strictly inside subtrees avoid per-object tests. Stable source identity,
complete validation and off-camera shadow behavior retain coverage. CPU traversal,
validation and bound generation remain linear; adaptive tree rebuilding, streaming,
production import/animation/editor tools and realistic polished Prism Relay remain
unfinished. Classification counters are not populated-game frame-time evidence.


Authored tangent data now survives geometry validation, DotMesh persistence, GPU
upload and physical shading, including reflected/nonuniform transforms and double-
sided faces. Feature Lab demonstrates a saved authored normal-map basis. This fills
one model-import prerequisite; glTF/GLB parsing, materials/hierarchy conversion,
transactional import, animation and realistic game content are still unfinished.


Static glTF/GLB import now creates native PBR meshes/images/materials and hierarchical
model scenes through bounded parsing and no-replace asset publication. Editor/agent
operations instantiate the model with fresh IDs in one undo transaction; real GPU
pixels and native import/edit/save workflows have acceptance cases. Baked occlusion
now affects indirect light only. Animation/skin/morph/compression/transparent materials,
additional UV/filter modes, asynchronous import, production asset streaming and
realistic polished Prism Relay remain unfinished. This does not prove general-purpose
large-game production readiness or mature editor completion.


Material Opaque/Mask/Blend now spans native persistence, static model import, actual
HDR composition/depth, masked shadow/AO guides and integer picking. Transparent
instances use stable object-depth sorting; Feature Lab demonstrates actual alpha
assets and rendering counters. OIT/refraction/transparent shadow lighting, animation,
streaming and realistic polished Prism Relay remain unfinished.


Structured material Inspector now edits the actual native PBR/texture/coverage
contract with shared-scene unsaved preview, independent bounded undo/redo, conflict-
checked save and normal Play/export lifecycle. Material graphs, standalone resource
thumbnails/previews, broader production authoring tools and realistic game content
remain unfinished; this does not establish mature-editor or large-game readiness.


The asset browser now shows actual bounded PNG/JPEG, mesh and material thumbnails,
folder breadcrumbs, grid/list and project-wide search. Material Inspector has an
isolated studio sphere for unsaved forms; failure states and scene isolation are
verified. Asynchronous preview/import work, zero-copy previews, scene/model/audio
thumbnails, material graphs and broader production authoring remain unfinished.


Asset/material preview decoding, HDRI preparation, GPU rendering/readback and device
lifetime now run on an owned background worker. Bounded queues/backpressure,
coalescing/priority, stale result rejection, explicit initialization errors and joined
shutdown are implemented. Main-thread GUI upload, synchronous model import, broader
streaming/animation and production content workflows remain unfinished. This is
not a general asynchronous asset pipeline or large-game readiness claim.


Ordered scene multiselection and an active entity now span document history, GUI
Hierarchy/Inspector and agent operations. Modifier/range selection and transactional
subtree duplication/deletion retain parent links, native components, fresh IDs and
undo selection. Native pointer/shortcut and runnable document acceptance exercise
the workflow. Multi-object transforms/framing, box selection, mixed-value property
editing and persisted profiles remain unfinished; single-object gizmos are hidden
for multiple selections to prevent partial group edits.


The existing Move/Rotate/Scale gizmo now operates on frozen selections around the
active pivot. Topmost selected roots transform once, local group operations share
the frozen active affine frame, and unrepresentable samples roll back completely.
A typed atomic Document transform commit supports larger groups than JSON edit
operation limits, with real GPU/native input/history/save acceptance. Group
framing, median/individual pivots, mixed-value properties, production animation/
streaming and realistic polished Prism Relay remain unfinished.


Native immutable TRS clips now support Step/Linear/CubicSpline, strict native clip
assets and explicit scene-bound playback with reverse/loop/pause/seek/stop. Sampling
and local transforms commit atomically with clock state; actual offscreen reference
pixels, saved clips/nine frames and resource release have acceptance coverage.
This precedes consumers: glTF animated-node import, GameSession/editor/SDK playback,
skinning/morphs/blending/state machines/events and animation streaming are still
unfinished. Production animation and realistic polished Prism Relay remain open.
