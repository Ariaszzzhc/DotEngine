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

`DotEditorAcceptance --gui-test <existing-parent>` creates an owned temporary
project and real native window. Tab/space events travel through SDL, Platform and
Application into a real ImGui button. Its handler edits the native Document;
the GPU preview changes actual pixels and uses one reused target. The test checks
surface presentation and context cleanup. Focused GPU cases additionally cover
actual GUI colors/DPI, target/readback equivalence, callback/stack error recovery,
text/button/modifier input, malformed fonts, budgets, thread/move/lifetime rules,
foreign context restoration and releasing a target inside its image callback.

Local verification uses macOS/Metal. Windows/Linux implementations and dependency
features are supported in code but remain uncompiled/unexecuted locally.
