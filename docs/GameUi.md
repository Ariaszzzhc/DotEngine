# Native game text

`dot:engine` exposes a scene-local `hud` service for game-authored text. The native
engine owns validation, font rasterization, wrapping, texture reuse and composition.
Scripts describe labels and gameplay feedback; they do not implement a renderer.
This first layer has no buttons, input interception, layout designer, images, rich
text, general shaping/bidi or automatic persistence.

```ts
import {hud} from 'dot:engine';

hud.set('checkpoint', hud.label({
  text: 'Checkpoint saved / 检查点已保存',
  position: {x: .02, y: .88},
  size: {x: .65, y: .1},
  fontSize: 20,
}));
const state = hud.snapshot();
hud.remove('checkpoint');
```

`hud.label(overrides)` creates fresh nested vectors. It supplies every field required
by the native protocol: text, position, size, fontSize, padding, color, background
and visible. Position/size are top-left normalized viewport UVs; size must be
positive and the rectangle must fit [0,1]. Font size is [8,64], padding [0,32], both
scaled from reference height 720 to the current physical viewport height. RGBA
components are [0,1]; RGB is display color and alpha is straight. Empty text may
still draw its background. Set visible=false to retain a hidden description.

Keys are 1..64 ASCII letters/digits/underscore/hyphen. At most 32 labels and 32768
aggregate text bytes, including hidden labels, are retained. Each label is bounded
to 4096 UTF-8 bytes and 1024 scalar values. Order is ascending key order, with later
layers composited over earlier ones. Successful changes increment a uint64 revision;
same-value set, missing remove and empty clear retain it. IDs/revisions cross JSON
as decimal strings. Returned snapshots own their values.

The service starts empty for each scene scope, survives realm hot reload, and is
retired with its scene/fault/close. Retained handles remain readable and reject
writes. New scenes reacquire their own service; labels are not shared through the
game-wide save store. Snapshot is read-only during constructors/migration; set,
remove and clear are denied there. Native operations require the creating thread.
Malformed/oversized input and failed preparation preserve the previous model.

## Fonts and layout

Editor and runtime use the existing pinned Sarasa UI SC font and its OFL notice.
The native kit carries EngineSans.ttf; it does not search system fonts or substitute
question-mark glyphs. Font bytes are owned, immutable and limited to 32 MiB. Each
CPU raster uses an independent FreeType face/library. Missing glyphs and invalid
fonts fail explicitly before a new text frame is published.

Layout is horizontal with kerning, space-delimited word wrapping and character
wrapping for words wider than the box. Newlines use LF. The supported scalar ranges
are U+0020..007E, 00A0..024F, 0370..052F, 2000..200A, 2010..2027, 2030..205E,
3000..3098, 309B..30FF, 3400..9FFF, AC00..D7A3, F900..FAFF and FF01..FF60.
Actual coverage also depends on the configured font. Unsupported combining,
joining, directional and other characters fail validation; this is not a complete
international text-shaping system. No Unicode normalization is performed.

Baselines and line height include actual hinted glyph ink bounds. Nominal face
ascenders alone can clip punctuation at small sizes even when the box has room.

Text clips to the padded box. Overflow is reported by key, including boxes that
map to zero physical pixels at very small viewport sizes; those boxes allocate no
texture. All glyphs are validated even when clipped. Fully clipped glyphs skip
bitmap work, and visible pixel loops are clipped before blending. Raster images
are at most 4,194,304 pixels; glyph/line metrics are bounded before integer layout.

`Graphics::TextRenderer` borrows a stable renderer and reuses unchanged text,
style and dimensions. Its cache and old/candidate replacement peak are limited to
16 MiB logical RGBA texture bytes. Hidden/removed labels release cached textures;
the immutable font remains until service destruction. Caller-retained ScreenDraw
copies may retain shared textures independently. Stats expose current cache bytes,
overflow keys and completed CPU rasterizations, including discarded candidates.
Font reading, rasterization and uploads are currently synchronous on the owner.

## Rendering and capture

ScreenDraw layers use renderer-owned Linear-storage textures whose bytes already
encode display RGB. A separate alpha-blended pass runs after HDR tone mapping,
without scene depth or exposure. Hardware sRGB targets receive the corresponding
linear conversion. This same path serves offscreen readback, persistent targets,
editor Play and the standalone surface. Labels are actual target pixels, not an
editor-only ImGui overlay. Stopped authoring/animation preview have no running HUD.

Editor view status exposes hudText cache/raster/overflow statistics. Capture frame
provenance includes hudRevision; a queued capture cannot accept pixels from another
UI revision. Native player inspection exposes hudText statistics and the last
presentedScope/presentedRevision, updated only after actual presentation succeeds.
A model update acknowledgement is not proof that a frame has rendered: inspect
readiness/errors and completed frame provenance. Missing fonts/glyphs or preparation
failure do not silently omit text or mark an old target current.

Existing mesh GPU timestamps exclude this display pass. Mesh CPU encoding includes
screen-pass encoding/submission, while frontend font/cache preparation is outside
that mesh interval. The application Render phase includes the frontend work too.
No combined GPU frame-time or populated-game performance claim follows from these
different measurements.

## Product operations

The SDK and product protocols share native Hud::perform. Inner operations are exact:

| Operation | Required fields besides command | Result |
| --- | --- | --- |
| snapshot | none | scope/revision/labels |
| set | key/value (complete label) | revision |
| remove | key | revision |
| clear | none | revision |

Editor Play:

```json
{"command":"hud","session":"1","scope":"83","value":{"command":"snapshot"}}
```

Standalone opt-in player control:

```json
{"command":"hud","scope":"83","value":{"command":"snapshot"}}
```

Use current IDs from inspect-play or player inspection. Wrong sessions/scopes fail;
closing/non-running state permits only an available snapshot. Mutations alter live
UI, not saved script properties or the authoring scene. Persist a UI design in normal
game scripts and their authored properties; visual layout authoring remains work.

Prism Relay uses this layer for controls and checkpoint success/failure messages.
Its uiFontSize script property controls both labels. Full checkpoint diagnostics
remain in relayProgressStatus; the in-game failure line gives a bounded user-facing
message. Feature Lab exercises the real API and displays an ordinary bilingual label.
