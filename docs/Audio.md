# Audio resources

`Dot.Audio.Clip` owns shared immutable interleaved float PCM. Creation copies the
caller's samples. Copies share storage; moves invalidate the source, and querying
a moved source returns `InvalidState`. A borrowed sample span remains valid while
any owning copy survives. Creation, decoding, and read-only queries can run on
independent threads; concurrent assignment of the same Clip object still needs
caller synchronization.

Formats use 1..8 channels and 8000..192000 Hz. Samples must be finite and within
[-1,1]; out-of-range float/lossy decoder output is rejected instead of clipped.
The decoder retains the original channel count and sample rate. Its unsigned
8-bit conversion maps 0..255 to -1..1, so 128 becomes approximately 1/255.
Empty clips, incomplete declared frames, and output exceeding the PCM budget fail
explicitly.
Clips retain whole decoded files; streaming file decoding is not implemented.

`Clip::decode` accepts RIFF WAV, FLAC, and MP3 signatures through the pinned
miniaudio backend. WAV validates RIFF length, chunk bounds/padding, unique format
and data chunks, and PCM/float/extensible frame alignment before backend startup.
Compressed decoding requires a nonzero backend frame length and a complete read;
FLAC frame corruption is rejected by the backend. These checks do not promise
rejection of every damaged MP3 bitstream that a tolerant decoder can recover.

`ClipLimits` defaults to 64 MiB encoded input and 256 MiB decoded samples. Each
limit must be 1 byte..1 GiB; exact limits are inclusive. The sample budget covers
retained float PCM, rather than backend scratch allocations. `fromPcm` applies
the same sample budget. `loadClip` uses root-relative FileSystem access and
case-insensitive `.wav`, `.flac`, or `.mp3` extensions matching the container.
Existing clips retain their samples when the source file is replaced.

The backend is a private C11 static library. macOS links CoreAudio/AudioToolbox
and CoreFoundation directly; Linux uses its platform backend and dynamic loader,
and Windows uses its native backend. Only macOS has been validated.

# Offline mixing

`Dot.Audio.Mixer` owns an independent miniaudio graph. It currently opens no audio
device; `render` writes actual interleaved float PCM into the caller's buffer.
Creation and instance operations run on the creating thread. Moving keeps that
thread and the mixer identity. Destruction may occur on another thread after all
use has stopped. Independent mixers can render on different threads.

Output supports mono/stereo at 8000..192000 Hz, with 1..1024 retained voices. A
voice holds its Clip copy, is initially paused, and has a scoped, non-reused ID.
Finished voices remain retained until explicitly destroyed; hitting the voice
capacity is an error. Playing resumes a paused voice and restarts an ended voice.
Queries report owned format/settings values and playing/end flags.

Voice volume and master volume are [0,1], pan is [-1,1], and pitch is [0.25,4].
Stereo pan moves and blends channels, so a full left pan combines the right
signal into the left signal. Mono sources duplicate into stereo. Source rates
and pitches use the backend resampler. Identical rates at pitch 1 bypass it;
pitch mode transitions and seeking reset filter history. Resampled output can
have the backend filter's startup latency. Seeking uses source-frame coordinates
in [0,clip.frames), clears stale prefetched data/end state, and does not start a
paused/ended voice. Loop changes preserve unconsumed source position and discard
old prefetched loop data. These operations require a quiescent graph; the private
C bridge deliberately manages exposed state in the pinned backend revision.

`render` accepts 1..65536 complete output frames. Empty graphs and naturally
finished sources produce normal silence; backend failures remain errors. Every
successful call advances the output frame clock by the requested count. Mixing
has no hidden limiter and may exceed [-1,1]. Invalid buffers/settings/handles are
rejected before changes; graph failures after rendering fault the mixer, retaining
queries and voice cleanup. Recreate a faulted mixer to recover.

The backend payload budget defaults to 16 MiB and must be 1 byte..1 GiB. A
reservation-aware allocator enforces it, including failed initialization and voice
preparation. Backend memory counters cover these payloads, excluding their small
allocation headers, Clip storage, C++ containers, and operating-system internals.
Process-wide counters are thread-safe informational samples, rather than one
atomic combined snapshot. Allocation failures preserve the usable existing graph.
The vendor resource manager is disabled: FileSystem/Clip remain the sole asset
loading path, avoiding hidden file access and unused loading workers.

# Native output

`Dot.Audio.Output` owns a native miniaudio context/device and a preallocated SPSC
PCM queue. Default selection is CoreAudio on macOS, WASAPI on Windows, and
PulseAudio/ALSA/JACK on Linux. Explicit selection can report an unavailable
backend. Neither path includes a null backend fallback. Device names and native
channel/rate metadata are captured as owned values during initialization.

The client format is mono/stereo at 8000..192000 Hz. Queue capacity is a power of
two in 64..262144 frames; requested periods are 64..8192 frames and cannot exceed
the queue capacity. Native channel/rate conversion is handled by the backend.
The payload budget is 1 byte..1 GiB, default 16 MiB; counters exclude queue/scratch
vectors, allocation headers, and operating-system internals. Mixer/output totals
are separate, using the same private reservation-aware allocator.

Controls, submission, mixing, and queries require the creating thread. A move
keeps that ownership. Submit copies 1..65536 finite, complete interleaved frames,
failing before copying if capacity is insufficient. Producer progress and consumer
progress publish complete frame batches with release/acquire ordering. `mix`
requires a same-owner Mixer with identical channels/rate; it validates available
capacity before advancing the graph. Native faults may still arrive after mixing.
The device callback consumes the queue without allocation, locks, or application
callbacks. It explicitly clips queued samples to [-1,1], records clipped samples,
and fills unavailable frames with silence while counting underruns. Endpoint
software volume is [0,1] and does not change system/global volume.

Preload while stopped, then start. Stop waits for callbacks and retains unconsumed
queue frames. Clear discards queued frames only while stopped; discarded/submitted/
consumed counters retain lifetime totals. Already-consumed samples buffered by
the driver are outside this queue. Statistics during playback are informational
atomic samples; after stop, callback totals remain stable and can be compared
consistently. Backend startup/stop failures and unexpected stop/reroute/interruption
fault the endpoint. Queries and stop remain available; recreate the Output to
recover. Uninitialization joins callbacks before freeing the queue/context, even
when the application hands quiescent destruction to another thread.

Scene audio authoring and explicit runtime binding are described below.

# Continuous playback

`Dot.Audio.Playback` creates a dedicated worker that owns Mixer and Output,
prefills PCM, starts a real endpoint, and keeps rendering while the client thread
is idle. Its Mixer/Output channel counts and sample rates must match. The target
buffer defaults to 2048 frames, with 256-frame mixing blocks and a maximum 2 ms
poll interval. Targets must fit the output queue and retain at least two requested
periods. Blocks are 1..65536 frames and cannot exceed the target; the maximum poll
interval is 1..20 ms. Waits adapt to a quarter of the currently queued duration,
with a 50 microsecond floor and the configured upper bound. OS scheduling can
still cause underruns; they remain observable in output statistics.

`PlaybackConfig.deferredStart` prepares the real endpoint and accepts voice controls
without filling PCM or starting device callbacks. Statistics report `Prepared`,
zero rendered/submitted frames and a stopped endpoint; a voice marked playing keeps
its cursor until activation. `start()` prefills the current graph and starts native
playback, then becomes idempotent. It does not pause/resume or reopen a closed/faulted
service. Closing or destroying a prepared service releases all resources without
playing it. A prepared worker waits at the configured poll interval.

`begin()` starts owned endpoint initialization and returns without waiting for it;
`pollReady()` checks actual completion. `beginStart()` queues activation once and
`pollStarted()` checks its result without advancing the mixer. Controls reject while
initialization/activation is pending. No null-device fallback is used. `requestClose()`
requests teardown without joining an OS call in progress; keep ownership until
`pollClosed()` succeeds, then `close()` reports any terminal error. Destruction and
synchronous close still join. Polls and controls require the original client thread.
The synchronous create/start convenience APIs wait for these same operations.

`SceneAudio::startPlayback` and the native `AudioSession::startPlayback` scheduler
operation forward activation (offline mode needs no activation). Reset retains
whether the service has been activated. Scripts can observe `prepared` through
`audio.stats().playback.state`, but cannot activate the device. GameSession always
prepares native audio deferred, runs Start and initial animation/audio pose
synchronization, then activates it. Loading a scene or constructing its scripts
therefore cannot play its authored autoplay voices. Recoverable component callback
failures retain their existing isolation/reporting behavior; realm or native startup
failures before activation fault the session and leave the endpoint prepared;
activation failures also propagate to the session. This defines
the audio preparation boundary used by scene startup and replacement. An explicitly
supplied ready deferred endpoint must match the full configuration and creating
thread before ownership can transfer into SceneAudio/GameSession. Async
GameSession beginStart keeps state Starting until pollStart confirms native
activation; gameplay/input advancement rejects during Starting. Editor Play uses
this boundary to keep native opening/activation waits off its owner thread.

Scene replacement requests old native teardown after Stop and retains only its
AudioSession in one retirement slot. The old realm/physics/animation are released
on the owner; the audio slot retains clips and its old scene until native completion
is polled. Update and pollScene reap it without advancing extra gameplay time.
A following scene request can queue, but preparation waits for retirement, keeping
at most two game-owned endpoints (including a prepared candidate). `retiringAudio`
in GameSession/Play/native player status means unreaped ownership, even when the
worker has already finished. Terminal teardown errors fault the current session
with retired-scene context. Synchronous close/destruction still join both active
and retired playback. `SceneAudio::closePlayback` and `AudioSession::closePlayback`
expose that same blocking native completion and its error; offline mode is a no-op.
Scheduler teardown remains valid after scene invalidation, on the creating thread.

Already buffered PCM may finish a tail while native stop completes, so a successful
replacement is not a sample-accurate crossfade or a guarantee of zero audio overlap.
Native endpoint preparation/activation, failed-candidate cleanup, non-audio service
retirement and final close are still synchronous owner work.

Creating client-thread calls synchronously dispatch validated voice operations
and return the worker's actual results. There is at most one pending command;
arguments retain owned Clip copies until execution. Startup waits for successful
backend initialization/startup; failures join and release partial resources.
Moves retain the original client thread and scoped voice identity. Other client
threads receive `InvalidState`; quiescent destruction may join from another
thread. The native callback remains a queue consumer and never runs these controls.

Refill batches are bounded and yield between blocks to pending controls. At least
one refill opportunity follows a command, so a burst of controls cannot repeatedly
skip all mixing. Already-mixed/driver-buffered PCM preserves its old content;
voice pause, seek, gain, and pitch affect subsequent blocks. Endpoint software
volume applies at the native output instead. Recoverable invalid controls, stale
handles, voice capacity, and voice-preparation allocation failures do not fault
the worker. Device/mixing failures retain an owned diagnostic, reject subsequent
controls, and require a new Playback for recovery.

`stats` obtains a fresh worker snapshot while running. `close` wakes and joins the
worker, stops/join device callbacks, destroys voices and backend owners, and
preserves cumulative counters/metadata. Queue data discarded by teardown is
accounted for; terminal snapshots clear live ownership after destruction. Healthy
closure is idempotent. Faulted closure still returns its error; terminal statistics
remain readable. SDK device operations and control calls may block on their
worker/backend completion and are not intended for a real-time callback.

# Spatial voices and listener

`Dot.Audio.Spatial` describes a single Listener and per-voice SpatialSettings.
Coordinates are right-handed metres, with forward -Z/up +Y matching the engine.
Finite nonzero quaternions are normalized before deriving forward/up vectors.
Position components are bounded to +/-1000000 metres and velocities to 100 m/s.
Velocity describes Doppler motion; it does not integrate positions automatically.

`Mixer::setListener` validates the entire pose before applying it. `setSpatial`
validates the full descriptor even when disabled. Enabled spatial voices require
mono clips and zero manual pan; stereo music/nonzero pan conflicts are explicit
errors. VoiceState retains an owned normalized spatial descriptor. Invalid inputs
leave existing state intact; unexpected backend update failures fault the mixer.
Queries after a fault retain the last successfully stored descriptions.

Distance attenuation supports inverse, linear, and exponential models. Minimum
and maximum distances are within 0.001..1000000 metres, with minimum strictly
less than maximum. Rolloff is 0..10; zero disables distance attenuation while
retaining directional channel gains. Gains are clamped to [0,1] by the backend.
Source cones use full inner/outer angles in 0..360 degrees, inner <= outer, and
outer gain [0,1]. Direction follows source rotation. Stereo spatialization applies
the pinned backend's directional channel gains and normal gain smoothing; it is
not an HRTF/headphone binaural renderer. Mono output retains distance/cone gain.

Doppler is opt-in through a factor in [0,1], default zero. The velocity limits keep
the speed-of-sound calculation nonsingular. The private quiescent pitch bridge
refreshes Doppler from listener/source motion and keeps resampling enabled when
needed; enabling/disabling Doppler must not lose the rate-1/pitch-1 optimization
or retain stale Doppler pitch. Coincident source/listener positions are supported.

Playback forwards spatial/listener setters and queries through the same owned
worker command path. Already-queued PCM keeps its previous content; controls
apply to future blocks. Listener/source changes allocate no native resources.

# Scene audio descriptions

`Dot.Audio.Voice` owns backend-independent VoiceSettings and their finite/range
validation. `SpatialProperties` separates attenuation/cone settings from the
position/rotation/velocity in SpatialSettings. `Dot.Audio.Source` combines those
properties with an asset path and `playOnStart` (default true). Scene stores it
as AudioSource; snapshots own their strings and changing a snapshot has no effect
on the scene. Source edits validate the entire replacement first, retain the old
component on failure, and remove idempotently with nullopt.

Asset paths are nonempty UTF-8 strings of at most 4096 bytes, relative to a project
root, using forward slashes and a case-insensitive .wav/.flac/.mp3 extension.
Empty, dot, and parent components, absolute/drive paths, backslashes, ASCII control
characters, `< > : " | ? *`, and components ending in dot/space are rejected.
This description check performs no filesystem access: missing/unusable files must
return a contextual failure when a playback service resolves them. Enabled spatial
descriptions require zero manual pan; mono clip validation belongs to binding.

AudioListener stores a primary flag, default false. There can be one primary
listener per scene independently of its camera. A second primary is rejected
before mutation. `primaryAudioListener` returns NotFound when none exists;
scope/thread/moved-scene errors retain their usual scene contracts. Component
destruction follows entity/subtree lifetime and retains no native audio resources.

Scene files preserve every source/listener property with strict nested fields and
types. Pose remains in the entity transform; decoding builds a fresh independent scene.

# Voice batches

`Mixer::applyChanges` accepts an owned VoiceBatch containing creations, optional
updates to existing voice properties/play state, removals, and an optional listener.
Each existing handle may appear once across update/removal. Returned handles follow
creation order. Omitted properties retain their current values; an update keeps
the voice identity and source cursor. Explicit playing=true resumes/restarts using
the same contract as play; false pauses. Creation starts at source frame zero.

All handles, clips, combined pan/spatial properties, listener values, and final
voice capacity are checked before native preparation. All new voices are prepared
before removing/updating old ones, so replacement works at full voice capacity.
Preparation needs transient backend budget in addition to the final live graph;
allocation/validation failures leave old voices, cursors, listener, and handles
intact. Temporary voices release their allocations and failed preparation consumes
no public voice IDs. Host container allocations are excluded from backend counters.

The commit transfers preallocated container nodes and performs no graph advancement.
Unexpected backend commit failures explicitly fault the mixer; partial commit state
is diagnostic and requires recreation. Those fault branches have been reviewed;
automated tests induce preparation allocation failures, not backend commit faults.
Playback copies batch arguments and executes one worker command without mixing
between individual edits. Queued PCM retains its previous content. Large batches
can occupy the control worker long enough to cause observable native underruns;
the bounded command path is not a real-time scheduling guarantee.

# Scene audio binding

`Dot.Audio.SceneAudio` owns a project-root FileSystem copy, a bounded immutable
Clip cache, and entity-to-voice bindings. `create` owns an offline Mixer;
`createPlayback` owns continuous real-device Playback. Both resolve and bind the
scene's audio sources. The service retains no scene reference; operations use its
creating thread, moves preserve ownership, and quiescent destruction can happen
elsewhere. Native teardown joins the worker before releasing the backend.

`synchronize(scene, seconds)` applies authored component edits and world poses.
Only the originally bound scene is accepted until reset. Owned snapshots/queries
reflect the last successful binding; removing an entity/component releases its
voice and evicts clips no longer referenced on the next successful synchronization.
An unchanged asset preserves its cursor and manual play state. Changing volume,
pitch, looping, pan, or spatial properties updates the existing voice; playOnStart
is used only for creation/asset replacement, and does not restart finished voices.
Play/pause/seek are entity-scoped controls independent of editing; master volume
affects the entire service.
Ended voices report stopped even when the backend retains an internal start flag.

Every authored path loads once while referenced. Shared references count once
against maxAssets (default 128, range 1..1024) and sampleBytes (default 256 MiB,
range 1 byte..1 GiB). Per-clip encoded/decoded limits also apply; decoding is capped
by the candidate cache's remaining PCM budget. Cache keys are authored paths;
separate path aliases are not physically deduplicated. Current caches/voice clips
remain alive while preparing replacement assets, so transient memory can exceed
the final cache budget. Same-path file changes apply on reset, not implicit polling.
UTF-8 paths use filesystem UTF-8 construction on all supported implementations.

Enabled spatial sources require mono PCM and a primary scene audio listener.
Flat sources need neither a listener nor a suitable spatial world transform.
Listener/source world TRS is decomposed and normalized; shear/reflection and
unsupported pose bounds fail explicitly. Positive world scale affects inherited
position but does not scale distance/cone settings. Source/listener directions
follow world rotation. Zero seconds means teleport with zero velocity; positive
finite seconds up to one derive velocity from consecutive successful positions.
New sources/listeners have zero initial velocity, including listener switches.
Derived speed over 100 m/s is rejected, and failed synchronization does not update
previous positions. Tiny positive timesteps use double division before float
conversion; stationary poses remain valid. Doppler still requires its opt-in factor.

All desired files, budgets, poses, and binding containers are prepared before one
voice batch commits. Missing/corrupt files include entity/path context and leave
old playback/cache/bindings intact. Mono/listener/pose/capacity/allocation failures
also retain the last successful state. Unexpected backend commit faults retain
explicit fault diagnostics and require reset/recreation; they are not claimed to
roll back a partial backend commit. Preparing files does not block the native
mixing worker; batch graph preparation still has the scheduling limits above.

`reset(scene)` independently recreates backend/cache, rereads every file, and may
bind another scene. Failure keeps the existing service; success resets playback
from playOnStart, sets master gain to one, and restores endpoint gain from configuration. Native reset can briefly have two
initialized endpoints, with old buffered audio overlapping new playback; it is
not a seamless transport transition. Offline render is rejected in native mode
without writing the caller's buffer. Statistics expose cache ownership, mixer
state and, in native mode, actual device/frame/fault totals. Queries and controls
can remain useful after the source scene is destroyed because binding data is owned.

## Stop and source cursor

Mixer, Playback and SceneAudio expose stop(voice/entity), pausing and rewinding to
source frame zero without advancing the graph. Native Playback executes both edits
in a single worker command; queued/driver PCM remains unchanged. Backend failure
during stop faults the mixer explicitly. Successful stop clears ended state, retains
the voice/clip, and allows a later play from the beginning.

VoiceState.sourceFrame queries miniaudio's source read cursor as uint64. It can
include backend prefetch/resampling and is not a device presentation timestamp.
Paused voices retain that cursor; seek and stop update it without starting playback.
The typed script bridge preserves frame values as decimal strings; see Scripting.md
for AudioSession ownership, scheduling, state/control methods and executable cases.


## Prepared scene PCM / 场景 PCM 准备

`loadSceneClips(files, scene, limits, stop)` decodes unique logical source paths on
the calling CPU thread without opening a mixer/output device. It enforces the exact
encoded/per-clip/aggregate PCM and asset-count bounds and spatial mono requirement.
Cancellation is checked between bounded files; individual bounded decodes remain
synchronous on that worker. Final listener/world-pose and voice capacity checks
remain part of native binding, since content can inherit the active world's listener.

SceneAudio and AudioSession factories accept an optional complete initial decoded
set. `synchronizePrepared` supplies one update's uncached referenced clips with no
file fallback; invalid/duplicate/unused paths fail before backend mutation. Supplied
PCM has per-clip/aggregate checks; encoded-byte bounds are a decoder responsibility.
Existing referenced paths retain their immutable clip data and voice cursor/manual
state even if a prepared set supplies different samples for that same path. The
combined active resources still obey the configured budgets. Removing the final
reference releases that cache entry; later ordinary binding rereads its file. Reset
continues to reread all resources. Queued PCM retains its existing content.

PreparedScene retains immutable PCM and the exact decoding limits. Its audio getter
rejects consumers with different limits, preserving encoded-source budget semantics.
GameSession's append/replacement worker and initial asynchronous editor Play use
that session's configured limits, and first native binding adopts their proof. The second synchronization after Start uses
ordinary refresh rules for script-authored changes. Synchronous initial session
creation without a proof, later dynamic file reads, backend batches/activation and
retirement still have their existing costs. This is bounded resource preparation,
not general streaming or a guarantee about endpoint presentation latency.

场景准备工作线程按精确预算解码唯一 PCM，不创建混音器或设备；最终监听器/位姿/声部
容量由原生绑定检查。首次绑定消费完整准备结果，不回退磁盘；缓存引用期间保留数据及
手动播放状态，最后引用移除后释放缓存。准备结果拒绝不同预算的使用者。初始同步创建、
后续动态读文件、后端批次及设备生命周期仍有原有开销，不代表完整流送或播放延迟保证。
