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
This stage retains the whole decoded clip; streaming playback is not implemented.

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
and Windows uses its native backend. Only macOS has been validated. This stage
has no playback device, mixer, spatial voices, or scene audio components yet.
