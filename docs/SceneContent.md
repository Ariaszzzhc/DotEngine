# Reusable scene content

Selected authoring subtrees can be saved as an ordinary `.dotscene` asset and
inserted into other scenes in the same project. Meshes, materials, clips, skins,
audio and scripts keep project-root paths; they are shared rather than copied into
a new package. The content file alone does not bundle those dependencies. This
provides independent authored copies; linked updates, overrides and prefab editing
are not implemented by these operations.

## Authoring and agent operations

Select one or more hierarchy entries. In Assets, enter a new `assets/...dotscene`
Path and Content name, then choose **Save selection as scene**. The last selected
entity determines the saved origin. Existing files, aliases and missing parents
fail; use the existing directory operation to prepare a parent. Saving content
leaves the active scene, scope, ordered selection, revision, dirty state, history
and entity allocator unchanged, including when the source has unsaved edits. It
publishes an external asset file, so document Undo does not remove that file.

```json
{"command":"save-selection-scene","revision":"12","path":"assets/MaterialStudies.dotscene","name":"Material studies content"}
```

The strict response owns the current document snapshot and `contentPath`. Use the
current revision, not the literal sample. Invalid/stale requests and external
project-metadata changes fail before publication. A response error after successful
file publication explicitly indicates that the operation may have committed.

Open the new asset with ordinary Open scene, edit, undo/redo and Save. Its content
need not contain a game camera; Scene view uses the editor camera. Select a
`.dotscene` asset and choose **Add scene instance**. It inserts at scene root with
identity placement and selects a new container; existing gizmos and hierarchy tools
then position or parent it. Agent callers can specify placement and parent in the
same transaction:

```json
{"command":"edit","revision":"13","operations":[{"op":"instantiate-scene","path":"assets/MaterialStudies.dotscene","parent":null,"transform":{"position":[2,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]}
```

Every field is required. Path may name an existing portable `.dotscene` under
assets or scenes. Parent is null or a live entity value in the current document;
transform is parent-local. Insertion creates a placement container plus every
content entity, returns all new values in `created`, and selects the container.
Native references map to these new values; existing entities and scene-wide
camera/environment/AO remain authoritative. Imported primary camera/listener flags
are demoted. One undo/redo entry restores content, transforms and selection together;
committed values are not reused on history branches. Malformed data, stale values,
invalid parents/transforms, missing/aliased sources, global scene limits and history
budgets reject the whole candidate without consuming published identity values.

Play, animation audition, active transform gestures, pending resource/entity drafts
and conflicting dialogs reject content saving. Ordinary authoring guards apply to
insertion. The GUI uses the same product operations; asynchronous mailbox
`queued:true` requires reading the applied response. A catalog refresh failure after
saving reports that the file was saved rather than claiming rollback.

## Native preparation

`Dot.World.SceneContent` is a CPU-only module with separate interface and
implementation. It performs no filesystem, window, rendering, audio or physics
initialization. The caller explicitly publishes the independently validated scene.

| Operation | Result and boundary |
| --- | --- |
| `extractSceneContent(source, selected, name)` | Descendant union, compact IDs and an identity container. Root world poses are flattened relative to the last selected entity's world origin; internal local transforms remain. Environment/AO are excluded; primary flags and opaque script data remain. |
| `prepareSceneInstance(target, content, firstValue, placement, parent)` | Fresh-scope candidate retaining target entity values, a placement root and owned source-to-target mapping. `firstValue` must exceed every live target value; authoring callers supply their history high-water mark. |

Both sources remain unchanged on success or failure. Returned handles address the
candidate's fresh scope; this is preparation, not in-place runtime instantiation.
Existing handles remain valid only in their original sources. Each new container
consumes one of the scene's 10000 slots and contributes to its depth limit of 256.
The final codec limit is 64 MiB. Creating-thread/moved-owner contracts remain the
same as Scene. No GPU or simulation service is implicitly staged.

Extraction accepts 1..10000 unique live handles. Ancestor/descendant overlap is
expanded once. Flattened roots require representable TRS; a sheared external-parent
basis fails rather than being approximated. Including that parent can retain the
local hierarchy. Skin joints and animation targets must remain within the extracted
union: select their common ancestor or all dependencies. These native references
remap on extraction and insertion. Arbitrary script JSON numbers/strings are not
interpreted as entity references, and resource paths remain project-root relative.
The mapping owns typed original/candidate handles and excludes the newly created
container, available separately as `root`.

Feature Lab's material content was validated by replacing the original studio group
with the saved asset at the original placement. Independent world-matrix comparison
and two distinct native viewport captures agree; this is specific workflow evidence,
not a claim of completed prefab or large-game authoring capability.
