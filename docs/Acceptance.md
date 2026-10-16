# Example and product workflow validation

Feature Lab is an ordinary editable project at
[examples/FeatureLab](../examples/FeatureLab/README.md), alongside
[Prism Relay](../examples/PrismRelay/README.md). Build-generated copies live in
build/<preset>/examples/. Project home uses the same example trees as templates.
These projects demonstrate real engine behavior; they are not dedicated test
executables or evidence that the large-game/editor goal is complete.

Feature Lab includes camera projections, hierarchy, material/mesh resources,
physics shapes/motions/contacts, audio, scripts and native animation playback.
Its coverage inventory and scripts exercise the exposed SDK; focused headless
regression tests independently check relevant contracts and failures. Inventory
membership is not proof of quality, performance or all possible inputs.

## Integrated product checks

The editor must be fully controllable by AI agents. Use the actual product CLI,
strict JSON operations and live mailbox for these workflows:

1. Open Feature Lab from examples/, inspect its real scene/resources and diagnostics.
2. Create a writable project from a template, author entities/components/materials
   and scripts, and verify selection/history/save behavior.
3. Check scripts, start isolated Play, drive gameplay through bounded agent input,
   inspect runtime diagnostics and stop. Verify automatic input release, stale-session
   rejection, unchanged authoring and externally saved source edits reaching hot reload.
   Relative camera checks must observe actual native capture, rather than assuming it.
4. Reopen saved data, export a native game, relocate it and check actual playback
   independently of the authoring tree and compiler.

Use computer use to supplement these checks with human pointer/keyboard interaction,
including project folder selection, hierarchy selection, panel layout and unexpected
focus/dialog failures. Record concrete findings and actual validation scope. Do not
commit stage-specific drivers or temporary screenshot/geometry-generation scripts.

Automatic CTest runs do not launch the editor or native game windows. They cover
unit behavior, file/compiler/physics/document integration, headless package execution
and durable offscreen rendering regressions; see [testing](Testing.md). Manual
product checks and automated checks provide different evidence. The chronological
[development log](Development.md) retains historical stage results, including
retired drivers; those commands are not current tooling.
