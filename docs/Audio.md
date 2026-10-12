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

Run `DotRuntime --audio-clip-test <existing-directory>` for atomic WAV write/load,
known PCM values, shared ownership/move semantics, and failure-path acceptance.
Tests generate their own WAV, constant-zero FLAC, and four silent MP3 frames;
no external media or encoder installation is required. The FLAC fixture follows
[RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html) sections 8.2 and 9.

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

Run `DotRuntime --audio-mix-test` for summed PCM, pause/seek/master gain, stale IDs,
and complete backend allocation cleanup. This is an offline acceptance case;
it does not establish playback-device operation or audible output.


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

`DotRuntime --audio-output-test` opens the real default endpoint, consumes actual
mixed PCM, observes underruns, stops, and checks native allocation cleanup. It
uses endpoint software volume zero so automated runs stay quiet. This proves
native backend/callback delivery, rather than physical speaker audibility. Native
audio CTest cases use `audio-native` and run serially; hardware/context failures
are failures. Tested locally on arm64 macOS with real CoreAudio in Debug/Release.
Windows/Linux implementations remain unverified. Hot-plug/interruption handling
is reviewed but has not been physically triggered by automated tests.

A continuous mixing worker, spatial voices/listeners, and scene audio components
remain to be implemented; the caller currently supplies queue data explicitly.
