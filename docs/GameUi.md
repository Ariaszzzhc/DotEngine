# Native game UI

`dot:engine` exposes a scene-local `hud` service for game-authored labels and buttons. The native
engine owns validation, font rasterization, wrapping, texture reuse and composition.
Scripts describe labels and gameplay feedback; they do not implement a renderer.
The same bounded native text path renders button backgrounds, focus, hover, pressed
and disabled states. Visual layout authoring, text fields, images, rich text,
general shaping/bidi and automatic persistence remain unfinished.

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

Keys are 1..64 ASCII letters/digits/underscore/hyphen. At most 32 total labels/buttons and 32768
aggregate text bytes, including hidden elements, are retained. Each label is bounded
to 4096 UTF-8 bytes and 1024 scalar values. Order is ascending key order, with later
layers composited over earlier ones. Successful changes increment a uint64 revision;
same-value set, missing remove and empty clear retain it. IDs/revisions cross JSON
as decimal strings. Returned snapshots own their values.

The service starts empty for each scene scope, survives realm hot reload, and is
retired with its scene/fault/close. Retained handles remain readable and reject
writes. New scenes reacquire their own service; labels are not shared through the
game-wide save store. Snapshot, events and input-state are read-only during constructors/migration;
mutations and activation are denied there. Native operations require the creating thread.
Malformed/oversized input and failed preparation preserve the previous model.

## Buttons, focus and actions

```ts
hud.setButton('resume', hud.button({
  text: 'Return to game', position: {x: .33, y: .38}, size: {x: .34, y: .075},
}));
hud.focus('resume');
// In onFrame, retain the last applied sequence through realm migration.
const actions = hud.events(lastSequence);
```

`hud.button` supplies independent nested values, enabled=true and a dark background.
`set` and `setButton` replace the same key; labels and buttons share ascending draw
order. Hit testing visits visible buttons in reverse order. Labels are passive;
a disabled top button blocks lower buttons. Focus selects only visible enabled
buttons. Tab/ShiftTab cycle eligible keys; Enter/KeypadEnter/Space activates focus.

Native left-button press arms and focuses a button; release activates only if the
same eligible button remains under the pointer. Holding does not repeat. Outside
press, outside release, focus/viewport loss, relative capture and changing/removing/
disabling/hiding the armed element cancel the gesture. Focus survives blur. Native
button colors derive from the base model without overwriting authored values.
Pointer and keyboard confirmation in the same frame produce at most one action.

Input uses logical viewport/window coordinates, normalized by the frontend's actual
logical area; rasterization uses physical pixels. No area means no inferred pointer.
Disabled unavailable areas can have zero size; enabled areas require positive finite
size. Dispatch happens once per published native frame, independently of fixed ticks.
After an input publication, a HUD-dispatch failure faults the session and retires
writes; that error explicitly reports that the input frame already committed.

`hud.activate(key)` and strict product activate use the same eligibility/queue path.
An acknowledgement means queued intent, not completion of gameplay or storage work.
Inspect game state/save results after frame callbacks apply it. Product request IDs
provide mailbox replay semantics; a fresh activate request intentionally adds another
intent. Programmatic activation does not require or claim native window focus.

The native owner retains the last 64 actions. `hud.events(after)` is observational,
non-consuming, and reports `lost` if the cursor predates retained history. Each entry
contains decimal-string sequence and key. Future/noncanonical cursors fail. Clear/
remove do not erase history; retirement preserves reads; a new scene starts at zero.
Consumers own their watermark, reload migration, loss policy and bounded per-frame
work. Prism reports loss and skips expired intent, processes at most eight actions
per frame and ignores menu actions after return has resumed the game.

`hud.inputState()` observes which pointer/keyboard input the HUD handled in the last
publication. It does not remove raw input edges. Scripts must use it to arbitrate
shared gameplay keys; Prism prevents Space activating Return from also jumping.
Consumption survives model edits until the next publication. With
`input.requestTextInput(true)`, game text entry owns navigation/confirmation keys;
relative mode still takes precedence over OS text mode. This is an input contract,
not a native text-field widget or a general event-bubbling system.

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
| snapshot | none | scope/revision/buttonRevision/labels/buttons/focused/hovered/pressed/sequence |
| set | key/value (complete label) | revision |
| remove | key | revision |
| clear | none | revision/sequence |
| set-button | key/value (complete button: label fields plus enabled boolean) | revision/sequence |
| focus | key (eligible button key or null) | revision/sequence |
| activate | key (visible enabled button) | revision/sequence; action queued |
| events | after (canonical decimal string, including "0") | sequence/lost/events |
| input-state | none | pointer/keyboard consumption booleans |

Editor Play:

```json
{"command":"hud","session":"1","scope":"83","value":{"command":"snapshot"}}
```

Standalone opt-in player control:

```json
{"command":"hud","scope":"83","value":{"command":"snapshot"}}
```

Use current IDs from inspect-play or player inspection. Wrong sessions/scopes fail;
closing/non-running state permits available snapshot/events/input-state reads. Mutations alter live
UI, not saved script properties or the authoring scene. Persist a UI design in normal
game scripts and their authored properties; visual layout authoring remains work.

Prism Relay uses this layer for controls, checkpoint success/failure messages and
a clickable and keyboard-operated pause menu. Its native [game clock](GameClock.md) freezes simulation
while frame/UI callbacks continue.
Its uiFontSize script property controls labels and buttons. Full checkpoint diagnostics
remain in relayProgressStatus; the in-game failure line gives a bounded user-facing
message. Feature Lab exercises the real API, displays an ordinary bilingual label and button,
and records actual demo activations in hudButtonCount.

Snapshots also expose buttonRevision, a decimal version of complete button
definitions. Only changing the button set or a button definition advances it;
labels, focus/hover/press and action history do not. Editor Play and native surface
input require the displayed version in the current source/size, preventing an old
image from activating a newly positioned button while preserving held gestures.
Programmatic activate retains the existing current-model eligibility contract.
