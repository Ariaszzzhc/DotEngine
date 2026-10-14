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
tests. The HDRI IBL row remains open until persistent authoring, asset caching and
editor/Play/export integration are implemented.

Development order follows dependencies: surface geometry and resource ownership,
texture/material rendering, authored light/shadow services and editor integration,
then content-production/scalability/game polish. Script namespace/resolution and
external-editor work are separate cohesive changes. Every stage needs runnable
acceptance and failure/boundary/lifetime checks before consumers rely on it.
Only arm64 macOS is locally validated; Windows/Linux implementation remains required.
