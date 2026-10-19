# Transform animation foundation

`Dot.Animation.Clip` provides immutable shared TRS channel data. `Dot.Animation.Files`
loads/saves native `.dotanimation` clips, and `Dot.Animation.SceneAnimator` binds
logical node indices to live scene-scoped entity handles. This foundation is usable
by native engine consumers. glTF/GLB node animations now
convert to these clips and native model bindings. Saved animation players now run
through GameSession, editor Play and native exports. TypeScript playback controls
are available through the engine-owned SDK. Isolated editor audition supports playback
and time scrubbing; existing library clips now support native keyframe editing,
preview, save and undo in the editor. Production multi-key/curve-handle, graph and
binding workflows remain unfinished.
[Native GPU skinning](Skinning.md) now includes glTF/GLB import, scene/editor bindings,
isolated Play and owned exports. Native SceneAnimator cross-fades now have CPU and
offscreen skinned-GPU regression tests. Library/SDK transitions now run in editor Play and
relocated exports. Visual transition/character authoring, morph targets, blend
graphs/state machines, events/root motion and production animation streaming remain
unfinished; this does not establish full animated-model support.

Interpolation follows the [Khronos glTF animation specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#animations):
STEP holds the preceding key, LINEAR interpolates vectors and uses shortest-arc
SLERP for rotation, and CUBICSPLINE uses Hermite interpolation with tangents scaled
by interval seconds. Cubic quaternions are normalized after sampling; authored
quaternion signs and tangents are retained. Invalid zero quaternion/scale and
non-finite/overflow results fail explicitly. A channel clamps independently to its
own first/last key. Clip duration is the latest channel key, including leading
constant intervals and zero-duration static clips.

A clip owns 1..30000 unique node/path channels, node indices 0..9999, at most one
million aggregate keys and finite strictly increasing times in 0..86400 seconds.
Names are 1..256 UTF-8 bytes without NUL. Values are XYZW unit quaternions for
rotation and four-component vectors with w=0 for translation/scale; scale magnitudes
must be at least 1e-6. Cubic tracks require at least two keys and explicit in/out
tangents; non-cubic tracks do not accept nonzero tangents. Copies share immutable
storage, samples own their output and concurrent read-only sampling is supported.
Default/moved clip handles report errors. No device, file or hidden wall clock is
created by clip validation/sampling.

Native files use one strict JSON structure:

```json
{"name":"Sliding panel","tracks":[{"node":0,"path":"translation","interpolation":"linear","keys":[{"time":0,"value":[0,0,0,0]},{"time":2,"value":[4,0,0,0]}]}]}
```

Paths are translation/rotation/scale; interpolation is step/linear/cubic. Cubic
keys additionally require inTangent/outTangent arrays. Unknown/duplicate/missing
fields, wrong numeric types and invalid descriptions fail. Files are bounded to
64 MiB/depth 64 and project-root portable relative `.dotanimation` regular paths;
symlinks, special files and missing parents fail. Loading reads only the clip.
Saving encodes completely before atomic replacement and preserves the previous
file on validation failure. It does not create parent directories.

A SceneAnimator owns a clip and explicit node/entity bindings, not a scene address.
Bindings must be unique, live and cover every clip target in one scene scope.
All supplied bindings retain original rest values for later clip switches, even
when the current clip does not write them. Moving the scene retains valid handles;
changing its scope or removing any bound target fails. Animator operations require the creating thread;
moves preserve state, while moved-from operations return errors.

Playback uses explicit elapsed seconds (0..86400 per advance), configurable speed
in -16..16, forward/reverse looping, zero speed, pause/resume and checked seeks in
0..duration. A zero elapsed step preserves the exact seeked endpoint. Non-looping
playback holds its terminal sample and pauses. A zero-duration looping clip holds
its pose without division by zero. `play(restart)` changes clock/play state only;
`seek`/`advance` applies the pose. Pause freezes clock and scene. There is no hidden
system clock or automatic background mutation.

Each application samples all channels first, preserves current unanimated transform
fields and commits every node's local pose in one Scene::setTransforms transaction.
Animated parent/child transforms therefore combine through the real hierarchy once.
Invalid samples, removed bindings and final world/camera validation failures preserve
all node poses and the playback clock/state. Stop restores only animated fields to
the captured bind pose, preserving current unanimated fields, then stops at time
zero. `Scene::transform` returns an owned local pose without copying names/scripts
or unrelated components; animation uses this lightweight boundary. Playback currently
allocates owned sample/pose batches per application; allocation-free pose caches,
parallel evaluation and populated-scene performance are not established.

Offscreen tests compare animated samples against independently constructed native
reference poses. CPU unit cases never create a window/device; file tests are
integration cases. Only arm64 macOS is locally validated; Windows/Linux remain
implementation targets.

## Native cross-fades

`SceneAnimator::crossFade(scene, clip, seconds)` transitions to the incoming clip's
directional start. Duration is finite 0..86400 seconds; zero uses the immediate
switch operation. Both active clips advance with the current speed/loop settings.
Their translation/scale channels blend linearly and rotations use normalized
shortest-arc SLERP. `PlaybackState.time` is incoming clip time; its optional
`transition` reports unscaled `elapsed` and `duration` seconds.

Starting retains the visible local pose. Missing source channels start there;
missing destination channels return to original bind values. Unaffected fields
continue using current scene values. A paused or never-applied source stays frozen.
Pause freezes both clip clocks and fade; speed zero freezes only clip clocks.
Nonlooping endpoints hold until transition completion. A new transition during an
existing one freezes the current composite instead of retaining an unbounded layer
chain. At most 30000 distinct channel values fit the existing 10000-node bindings.
Seek, stop and immediate switching restore outgoing-only channels and cancel the
transition only after a successful scene transaction. `play(restart)` changes only
incoming time/play state; it does not restart the fade clock. Restarted clock-only
state counts as unapplied until the next pose sample, so a subsequent fade starts
from the still-visible pose.

Every sample and final world transform must validate before publishing poses,
incoming/outgoing clocks or fade progress. Singular blended scale, invalid source/
target samples, missing bindings and world overflow preserve all prior state.
`Math::slerp` also exposes the normalized shortest-arc rotation operation for other
native consumers, including finite-weight and nonzero-input checks.

This stage is the lower-level native transition
service. ScenePlayers authority reservations, SDK exposure, isolated editor Play/
export transition workflows and visual blend controls still need integration;
existing immediate library controls remain available. Layered/additive blending,
state machines, animation streaming and populated-game throughput remain unfinished.

## Imported model animation

`Dot.Assets.Model` reads glTF/GLB TRS channels and remaps source node indices to the
selected native hierarchy. Authored TRS signs/axes are retained for partial-channel
animation; matrix-target animation is rejected. Input/output accessors must be
nonnormalized floats without buffer-view stride; scalar input bounds must exactly
match finite increasing key times. Shared/sparse accessors use checked unpacking.
Rotation output is VEC4, other channels VEC3; cubic output contains in/value/out
triples per key. Existing clip validation also rejects duplicate targets, invalid
rotations and singular scale keys. Skinned models use the same joint-node channels
and native players; morph targets remain unsupported.

Import supports at most 1024 source clips, 30000 aggregate channels and one million
aggregate keys. Every channel is validated, including targets outside the selected
scene. Such targets are excluded with explicit warnings; empty resulting clips are
omitted. Names are retained, with stable source-index names for unnamed clips.

Native bundles contain `model.dotscene`, `animations/N.dotanimation` and
`model.dotmodel`. The descriptor records project-relative scene/ordered clip paths
and canonical string entity IDs for each logical node. Primitive entities do not
participate in the node index table. `Dot.Assets.ModelBundle::loadModelBundle`
returns an owned scene, immutable clips and live SceneAnimator node bindings; a
scene move retains those bindings. Files are independent of original glTF bytes.
No playback begins automatically. The descriptor is strict JSON <=1 MiB; encoded
clips total <=64 MiB with aggregate channel/key limits. References remain inside
the same model bundle; links, malformed/duplicate/missing IDs and wrong targets fail.
Import prepares the complete bundle in owned staging and publishes without replacing
an existing destination. This does not isolate hostile filesystem races.

The editor import command reports `model`, `animations` and its existing scene/
geometry fields. Native `.dotmodel`/`.dotanimation` assets can open externally.
Add model to scene remaps and retains the saved animation player and its node IDs.
GameSession/editor Play/native exports automatically play the selected clip. Existing
library clips can be edited through the editor's animation track/key workflow.

## Saved animation players

An entity may own an `animation` description: `clips` (unique assets paths), `targets`
(unique `{node, entity}` bindings with canonical string scene-local entity IDs),
`clip` (selected library index), `speed` (-16..16), `looping`, `playOnStart`,
`enabled` and the required `layers` array (empty when unused). `Dot.Animation.Source` validates/transports this strict description without
IO/devices. Scenes allow at most 128 players, aggregate 10000 bindings/1024 clip
paths, and 30000 masked layer channels. Targets must exist when authoring is published. Removing a referenced target
requires removing/rebinding its external player; a deletion containing the player
and all its targets is valid. Description snapshots copy no unrelated components.

Imported models attach a player to the first model node when clips exist. All nodes
are bound, including non-animated nodes required by other library clips. Insertion
and subtree duplication remap bindings with entity/parent IDs. Duplication requires
all of a copied player's targets to be included, ensuring independent instances.
Undo/redo/save and cloned editor Play preserve descriptions; Stop re-presents the
unmodified authoring scene. Inspector supports selected clip, speed, loop, auto-play,
enabled and clip-asset choices. Node bindings come from model import or native/
automation authoring; visual binding tools remain unfinished. See the isolated audition
workflow in [editor operations](Editor.md#isolated-animation-audition--独立动画预览).

`Dot.Animation.ScenePlayers` owns a shared scene and playback instances. Each
synchronize validates additions/changes as candidates before replacing its table;
unchanged descriptions retain clocks/rest poses, removed/disabled players release
playback, changed descriptions restart against the current local pose. Same-value
assignments retain playback. Disk clip edits require a changed description or a new
session. All library clips are validated, with 30000 aggregate channels/one million
keys per scene. All immutable library clips are retained within that aggregate budget for native switching.

Actual selected-channel writers must be disjoint across players. Rigid bodies on
an animated node or ancestor are rejected until explicit animation/physics authority
integration exists. These checks run before pose updates and include runtime edits.
Players reserve their selected writers even when paused; disabling releases them.
Each player's sampled pose/clock transaction is atomic; a later player's sample
failure does not roll back earlier players or completed scripts/physics callbacks.

GameSession preparation validates animation resources; start applies auto-play's
initial pose. Negative speed starts at the clip end, forward/zero at zero. Fixed
updates retain their script/physics order; variable update runs scripts, synchronizes
and advances animation once, then audio observes final poses. Native animation
failure faults the session until close, which releases playback while retaining the
inspectable final scene. No hidden timer, renderer or window is created. Scene
player synchronization reuses a completed binding/authority check while the scene's
scope-local `animationBindingRevision` matches. Animation property value changes,
hierarchy edits, physical-owner addition/removal and structural publication invalidate
it; transform/name/unrelated edits do not. Failed edits leave the stamp intact, and
failed synchronization never publishes a cached success. Saturation disables reuse
rather than wrapping. Runtime clip/fade controls validate new writers independently;
each applied pose still validates final world transforms/camera inverses. Prepared
asset updates always validate missing/duplicate/unused entries before publication,
even when the scene stamp matches. Populated-game throughput/streaming remain open.

## Runtime clip controls

Character controller roots and their ancestors are reserved for physics. Child
bones/meshes remain animatable. Clip switches and cross-fades validate this same
authority before modifying playback or pose; see [scene physics](Physics.md).

ScenePlayers provides owned state snapshots and explicit play/pause/stop/seek/
configure controls. They synchronize descriptions without advancing time and leave
saved player descriptions unchanged. Playing another library index restores the
previous channels to original binding values and applies the new directional start
in one local-transform transaction. Original bindings include every supplied node,
so switching disjoint clips does not accumulate stale transforms. Speed/looping
remain live runtime settings; same-clip play resumes unless restart is requested.
Restart/first play uses the end for negative speed, zero otherwise. Stop restores
only the selected animated channels and pauses at time zero.

Clip selection validates the new actual writers against other players and rigid-body
authority before applying. Failed targets, samples or final world transforms retain
the old clip, complete pose and playback clock. Reads are observational snapshots
of the last synchronized description; changed descriptions require synchronization.
Controls hold no borrowed scene address and remain owner-thread APIs.

## TypeScript playback

`dot:engine` exports `animation` and the owned `AnimationState` snapshot. Entity
provides `animationState`, `playAnimation`, `pauseAnimation`, `stopAnimation`,
`seekAnimation`, `configureAnimation` and `crossFadeAnimation` conveniences. Authored
enabled players and valid clip resources are required; no synthetic player is created for an entity.

| Service call | Behavior |
| --- | --- |
| `state(entity)` | Last synchronized clip index/asset/name, duration/time seconds, speed/looping/playing and nullable transition progress; returned values are owned |
| `play(entity, clip = null, restart = false)` | Resume current clip or atomically switch to a library index; explicit restart uses its directional start |
| `crossFade(entity, clip, seconds)` | Blend to a library clip's directional start over finite unscaled [0,86400] seconds; zero switches immediately |
| `pause(entity)` | Hold current clock and pose |
| `stop(entity)` | Restore original bound animated fields and pause at zero |
| `seek(entity, seconds)` | Apply a finite [0,duration] sample without changing playing state |
| `configure(entity, speed, looping)` | Set finite [-16,16] runtime speed and loop mode without rewriting authoring |

A resumed nonlooping endpoint stays at its endpoint; request restart to begin again.
ReadOnly state is available during constructors/state migration; every control is
ReadWrite and denied there. `Dot.Script.AnimationSession` owns the native backend
and shared scene; retained host bindings survive wrapper moves/destruction without
borrowing it. Foreign/stale/missing/disabled players and malformed arguments become
catchable DotHostError values. Native scheduling alone advances time. GameSession
prepares resources/services before script construction, runs start hooks before its
zero-time animation step, and variable scripts before native animation/audio.

```ts
import {input, ScriptComponent} from 'dot:engine';

export class Panel extends ScriptComponent {
  onStart(): void {
    this.self.configureAnimation(1, false);
  }

  onUpdate(): void {
    if (input.key('E').pressed) {
      this.self.crossFadeAnimation(0, 0.25);
    }
  }
}
```

Feature Lab demonstrates two saved clips, Digit 1/2 selection and Space pause/resume
with actual SDL input, native poses, editor Play and relocated standalone coverage.
Digit 1/2 now cross-fade over 0.25 seconds. An `AnimationState.transition` is either
null or an owned `{elapsedSeconds, durationSeconds}` snapshot; `timeSeconds` is the
incoming clip clock. Both clips advance during a fade. Pause freezes both clocks
and progress; speed zero freezes clip clocks while the fade still progresses.
Interrupted fades retain the visible composite pose without accumulating chains.
Seek/stop/immediate switching retire transitions after successful pose application.

ScenePlayers reserves outgoing/incoming entity writers, retaining interrupted
channels until completion or explicit cancellation succeeds. Competing players and
rigid-body ancestors reject controls before mutation. Failed samples retain all
clocks, poses and reservations. Once retired, outgoing-only entities can be owned
by other players; stopping the new clip cannot overwrite those released entities.
Saved player descriptions are unchanged. Visual transition/curve authoring and
populated-game scale remain unfinished.

## Keyframe editing data and resource drafts

`Dot.Animation.Edit::editClip` applies an ordered <=256-operation batch to an owned
candidate description and publishes one validated immutable native clip. PutKey uses
logical node/path and exact float seconds: insert in time order, replace an equal-time
key, or create a track with the specified interpolation. An existing track requires
matching interpolation; conversion is explicit. RemoveKey removes an empty track;
UpdateKey requires the existing exact source time and replaces that key in sorted
order, preserving the channel's interpolation. The complete replacement includes its
time/value/tangents; moving never creates a copy or overwrites an occupied destination.
Tangents remain authored derivatives per second, without automatic time rescaling.
RemoveTrack and RenameClip are explicit. SetInterpolation clears tangents when its
mode changes and retains them for the same mode. Cubic tangents can subsequently be
edited through PutKey. Validation occurs on the final clip, so a batch may replace
all old tracks or create a two-key cubic track without exposing an invalid intermediate
state. Missing tracks/keys, invalid times, modes, values or final empty channels fail
without changing the input. It creates no files, scenes or devices.

TransformKeys adjusts 1..16384 unique existing exact-time references in one immutable
candidate. Local translation adds XYZ, scale multiplies XYZ, and rotation right-
multiplies XYZW by a finite unit quaternion. Cubic scale/rotation derivatives receive
the same linear transform; translation derivatives stay unchanged. Non-cubic zero
tangents and vector W remain unchanged. Identity components skip arithmetic to keep
native float bits, including signed zero. Times/counts, interpolation and bindings
never change. Invalid parameters, references, arithmetic overflow or final native
validation rejects the entire batch. Selected-key group adjustment shares this
kernel with the editor's preview/history preflight; it does not author world-space
poses or normalize derivatives as quaternions.

TransformKeys 原子调整已有多键局部 TRS，平移加偏移、缩放逐轴乘系数、旋转右乘单位四元数。
三次缩放/旋转导数做同一线性变换，平移导数不变；恒等分量保留 float 位。时间、数量、
插值和绑定不变，错误拒绝整批；编辑器复用原生姿态/蒙皮预检和历史，不隐式编辑世界姿态。

`Clip::sampleTrack` evaluates one track index from this immutable clip description
with the exact ordinary native evaluator. It avoids evaluating/allocating samples for
unrelated channels, and therefore does not certify their current samples. Endpoint
clamps, shortest-arc rotation, normalized cubic rotation and singular/overflow failures
match `sample`. Results are owned and concurrent read calls remain safe.

UpdateKey 按精确原时间替换并重排已有键，不增加键数量，不覆盖已占用的新时间；插值模式
不变，切线保持每秒导数。单轨采样复用普通原生求值器，只验证所选通道，结果独立拥有。

`Dot.Editor.AnimationDocument` opens an assets .dotanimation using the existing resource
text/path/metadata/conflict implementation. Ordinary resource text defaults remain
1 MiB; this typed native-data editor explicitly requests 64 MiB, matching the animation
codec cap. Source bytes are preserved exactly for undo. Snapshots share immutable clip
handles and report revision, dirty/undo/redo, retained state count and serialized byte
count without repeatedly decoding the whole clip. All calls are owner-thread operations;
mutations require the current resource revision, including no-ops. Successful semantic
no-ops preserve revision and redo. Invalid edits/failed saves preserve the draft.

History defaults to 128 states and 128 MiB of serialized history text. Callers may
choose 2..128 states and a positive byte budget up to 128 MiB. Oldest states evict until
both limits fit; a single state larger than the budget fails without mutation. Shared
immutable clip data, current text and saved disk baseline are additional owned storage;
these counters are not a claim about total process memory. Undo/redo restore exact
retained state text; save retains history and checks the exact original disk bytes
before atomic replacement. External byte changes conflict even on a clean draft.
Destruction never saves. historyValue reads the next undo/redo candidate without moving
cursor or revision, permitting preview preflight before a same-revision transaction.

Native ScenePlayers can accept up to 1024 unique decoded ClipAsset entries at creation.
Each portable assets .dotanimation path must occur in an initially enabled player's
library, and its immutable clip must be valid. Exact path matches replace disk loading
for that service lifetime; ordinary binding, sample, aggregate-channel/key and writer/
physics authority checks still apply. Copies of the shared clip handles are retained;
no temporary resource files or source writes are used. Existing default callers still
load their actual disk assets.

GameSession scene replacement decodes complete initially enabled libraries on its
owned ScenePreparation worker and transfers immutable Clip handles, not World or
player handles. ScenePlayers::createPrepared and AnimationSession::createPrepared
require every enabled initial library asset, with no disk fallback. They keep native
target/authority and aggregate checks, then drop the temporary decoded lookup after
initial binding. Live players retain all library handles for normal playback/fades.
Subsequent authored-description replacement loads current files, preserving refresh
and recoverable rebind behavior. The existing scene bounds remain 1024 references,
30000 channels and one million keys; shared files count once in storage but every
reference counts toward library budgets. This does not move initial session startup
or runtime description reloads off the owner. The ordinary create(decoded) factory
continues to retain explicit immutable overrides for draft preview service lifetime.

AnimationPreview accepts an optional decoded draft from the selected player's library,
selects that library slot only in its isolated clone, and exposes the draft asset in its
native snapshot. Enabled skins are CPU-validated at the initial sampled pose before
publishing a preview. All other player/library validation remains active. Playback, seeking,
clip switching and skinning use the ordinary native implementation and retain the draft
after source animation assets retire. The editor connects these foundations to track/
key selection, staged values/tangents, atomic candidate preview, save/history and strict
agent operations; see [animation clip editing](Editor.md#animation-clip-editing).

关键帧编辑按节点/路径/精确 float 时间执行有界原子批次，最终统一通过原生片段校验。
动画资源草稿复用现有资源保存约束，支持精确字节撤销、版本检查、双重历史预算及磁盘冲突。
预览通过原生播放器持有不可变解码草稿，不创建临时文件，也不改写编辑场景。编辑器已
接入轨道/关键帧选择、值与切线草稿、图形切线事务、候选姿态预检、保存/历史及同源智能体操作；
实际鼠标验收、状态图、视觉绑定和生产规模验证仍待完成。

`Dot.Editor.AnimationKeyMove` owns an immutable clip and 1..16384 unique selected
source keys across channels. The last key anchors an absolute target time; updates add
the same double offset to every original source before native float rounding. Any
translated key outside 0..86400 rejects the update and retains the preceding ghost.
Selected sources vacate together. Unselected occupancy and duplicate destinations
caused by float rounding are visible per key and reject edit without discarding the
preview. Returning to origin restores exact source float representations. Values,
tangents and modes remain unchanged. Snapshots own ordered key payloads and aggregate
changed/occupied state, independent of later updates or tool lifetime. It creates no
scene, file, device, clock or history; update does not copy the clip.

RetimeKeys is a simultaneous native edit within an ordinary ordered ClipEdit batch.
It resolves up to 16384 unique exact source keys against the current candidate before
retiming/sorting each affected track once. Selected sources may swap or vacate for one
another; duplicate final times cannot overwrite any key. Input payloads, tangents,
interpolation and key counts remain unchanged. Numeric no-op targets preserve original
float bits. Invalid/missing/duplicate sources, nonfinite/out-of-range targets or final
collisions fail the whole batch. The final native clip still validates normally.

Copies of the tool are disabled; moves retire the source handle and destruction
discards the ghost. Independent tools share clips; synchronize mutable access. The
editor scopes the selection/gesture to current document identity/revision and native
preview preflight, and commits one history step. See [clip editing](Editor.md#animation-clip-editing)
for shared GUI/agent selection, move and relative TRS value adjustment operations.
Cubic derivative handles now share owned ghost transactions with agents; physical
pointer acceptance and state/layer authoring remain unfinished.

多键移键持有不可变片段，至多 16384 个跨通道唯一源键，末键锚定绝对时间；每个原键加
同一 double 偏移后转 float，任一越界保留前一预览，舍入重叠及未选键占用可见且可恢复。
组内同时腾位，原位恢复精确保留 float 表示；值/切线/模式和数量不变。RetimeKeys 在
普通有序批次中同时解析全部源键，统一重排受影响轨道，最终片段仍须完整校验；失败不改源。
工具不涉及场景/文件/设备/历史，编辑器校验身份/版本及原生姿态后提交一步历史。


## Owned key groups and clipboard preparation

KeyReference and the shared MaxSelectedKeys bound (16384) belong to native clip data.
Dot.Animation.KeyGroup copies ordered selected records into immutable owned storage,
with original channel modes and minimum/maximum source time. Copies share that data;
default/moved handles report errors. Source clip retirement or later edits cannot
change the group. Partial cubic selections are valid groups even when they would not
form a valid standalone clip. at(time) returns owned translated records with one double
offset from the earliest source key, before native float rounding. Every target must
fit 0..86400 and remain unique within node/path; failures preserve the group and origin
placement retains original float representations. It touches no scene, file or device.

Native PasteKeys merges those records into each affected candidate track in time order.
Existing channels require matching interpolation; absent ones inherit source modes.
Reject refuses occupied times, and explicit Replace replaces only equal-time keys.
No mode conversion, tangent rescaling or binding remap is implicit. The final clip,
including cubic minimum key counts, still validates after the complete ordinary edit
batch. Failures retain the source clip and owned group.

Dot.Editor.AnimationClipboard owns a workspace clipboard without an OS clipboard,
window, renderer, file access or hidden clock. Copy validates portable asset provenance,
canonical source identities and unique bounded node/entity bindings, including every
selected node, before replacing content and advancing its ID. Failed copies retain
content and ID; clear retires an ID without reusing it. Immutable shared snapshots
retain source metadata and KeyGroup data after replacement, clear or service retirement.
The service is noncopyable/nonmovable; synchronize mutations with other calls.

Preparation requires the exact original authoring scene/player/full binding map and
current clipboard ID. It reuses native PasteKeys and returns a candidate plus ordered
target references, without changing the clipboard or any resource/preview/history.
The editor performs its actual native pose/skin preflight before one publication;
source/destination resources remain distinct from the authoring scene. Every UI action
has the same strict [editor operation](Editor.md#animation-clip-editing). Retargeting,
physical tangent-pointer acceptance and layered/state-machine tools remain open.

KeyGroup 拥有不可变、有序的所选记录及原模式/时间范围，可包含部分三次键；源片段销毁/
修改不影响它，放置结果独立拥有，时间越界或 float 重叠时不改原组。PasteKeys 按时间归并，
默认拒绝占用，显式替换只覆盖同时间键；模式转换、切线缩放和绑定重定向均不隐式发生。
AnimationClipboard 在无窗口/文件/设备环境中校验来源与节点绑定、管理单调标识及拥有快照；
同场景/播放器/完整映射才可准备候选，实际姿态/蒙皮及一步历史由编辑器统一处理。

## Multitrack timing and key removal

Dot.Editor.AnimationTimeline owns an immutable native clip and a sorted node/path
index. It provides bounded exact/density row pages and inclusive rectangular key
selection, without window/device/files or mutable shared caches. Dense bins retain
actual first/last times and counts; selection still resolves source keys within the
native 16384-key limit. Source retirement and const concurrent queries are supported.

RemoveKeys is one ClipEdit operation over 1..16384 unique existing exact references.
It removes emptied channels and preserves all remaining key payloads/modes. Final
native validation rejects an empty clip or a single-key cubic channel; no hold key
or interpolation fallback is inserted. Ordered batches remain atomic. The editor's
Delete selected keys uses ordinary history and native skin/authority preflight.
See [the shared editor workflow](Editor.md#multitrack-dope-sheet--多轨时间表).


## Rational frame editing and paused seeks

Dot.Editor.AnimationFrames provides bounded rational view grids, nominal ruler ticks,
nearest native samples and adjacent frame stepping. Rates are editor preferences;
assets remain float seconds. Mutations reject a native float sample that maps to a
different nearest valid frame, rather than silently accepting a long-duration alias.
See [frame editing](Editor.md#animation-frame-grid--动画帧网格) for limits and strict controls.

SceneAnimator, ScenePlayers and AnimationPreview seek accept an optional pause flag
(default false). The animator commits that flag only after successful pose application,
together with the new clock and transition cancellation. Invalid times/samples retain
the prior pose, clock and playing state; controls still synchronize player descriptions
according to the existing ScenePlayers contract. Existing runtime seek behavior is unchanged.


`Dot.Editor.AnimationTangent` owns an existing cubic key's component and used side.
The native float derivative is per second; quaternion derivatives remain raw XYZW.
Control points use a signed third of the adjacent interval. Invalid keys, channels,
components or unused endpoint sides fail. update changes only one scalar and rejects
nonfinite values without discarding the previous ghost. Numeric return to origin
restores original float bits. Snapshots own their key payload; value builds an
ordinary native UpdateKey candidate, preserving key time/value, other derivatives,
track mode and unrelated channels. A no-op shares the immutable original. There is
no scene/file/device/history. Move retires the source and destruction discards the
ghost; mutable access needs synchronization. The editor checks current resource
identity/revision, samples native ghost curves, and preflights pose/skin before one
history commit. A valid clip can still fail at an intermediate sample; failed commit
retains the transaction for correction. See the editor's tangent handle contract.

切线工具持有现有三次键的一个分量/有效侧，导数按秒，四元数保留原生 XYZW。快照拥有键值，
更新只改一个有限标量，失败保留候选；回原值恢复位表示。原生 UpdateKey 候选保留其他数据，
无变化共享原片段；不触及场景/文件/设备/历史，编辑器负责身份检查、姿态/蒙皮预检和一次发布。


## Masked animation layers / 遮罩动画层

`Dot.Animation.Layers` owns up to eight ordered layers and 30000 aggregate masked
channels. A layer uses one library clip, a unique 1..64-character ASCII identifier
(letters/digits/_/-, starting with a letter), a nonempty list of unique logical
node/path/weight entries, weight [0,1], speed [-16,16], looping, playOnStart,
explicit mode (override/additive) and referenceTime in seconds.
Each channel must actually exist in that clip; even stopped layers validate their
masks and reserve their bound entity writers. Different layers may affect the same
channel, composing in list order. Scene authoring checks structure/bindings/budgets
without reading files; audition, Play and export validate actual clip channels and
native authority. No fallback decoder or format-version layer is added; current
saved player descriptions explicitly carry layers, including empty arrays, and
each layer carries mode/referenceTime. Missing/unknown fields reject; there is no
old schema reader.

```json
{"name":"Inspection","clip":7,"mask":[{"node":8,"path":"rotation","weight":1}],"weight":1,"speed":1,"looping":false,"playOnStart":false,"mode":"additive","referenceTime":0}
```

Native base/cross-fade samples compose first, then each active channel uses the
product of layer/channel weights. Override linearly interpolates local translation/
scale and uses shortest-arc quaternion SLERP for rotation. Additive captures masked
reference channels once at referenceTime in the same layer clip, then uses:

- Translation: current + weight * (sample - reference).
- Rotation: current * slerp(identity, inverse(reference) * sample, weight), using
  normalized quaternions and the shortest arc. This is a local post-multiplied offset.
- Scale: current * lerp(1, sample / reference, weight), componentwise; signed scales
  are preserved, and a singular/nonfinite final pose rejects atomically. Offsets/
  ratios use double arithmetic;
  overflow rejects before conversion to float, preserving the previous pose/clocks.

Additive referenceTime must be finite and inside [0,clip duration], including the
terminal sample without wrapping. Override requires exactly zero. References must
sample successfully even for stopped/zero-weight layers; zero/near-zero sampled
scale components reject under the existing clip limits. Only masked tracks are
reference-sampled, so unrelated tracks cannot invalidate a reference. Reference
values share immutable configuration ownership and never follow the layer clock.
At reference time an additive layer contributes identity, preserving the current
base or preceding layer. No inverse bind matrices, world-space offsets or retarget
assumptions are substituted.

A masked channel absent from the base starts at its captured bind value, not last
frame's layered result. Unmasked fields remain owned by the base/current scene. Zero effective contributions and unmasked
layer tracks are not sampled; a missing mask channel is still a definition error.
Intrinsic base sample failures are not hidden by an overriding layer. All layers
and the base share one final scene-transform transaction. Invalid samples or a
singular final mixed scale preserve all poses, base/transition/layer clocks, weights
and ownership. Cross-fade interruption captures an unlayered origin, so overlays
are applied exactly once. Independent layer clocks survive base clip switches/fades.

Native `Layers` functional candidates share immutable clips/masks and own small
playback vectors. Snapshots own names, mode/reference-time and clock/weight/fade
data and do not borrow the service. SceneAnimator applies and then publishes the
prepared clocks without allocating after pose commit. Clock-only/unchanged/inactive controls do not prime
an unapplied base pose. ScenePlayers reserves every masked entity even at zero
weight/while stopped, retaining the existing whole-entity writer exclusion and
rigid-body/character-root authority checks. Earlier successful players are not
rolled back if another player fails. Owner-thread and scene-scope rules still apply.

Explicit player pause freezes every clock. A naturally ended base clip holds while
layers continue. Player `playOnStart:false` starts with this global pause; layer
`playOnStart` selects active layers, which advance once the player resumes.
播放器的 `playOnStart:false` 初始总暂停；各层自动播放只设置初始活动状态，恢复播放器后才推进。
Base speed zero freezes its own clip clocks; layer clocks and
weight fades remain independent. Layer pause freezes its clip and fade while
holding contribution; play with restart:false resumes, restart:true selects its
directional start. Stop removes that layer and resets its clock. Nonlooping layers
hold their terminal contribution until explicitly stopped/faded. Fade uses unscaled
seconds [0,86400], can be interrupted from its current weight and optionally stops
at zero. Only zero-target fades may request stop. Zero duration applies immediately.
Game simulation pause supplies zero elapsed time, freezing base and layers together;
explicit scoped diagnostic controls can still recompose the paused pose.

The strict shared control objects are:

```json
{"action":"play","restart":true}
{"action":"pause"}
{"action":"stop"}
{"action":"seek","seconds":0.8}
{"action":"weight","weight":0.5}
{"action":"configure","speed":1,"looping":false}
{"action":"fade","weight":0,"seconds":0.15,"stop":true}
```

`animation.layer(entityId,name,value)` and `Entity.controlAnimationLayer(name,value)`
use the same native implementation. Native scheduling alone advances time; no JS
pose mixing, manual animation update or hidden clock is introduced. Saved definitions
are unchanged by controls. `animation.state`/`Entity.animationState` now include
explicit player `paused` and owned `layers` with name/mode/referenceTimeSeconds/
duration/time/weight/speed/looping/playing/active/paused/fade. playing describes the clip clock, active the
contribution, paused an explicit clock freeze; these are different states. The
player's paused flag is separate from the scene's game-clock pause. Weight fade
snapshots include from/to/elapsedSeconds/durationSeconds/stop, or null.

Editor draft recreation restores a complete ordered layer snapshot with the base
in one transaction, never replaying partial combinations. It validates count/name
identity, clamps times only when edited durations shrink, preserves weights/fades/
active/play/pause state and retains ordinary native/skin preflight. A singular
intermediate partial combination can therefore be avoided when the complete final
composition is valid. Controls and failed edits retain the preceding resource and
preview. Existing initial-pose validation still applies during preview creation.

遮罩层保存库片段及显式逻辑通道，按顺序混合本地 TRS；覆盖模式权重为层/通道乘积，旋转走最短
SLERP。未被基础片段动画的遮罩字段从绑定值开始，不反馈累积。零贡献/未遮罩轨道不采样，
但定义必须有效且始终保留实体写入权。一次原生事务提交基础和全部层，失败保留全部姿态/
时钟/权重。基础自然结束不阻止层推进；总暂停、层暂停、片段结束及停止有不同语义。
权重淡化由原生未缩放时钟推进，可在零权重退休；场景暂停冻结调度，显式调试控制仍可重构
暂停姿态。编辑器、SDK、独立游戏共用控制与校验，草稿重建一次恢复完整层状态。
叠加层从同片段固定参考时刻捕获遮罩通道，以局部位移差、后乘最短旋转差及缩放比叠加
当前候选；参考不随时钟改变，不从上次输出累积。覆盖层参考时刻须为零；叠加参考须处于
片段时长内并能合法采样。所有层共用最终事务，奇异/非有限结果不发布、不推进任何时钟。

Layers do not implement IK, root-motion extraction or production-scale guarantees.
Saved state machines and explicit offline retarget authoring are documented below.


## Saved state machines / 保存的动画状态机

A player's required `controller` field is null for manual clips or a portable
`assets/... .dotcontroller` path. The resource is strict JSON (at most 1 MiB/depth
16) with exactly name, initial, parameters, states and transitions. Parameters have
unique ASCII identifiers and Boolean or finite numeric defaults within ±1000000.
Up to 64 parameters and 64 states are supported. Each state has a tagged `motion`,
speed in [-16,16] and looping mode. A single motion is
`{"kind":"clip","clip":"assets/idle.dotanimation"}`. A synchronized one-dimensional
motion is `{"kind":"blend1d","parameter":"Speed","points":[{"threshold":0,"clip":"assets/idle.dotanimation","phaseOffset":0},{"threshold":3,"clip":"assets/run.dotanimation","phaseOffset":0}]}`.
All clips belong to the player's existing library. Blend axes must name a declared
numeric parameter; 2..16 finite, strictly increasing thresholds lie within ±1000000.
Normalized phase offsets lie in [-1,1]. There is no legacy state-level clip field. Up to 512 named,
ordered transitions have from/to, at most eight AND conditions, fadeSeconds,
minimumSeconds and nullable exitTime. Comparison operations are eq/ne/lt/le/gt/ge;
Booleans support only eq/ne. Fade/minimum seconds lie in [0,86400], exit progress
in [0,1]. Missing/duplicate names, mismatched types and unknown fields fail.

A positive native simulation update chooses the first eligible outgoing edge at
most once, including cycles/self-edges. Minimum time checks the source state's age
at tick entry. Exit progress includes directional travel crossed during this tick,
so a looping clip's wrap cannot skip its end threshold. Zero-length clips have
progress one. The selected destination consumes the whole tick; its age starts at
the tick delta. Age and fades use simulation seconds independently of clip speed.
Zero delta/global player pause do not evaluate edges or age. A finished nonlooping
clip can still transition on later ticks. Speed zero freezes clip progress while
state age continues. There is no expression VM or implicit input inference.

The player reserves all entities written by every controller state, plus layer
masks, for its lifetime. Native character roots/ancestors, rigid bodies and other
players cannot acquire those writers. A transition prepares the complete pose,
clocks and graph candidate before one scene commit; failure retains the prior
state, age, fade, clocks, layers and pose. Previously accepted parameter intent
remains accepted. Atomic switches restore departed channels/entities, even when
incoming tracks write different fields. Outgoing/incoming clips retain their own
speed/loop settings during a fade; interruption freezes the current base blend.

`animation.parameters(entity, values)` / `Entity.setAnimationParameters(values)`
atomically set a declared typed subset, without sampling or advancing. Snapshots
include nullable controller {asset,name,state,elapsedSeconds,parameters,
lastTransition,transitions}; transition counts are decimal strings. Constructors
and reload migration may observe but cannot change the service. It belongs to the
scene and survives source reload independently of VM state.

While bound, explicit base clip/restart, crossFade, seek, stop, configure and manual
playback restoration reject rather than fighting controller ownership. Parameterless
play resumes; pause freezes all clocks. `animation.resetController(entity)` /
`Entity.resetAnimationController()` atomically restore defaults and entry directional
pose while retaining global pause and layer clocks. Saved manual clip/speed/loop
settings return after explicit detach. Checkpoint persistence is a game policy,
not automatic state-machine serialization. Multidimensional blend trees, hierarchical machines,
triggers, events and root motion remain future work.


A 1D motion clamps outside its axis and linearly weights the adjacent two points.
The effective cycle duration is their weighted clip duration. Changing the axis on
a positive unpaused tick preserves normalized phase, then advances by the actual
simulation delta. Each clip samples this common phase plus its offset: looping
wraps, nonlooping clamps, reverse starts at the directional end. Zero-duration
clips remain constant. Vec3 channels interpolate linearly; rotations use shortest
quaternion SLERP. Missing channels use the owned bind pose, never the previous
output; zero-weight points still reserve their writers. The complete motion,
transition and layers publish in one transaction; a singular/nonfinite pose retains
all prior applied motion values and clocks, while accepted parameter intent remains.

Controller observations include nullable `blend` with parameter, applied value,
normalized phase, incoming-motion contribution, applied flag and positive-weight
samples {clip,asset,weight,timeSeconds,durationSeconds,phaseOffset}. Sample weights describe the
incoming motion internally; `contribution` is its cross-fade factor (one without a
transition). `applied` is false before the first sample. Pending `parameters` can
differ while paused or after a failed tick; changing intent does not publish a pose.
The top-level clip/asset/name identifies the dominant incoming sample (lower point
wins ties), while duration/time refer to the effective cycle. A state transition
can still reset the incoming phase; retuning within a state does not. These are
explicit authored offsets, not automatic foot contact alignment or root motion.

一维动作按相邻阈值插值权重与周期时长；改变数值保留归一相位，再按实际模拟步进。
各片段使用共同相位加显式偏移；循环回绕，非循环钳制。缺失通道取绑定姿态，零权重
通道仍预留写入权。姿态、过渡和层一次发布，失败保留已应用动作与时钟。
观测区分待处理参数与已采样值；权重属于输入动作内部，contribution 是状态间淡入比例。
暂停不应用待处理参数，默认重置可显式采样。未实现自动接触对齐、足部锁定或根运动。

状态机是严格资源而非脚本选片逻辑；类型参数表达意图，原生模拟按出边顺序每步至多转换一次。
零时长或播放器暂停不决策、不增龄；失败保留状态、双时钟、叠加层及姿态。绑定期间预留所有
状态的实体写入权并拒绝手动争抢基础播放；显式重置恢复默认参数/入口且保留暂停和层时钟。
源码重载保留原生服务，持久化检查点由游戏自己定义，不自动保存运行时状态。


## Saved motion retargeting / 保存的动作重定向

`Dot.Animation.Retarget` bakes an owned native clip from two saved reference rigs,
using explicit one-to-one logical binding IDs and checked names. Capture includes
intervening helpers and omits the owner's external parent. Mapping an owner anchor
is forbidden, so character placement stays outside the emitted animation. Nearest
mapped ancestry must agree through helpers. Reference local scales must be positive
and uniform; source scale keys must match the reference within the 1e-5 relative
import tolerance, and cubic scale tangents must be zero. Unsupported changes reject
even when they fall between bake samples.

Choose corresponding reference poses before mapping: the same physical arm/leg
posture, not merely matching names. The native algorithm transfers source world
rotation changes relative to its reference in the fixed anchor orientation frame,
then applies them to the target reference and derives local rotations. It does not
automatically align rest poses or infer humanoid roles. Different reference arm
postures produce different arm postures in the result. Unmapped descendants retain
the target reference pose; the output covers complete bound subtrees of mapped
roots, and leaves all outside nodes untouched. Target bone lengths/local scale stay
at the saved target reference. Optional displacement transfers one mapped root's
world-space offset with an explicit scale; it is bone motion, without physics root
motion extraction, foot locking, IK or automatic proportion/contact correction.

A rational rate between 1 and 240 Hz produces linear TRS keys, including the exact
endpoint once when a rounded grid point reaches it. This is a sampled approximation,
not exact preservation between samples. Limits are 8192 samples, one million keys
and four million evaluated rig nodes. Input clips/rigs remain unchanged. Invalid
references, hierarchy, mappings, unsupported scale or overflow yield contextual
errors and no partial clip. Reference rigs have at most 10000 nodes/depth 256.

`Dot.Editor.AnimationRetarget` prepares on an owned cancellable CPU worker. It
freezes authoring scene/project metadata, reads the exact saved scenes/clip, validates
target names/topology/helpers and the source library, then runs actual native
preview/writer/skin preflight. No devices, script hooks, file writes or component
publication occur during preparation. Ready owns a clip/proof; publication rechecks
exact source/reference/clip/project bytes and authoring revision/scope/selection.
Changing a file requires preparing again. Creation uses the native animation 64 MiB
text limit separately from generic 1 MiB text documents. The scene component remains
an Inspector draft until Apply. Existing controller, flags and layers are retained;
a bound controller still owns the base, so audition an individual clip or explicitly
change that binding before using the new clip as the base.

`.dotretarget` is a portable strict JSON profile of at most 512 KiB with exactly
sourceScene/sourceEntity/targetScene/targetEntity/clip/name/rate/mappings/translation.
Scene references and source clips live under assets; entity values are canonical
decimal strings, while mapping IDs and rate parts are unsigned numbers. Translation
is null or {target,scale}. No imported-role guessing, format version or fallback.
Profiles remain editable resources; exported runtime playback consumes the ordinary
baked clip and needs no live retarget service. See the
[editor workflow](Editor.md#animation-retarget-authoring).

先匹配源/目标的物理参考姿态，再显式映射名称和逻辑节点。原生烘焙传递相对参考的世界
旋转差，保留目标骨长及实例放置；未映射后代保持目标参考。可选择一个映射根的显式比例
位移，但不提供自动姿态对齐、脚部锁定、IK 或物理根运动。准备任务不写文件，发布前重验
输入和编辑场景；创建资源与 Inspector Apply 是两个明确步骤。


## Capturing reference poses / 捕获参考姿态

Pause isolated audition at the desired composed pose, then **Save pose as scene**
in the animation controls. This creates ordinary editable `.dotscene` content under
assets, using the selected preview player's complete subtree. The pose includes the
base/controller, current cross-fade and ordered layers already sampled by native
playback. Source scene, clip resources, clocks and preview generation stay unchanged.
Saving requires exact authoring revision, preview session and generation, a healthy
explicitly paused preview (or binding pose), and clean resource forms without another
job/gesture/dialog. A naturally ended base alone is insufficient when the preview
has not been explicitly paused.

The native capture uses scene-content extraction, with a fresh identity container,
compact entity values, internal skin/animation reference remapping and the selected
owner's world origin removed from placement. Unlike generic subtree extraction,
pose references retain the source environment for usable standalone PBR inspection;
insertion still preserves the destination environment through ordinary content rules.
Direct lights/AO/cameras outside the subtree are not invented or copied. Native
references outside its subtree
and sheared root flattening reject. It validates actual current CPU skin assets at
the captured pose before publication; stale cached preflight is insufficient.
Every copied animation player is disabled with autoplay off so loading animation
services preserves this saved pose. The current preview scene's library/controller/layer descriptions and other
components remain available for inspection/editing; live clocks are not serialized.
Individual clip audition retains its explicitly detached controller in this copy. This freezes animation components;
other behavior remains ordinary saved component data. Meshes, inverse bind matrices,
skins, scripts and clip assets are referenced, not duplicated or rebound.

The result is reusable posed content and a retarget reference, not a new skeleton
bind, automatic rest-pose alignment or an animation clip edit. Open it through the
normal scene tools to adjust bones, undo/redo and Save, then choose its reported
player value in retarget reference inspection. Corresponding source/target physical
postures still need review. Play or insert a captured rig with animation disabled
to keep the saved pose; explicitly enabling a player starts its normal native library
and controller semantics again.

`AnimationPreview::capturePose` returns owned content, mapped owner and generation;
retiring the preview leaves it valid. `Document::saveContentAs` shares native scene
resource publication with selected-content saving: check authoring revision/project
metadata, use the native 64 MiB scene budget, refuse existing files/aliases and
preserve document history, dirty state, selection and allocator. Publication is an
external resource write; scene Undo does not remove it.

在暂停的实际合成姿态上保存普通场景参考；副本拥有自身身份，原文档、片段、时钟和代次
不变。复制的动画组件关闭播放，保留其描述及其他组件；网格/蒙皮资源不复制、不重绑。
先校验实际蒙皮，原生引用须在所有者子树内闭合。可用普通场景工具编辑骨骼、撤销和保存，
再作为重定向参考；这不提供自动参考姿态对齐。

## Two-bone pose construction / 双骨骼姿态制作

`Dot.Animation.TwoBone::solveTwoBone` constructs an owned pose for a direct
root/middle/end scene chain. Target and pole are world positions; the pole's
projection perpendicular to the root-target axis chooses the bend plane. The
analytic triangle uses the [law-of-cosines construction](https://www.ryanjuckett.com/analytic-two-bone-ik-in-2d/).
It rotates the root and middle toward that triangle, then compensates the end's
local rotation to retain its original world orientation. Local translations and
scales remain exact; branches follow their normal hierarchy. The result contains
three scoped `World::TransformUpdate` values without changing the source scene.
Apply them together through the native scene/document transaction before replacing
or otherwise editing that scene. Results own their values; they do not borrow a rig,
skin, file, device or source lifetime. Scene owner-thread rules still apply.

The chain must have distinct current-scene handles, direct parent links, positive
uniform local scales (relative tolerance 1e-5, minimum component 1e-6) throughout
its ancestors, and at most 256 ancestors including the end. World segment lengths
and root-target distance must be at least 1e-5. A pole at the root or within 1e-5
relative distance of the target axis rejects instead of inventing a bend direction.
Zero-length, reflected, nonuniform, nonfinite, unsupported shear and insufficient
world-coordinate precision reject without writes. Antiparallel shortest swings
choose a deterministic perpendicular axis. Unreachable goals clamp to the closed
interval `[abs(upper-lower), upper+lower]`, preserving segment lengths; the result
reports `clamped`, actual float-hierarchy `reached` and `residual` to the requested
goal. Clamping is visible to the caller, rather than a claim that the hand reached it.

This is an ordinary authoring pose edit. Save a reference scene or create a native
clip from that pose and use normal key editing, audition, Play and export. It does
not create a persistent runtime constraint, enforce anatomical joint limits, solve
collisions or ground contacts, calibrate reference skeletons, lock feet, retarget
proportions or extract physics root motion. Animation playback can overwrite the
authored rotations. Native skin binding and inverse-bind resources remain intact.

双骨骼求解使用场景世界坐标的目标及 pole，保留骨长和末端世界朝向，只返回三个拥有式、
当前场景作用域的变换，不修改输入。编辑器将它们作为一次可撤销事务提交，可保存为参考
场景或通过现有工具制作原生动作。不可达目标显式报告夹取和实际残差；退化骨链、无效
弯曲平面及不支持的缩放/精度拒绝。该工具不等同于运行时约束、碰撞/关节限位或脚部锁定。

## Keying an IK pose / 以 IK 姿态写键

`Dot.Editor.AnimationPosing::prepareTwoBoneKeys` turns a current posed scene and
explicit native clip time into three rotation keys. The logical end and both direct
parents must be uniquely bound inside the selected animation owner's subtree. It
uses the native two-bone solver; translations/scales, other channels and unrelated
keys remain intact. Existing modes stay unchanged. Replacing a cubic key retains
its derivatives; newly inserted cubic keys have zero derivatives. Rotation signs
follow the equal-time key or preceding key's hemisphere (the first key when inserting
before it). Missing rotation tracks become linear, initially with one key and the
ordinary native endpoint hold across the timeline; add boundary keys when motion
rather than a constant new channel is wanted. Nothing silently smooths curves.

The caller supplies the clip associated with that posed sample. Preparation has no
file/device/history effects and owns its resulting clip and proof. Before publishing,
`validateTwoBoneKeys` checks a real candidate animation sample: local position/scale
stay exact, local rotation matrix entries differ by at most 1e-5, and the end world
position matches at 1e-5 * max(1, summed world segment lengths). It returns the actual
candidate end position. This guards composition from active layers or other sample
changes instead of assuming that writing a base key reproduces a composed pose.
Creator-thread and scope rules follow the scene. Initial and retained sample CPU
skin checks remain part of the editor's ordinary preview preflight.

The native editor requires an explicitly paused, healthy clip audition, outside
bind-pose display and base transitions. It freezes resource revision, preview session
and generation. Three equal-time keys form one ordinary resource history change,
then the new preview replaces the old one. A failed solve, resample, binding/skin or
history preparation retains the prior draft, preview clocks/generation and selection.
Exact semantic no-ops keep revision/redo/preview identity, while selecting the three
keys. Save remains explicit. The retained playhead is sampled normally; the key uses
its float32 time and preflight must still reproduce that pose at the retained time.
Reports use actual preview reach/residual, with key time and sample time separately.
This authors a sampled pose, not a persistent world target between keys or at runtime.

当前暂停姿态可直接经原生双骨骼求解写入三个旋转键，共享片段历史及实际预览预检。
三关节须在同一播放器子树内并具有唯一绑定；保留其余键、通道与插值。三次键替换保留
切线，新键切线为零，不自动平滑；缺失旋转轨以单键线性轨加入并遵守端点保持。发布前
检查实际动画合成能复现所求局部姿态及世界目标，失败完整保留原草稿和预览。返回实际
预览的位置/残差，不将求解结果当成已展示结果；不提供键间或运行时持续接触约束。
