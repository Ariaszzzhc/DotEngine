# Native GUI rendering

The graphics layer provides persistent GPU scene targets and an isolated Dear
ImGui context with official SDL3/WebGPU backends. This is the rendering/input
foundation for the [native project editor](Editor.md) and [project home](ProjectHome.md).

The vcpkg manifest pins Dear ImGui 1.92.9b docking through the checked-in baseline
recipe and an overlay at port version 1. The overlay exposes only the required
docking, wchar32, SDL3, WebGPU and FreeType features and disables implicit Dawn
default features, preserving Metal/D3D12/Vulkan selection by platform. Vendor
headers remain private to implementation units and application frontend files.

## Persistent scene targets

`Renderer::createTarget(width,height)` allocates shared RGBA8 color and depth
storage. Dimensions are 1..4096 and within the device limit. Each renderer retains
at most 128 targets/256 MiB of logical target bytes. Color+depth counts as eight
bytes per pixel; driver allocations and queued GPU ownership are separate.
Copies share the same content and count once. Releasing the last copy releases
logical ownership; a target may survive renderer destruction for release, but
cannot be queried or drawn afterward. Target queries require the creator thread.

`SceneRenderer::renderInto(scene,target)` and `Renderer::renderMeshesInto` draw
directly into this storage, retaining color/depth allocations across frames.
No CPU image readback/upload is needed to show a viewport. New targets have no
completed frame; read/image use rejects them. Invalid drawing input retains the
last frame. A GPU error after drawing submission faults the target, requiring
replacement. Foreign targets and moved/failed owners are explicit errors.
`readTarget` is synchronous diagnostic/export readback with the existing bounded
GPU wait and row-padding removal; it returns owned top-to-bottom RGBA8 pixels.

## GUI context and frames

Enable GUI on a renderer with `enableGui(GuiConfig)`. GUI contexts are restricted
to the process main thread because ImGui's current context is global. SDL3 is used
when the renderer borrows a native window; an offscreen context supports explicitly
forwarded input and image capture without a window. An empty font path uses the
built-in font. Explicit fonts must be readable scalable fonts within 64 MiB,
validated with FreeType; missing/truncated/unloadable files return errors without
fallback. Font size is finite 8..64 logical pixels. wchar32 preserves Unicode input;
the selected font's glyph coverage determines rendered characters.

`presentGui(draw,seconds)` draws the callback's ImGui widgets and presents the
native surface with native logical/pixel DPI. `renderGui(draw,width,height,seconds,
scale)` captures a GUI frame, with scale in [0.25,8], dimensions 1..4096/device limits
and seconds finite [0,1]. Zero uses one microsecond for GUI time only. BGRA surface
readbacks are converted to RGBA. CPU callbacks may execute before a later surface
deferral/failure; widget/native side effects are not rolled back.

The draw callback returns Result<void> and runs with this renderer's ImGui context
selected. It must not replace the context or reenter renderer operations. Direct
ImGui widgets are frontend implementation details. `guiImage(target,width,height)`
is allowed inside the callback, uses logical dimensions in (0,32768] and retains
the target until submission. It rejects foreign, uninitialized or failed targets.
The GPU samples the target's view directly. Context switches restore the previous
context, including after errors, without writing an implicit ini/log file or
creating detached native viewports.

Callbacks returning failure, throwing standard/nonstandard exceptions or leaving
an unbalanced widget stack return contextual errors. Recovery restores widget
stacks so another frame can be attempted; recoverable ImGui diagnostics are reported
through the engine instead of asserting or silently publishing a repaired frame.
Draw geometry is bounded by configured vertex/index counts (defaults one/three
million, maximum two/six million). GUI stats report input capture, geometry and
successful submitted frames. `guiContexts()` counts live renderer-owned contexts
when operations are quiescent. `closeGui` is terminal/idempotent for that context,
works after device diagnostics fail and allows explicit reinitialization. Renderer
destruction releases GUI/font/backend resources before destroying its device.

Forward each normalized window event with `guiEvent` before drawing. Foreign
window IDs are ignored; keyboard, pointer, buttons, wheel, committed UTF-8 text
and focus are supported. Nonfinite coordinates/wheel and malformed/NUL/oversize
text fail (text <=64 KiB). Unknown keys/buttons and unrelated events return false.
Pending ImGui input is bounded to 131072 events including decoded characters;
exhaustion reports Unavailable before queuing another event and recovers after
frame processing. A paste may therefore exceed the remaining pending budget even
when it fits the per-event byte limit.
Focus loss clears tracked modifiers. ImGui preserves native macOS Cmd/Ctrl shortcut
semantics. Pointer units are native logical pixels. Native SDL polling, cursors,
clipboard and IME placement remain in the official platform backend. Core's key
vocabulary includes Delete/Insert/Home/End/PageUp/PageDown/KeypadEnter, with exact
SDL, GUI and script SDK mappings.

Local verification uses macOS/Metal. Windows/Linux implementations and dependency
features are supported in code but remain uncompiled/unexecuted locally.

The editor's pinned Sarasa UI SC Regular resource supplies upstream-composed Inter
Western and CJK glyphs; it does not depend on OS font installation. The GPU font case
loads the packaged file, deletes its staged source after GUI initialization, moves
the renderer, resolves actual Latin/Chinese/Japanese/Korean glyphs without fallback
and draws/readbacks mixed text before closing and checking context lifetime. An empty
font path still selects the renderer's explicitly documented built-in font; failed
explicit fonts never silently substitute it.

## Current-frame viewport images

`guiViewport(draw,logicalWidth,logicalHeight)` reserves a widget in the active GUI
layout. After layout completes, renderGui/presentGui synchronously call its
GuiViewportDraw with current physical dimensions, then encode GUI sampling from
that completed target. This permits resize/docking without a stale-size image while
retaining the rule that ordinary GUI construction callbacks cannot reenter rendering.
The viewport callback may render targets; recursive GUI frames or shutdown are
rejected. Captures are held only for that frame and are never replayed after failure.

Returned targets must belong to this renderer, have a completed frame and match the
requested physical dimensions (1..4096/device limits). Ownership is retained through
GUI encoding/submission. Explicit optional absence omits the image command, and
errors/throws prevent a fake GUI frame from being published. At most sixteen viewport
callbacks are accepted per frame; sizes must be positive and finite. Private stable
layout tokens are resolved or removed before the WGPU backend sees draw commands.
The immediate guiImage API remains useful for already rendered cached textures.

Offscreen cases verify actual DPI-rounded dimensions and red pixels on the first
frame and resize, owner/unrendered/wrong-size errors, absence, exceptions, non-replay,
GUI reentry/close protection, size/count bounds and target release. Validate workspace
integration through actual camera-free editing, missing-camera recovery, material
undo and Play/Stop using the editor's agent interface.

The editor's translation manipulator overlays actual projected axes on the Scene
image, with hit-testing from those same observed segments. Its draft scene is resolved
through guiViewport after layout. Capture/readiness is invalidated when the gesture
changes or ends; an old target never claims to be the current draft. Transform overlays
are editor chrome and are not serialized or included in game render targets.

## Deferred viewport overlays

GuiViewportDraw returns an optional GuiViewportFrame: a completed target plus a
bounded ViewportOverlay. Lines/points use top-left normalized UVs [0,1], display
RGBA [0,1] and logical-pixel thickness/radius [.5,16]. Each vector has at most
4096 elements. Empty overlays add no geometry. Invalid geometry and total GUI
vertex/index budget failures are explicit errors, without replaying callbacks.

Overlays are resolved after layout and viewport rendering. They use an owned
Dear ImGui draw list clipped to the actual image rectangle (including image borders,
window clipping and DPI), inserted after its source window at that window's stacking
layer. Other foreground windows still cover the overlay. They are x-ray editor
geometry, without scene depth testing. They never modify scene/HDR targets or their
readbacks. Lists and font references remain owned through submission and are retired
before their ImGui context. Large geometry uses the backend's vertex-offset path.

Dot.Graphics.Overlay supplies device-free homogeneous projection/clipping, including
near-plane and viewport-crossing segments. Dot.Animation.Skeleton samples actual
skin joint names, world positions and nearest bound ancestors using native skin
count/palette validation. Multiple roots and unbound intermediate hierarchy nodes
are preserved explicitly; no connections are inferred from palette order.
These are foundations for the editor's skeleton viewer, not an implemented bone editor.
