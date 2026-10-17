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
`clip` (selected library index), `speed` (-16..16), `looping`, `playOnStart` and
`enabled`. `Dot.Animation.Source` validates/transports this strict description without
IO/devices. Scenes allow at most 128 players, aggregate 10000 bindings/1024 clip
paths. Targets must exist when authoring is published. Removing a referenced target
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
player synchronization still copies bounded authoring descriptions and checks node
ancestors; populated-scene frame times/streaming are not established.

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
接入轨道/关键帧选择、值与切线草稿、候选姿态预检、保存/历史及同源智能体操作；图形切线/
状态图、视觉绑定和生产规模验证仍待完成。

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
Complete dope-sheet, tangent handles and state/layer authoring remain unfinished.

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
complete dope-sheet, bulk value authoring and layered/state-machine tools remain open.

KeyGroup 拥有不可变、有序的所选记录及原模式/时间范围，可包含部分三次键；源片段销毁/
修改不影响它，放置结果独立拥有，时间越界或 float 重叠时不改原组。PasteKeys 按时间归并，
默认拒绝占用，显式替换只覆盖同时间键；模式转换、切线缩放和绑定重定向均不隐式发生。
AnimationClipboard 在无窗口/文件/设备环境中校验来源与节点绑定、管理单调标识及拥有快照；
同场景/播放器/完整映射才可准备候选，实际姿态/蒙皮及一步历史由编辑器统一处理。
