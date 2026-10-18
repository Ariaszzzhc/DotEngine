# Native game UI

Scenes optionally bind an `assets/` resource with a `.dotui` suffix through their
`ui` field. No binding means `ui.snapshot()` returns null. The engine decodes and
owns the complete layout before publishing the scene or invoking scripts. Prepared
scene transitions carry immutable UI defaults across the worker boundary; the owner
creates the native instance without reopening the file. A new scene starts with its
resource defaults; source hot reload retains the current UI instance and action queue.

## Saved layout and visual editing

Open the editor's **UI** panel. Create a resource, open it, edit its node list and
properties, Apply, Save UI, close the resource and bind it to the scene. Save the
scene separately. The resource editor can also open an unbound layout. Its preview
uses the normal native text renderer over the scene viewport. Select a node in the
list or viewport; drag its body to move, or its lower-right handle to resize.
Rectangles stay inside their parents during graphical dragging. Escape cancels;
one completed gesture adds one undo entry. Typed out-of-bounds values fail Apply.

Panels, labels and buttons share a bounded key space. Only panels may have children;
panels contain no text. Every node has `key`, nullable `parent`, `kind`, `box`,
`enabled` and `blocksPointer`. The root resource has exactly `name` and `nodes`.
Box fields are `text`, `position`, `size`, `fontSize`, `padding`, `color`,
`background` and `visible`. Vectors use named x/y or x/y/z/w components. Required
fields cannot be omitted; duplicate/unknown fields, unknown kinds and malformed
UTF-8 fail. Resource JSON is bounded to 1 MiB and depth 12.

Keys are 1..64 ASCII letters/digits/underscore/hyphen; the name is 1..256 UTF-8
bytes without NUL. Layouts contain at most 32 nodes, eight hierarchy levels and
32768 total text bytes. Each text is at most 4096 UTF-8 bytes/1024 scalar values.
Rectangles use parent-local normalized coordinates, with positive size and complete
containment in [0,1]. Resolution produces viewport UVs and rejects unrepresentable
floating-point rectangles. Definition order controls root/sibling paint order;
parents paint before their children. Later layers cover earlier layers. Local
visibility and enabled state inherit through panels. Visible buttons always block
pointer hits; panels/labels explicitly choose blocking. Hidden nodes keep their keys.

Fonts and padding scale from physical viewport height 720; font size is [8,64] and
padding [0,32]. RGBA is [0,1], display RGB with straight alpha. Empty text may draw a
background. The packaged font supports the documented horizontal Latin/CJK ranges;
missing glyphs and text overflow remain explicit rendering diagnostics. The UI editor
validates the native model before publication; it does not guarantee that every font
or viewport size can display every candidate without overflow.

**Preview hidden nodes** changes only the isolated preview, allowing hidden menus to
be selected and arranged. It may expose overlapping alternatives normally hidden in
the game. It never changes the saved visibility. Typed forms may have incomplete
references while editing; Apply validates the complete graph. Save/history require
an applied or reset form. Native resource history retains exact original bytes for
undo, semantic no-ops retain revision/redo, and save detects external edits before
atomic replacement. History is bounded to 128 states/32 MiB. Discard does not save.

Text fields, images, rich text, general shaping/bidi, automatic flow/anchors,
scrolling and automatic persistence of UI instance state are not implemented.

## Gameplay intent and dynamic text

```ts
import {ui} from 'dot:engine';

ui.text('checkpoint', 'Checkpoint saved / 检查点已保存');
ui.visible('menu', true);
ui.enabled('save-button', true);
ui.focus('save-button');
const actions = ui.events(lastSequence);
```

`ui.snapshot()` owns asset/name/scope/revision and resolved nodes. Each node exposes
its local box/current local enabled value, resolved viewport box/effective enabled
value, kind, parent and pointer-blocking flag. `ui.text`, `visible` and `enabled`
update instance values. `ui.update([{key, text?, visible?, enabled?}, ...])` atomically
updates at most 32 distinct existing keys. UI requests/host arguments allow 128 KiB,
including JSON escaping of a full 32768-byte text batch; transient HUD requests
retain their 64 KiB bound. Invalid text, panel text, duplicate/unknown
keys or combined text capacity failures leave the whole instance untouched.
`ui.reset()` restores resource defaults, retaining the action sequence/history.
Same-value writes retain the native revision. These operations do not edit resources.

The scene-local `hud` service remains available for transient labels/buttons. Its
`label`/`button` factories return independent nested vectors. `set`, `setButton`,
`remove` and `clear` operate on transient keys; authored keys reject replacement or
removal, and clear preserves all authored nodes. Authored preorder draws first,
followed by transient lexical key order. Both share the 32-element/32768-byte budget,
renderer, input handling, focus and action queue. `hud.snapshot().order` reports the
complete paint order; label/button arrays retain hidden descriptions. Panels appear
as empty text layers in the label array. `ui.focus/events` share `hud.focus/events`.

## Native input, ownership and action lifetime

Hit testing walks the paint order backward. Visible pointer blockers stop lower
hits, including disabled buttons. Only visible enabled buttons receive focus or
actions. Tab/ShiftTab traverse eligible buttons in paint order; Enter/KeypadEnter/
Space confirms. Pointer press arms a button, release activates only if the same
eligible button is still under the pointer. Holding does not repeat. Outside release,
input loss and changing/removing/hiding/disabling an armed button cancel the gesture;
hiding/disabling a parent cancels affected descendant focus and gestures. Pointer
release and keyboard confirmation in one frame produce at most one action.

Input uses the actual logical viewport/window rectangle; rasterization uses physical
pixels. `hud.inputState()` reports the last native input publication's consumption
without removing raw input edges. UI actions queue intent, so gameplay handles them
in `onFrame` while simulation is paused. The queue retains 64 sequenced actions;
`events(after)` explicitly reports loss and rejects future/noncanonical cursors.
Scripts retain their processed sequence through hot reload to avoid repeated actions.
Programmatic `hud.activate` uses current-model eligibility and the same queue.

`buttonRevision` describes buttons and pointer blockers, so stale displayed input
cannot pass through a newly hidden blocker or activate a moved button. Passive labels,
interaction styling and actions do not advance it. General revisions also include
instance changes and interaction state. IDs/revisions are decimal strings across JSON.
Owner-thread operations return contextual results; retired handles remain readable
and reject writes. Snapshot/events/input-state/ui-snapshot are read-only during
constructors and migration; all mutations require active hooks.

## Strict product operations

Editor UI authoring uses the same native session as the graphical panel:

| Command | Fields besides command |
| --- | --- |
| create-ui | revision, path, name |
| set-ui | revision, asset (path or null) |
| open-ui | revision, path |
| inspect-ui, apply-ui, reset-ui-form, undo-ui, redo-ui, save-ui | buffer, revision |
| stage-ui | buffer, revision, value (complete typed resource form) |
| select-ui-node | buffer, revision, key (or null) |
| add-ui-node | buffer, revision, kind, parent (or null); selects a generated key |
| remove-ui-node | buffer, revision, key; stages deletion of the subtree |
| rename-ui-node | buffer, revision, key, name; updates child references |
| reparent-ui-node | buffer, revision, key, parent (or null); retains local rectangle |
| reorder-ui-node | buffer, revision, key, direction (earlier/later sibling) |
| set-ui-node-kind | buffer, revision, key, kind; parents with children remain panels |
| pick-ui-node | buffer, revision, generation, uv {x,y} |
| ui-preview-options | buffer, revision, showHidden |
| begin-ui-rect | buffer, revision, key |
| update-ui-rect | buffer, revision, gesture, value {position:{x,y},size:{x,y}} |
| finish-ui-rect | buffer, revision, gesture, commit |
| close-ui | buffer, revision, discard |

Scene and resource revisions are separate. Opening requires a current clean authoring
scene and no active Play/animation/transform operation. Resource snapshots expose
buffer/revision/generation, applied value, staged form, history, selection, gesture and
native preview. Viewport picking additionally requires a completed current frame.
Changing preview options, leaving/reopening or editing invalidates previous capture
provenance. Apply/history restart only the isolated UI preview. Save does not bind or
save the scene. Close requires a saved draft or explicit discard; gestures must first
finish/cancel. Saving a resource detects external disk changes.

Runtime UI operations use the normal `hud` envelope, requiring current scope and,
for editor Play, current session. Inner commands are exact:

| Command | Fields besides command |
| --- | --- |
| ui-snapshot, ui-reset | none |
| ui-text, ui-visible, ui-enabled | key, value |
| ui-update | changes (typed patch array) |
| snapshot, input-state, clear | none |
| set, set-button | key, value (complete native label/button) |
| remove, activate | key |
| focus | key (or null) |
| events | after |

```json
{"command":"hud","session":"1","scope":"83","value":{"command":"ui-visible","key":"menu","value":true}}
```

Native player control omits `session`. Available reads remain allowed after faults/
retirement; writes require a live running scope. Editor Play's temporary **Show game
UI** control hides presentation and disables native widget input until a current
visible frame returns. It preserves instance values; ordinary scene saves and exports
retain game UI. See [Play view control](Editor.md) and [game clock](GameClock.md).

Prism Relay stores controls, status alternatives, pause panel/title and three buttons
in its native layout. Scripts publish text/visibility and handle menu actions; they
contain no widget construction or menu coordinates. Feature Lab exercises every
`ui` API and its ordinary resource button records actual activations in uiClickCount.
