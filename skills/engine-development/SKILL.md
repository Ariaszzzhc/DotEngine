---
name: engine-development
description: Implement or extend DotEngine subsystems from tested foundations to runtime and editor features.
---

Read repository `AGENTS.md` and the current `docs/Development.md` milestone before
choosing the next change. Build integrated capabilities toward the user-facing
goal. Retain enduring behavioral regression tests, not phase-specific demonstration
drivers. Use the actual editor and projects under examples/ for visual workflows.

Keep module dependencies directed from application code toward engine subsystems
and core utilities. Vendor headers belong in a module's global fragment. Use the
project's CMake targets instead of duplicating compiler or linker settings. Separate
non-template definitions into `.cpp` implementation units so internal changes do
not invalidate interface consumers; templates remain visible in module interfaces.
Separate function/type definitions and control-flow blocks with blank lines;
keep the inside edges of blocks unpadded and keep `else`/`catch` attached.

For stateful services, define initialization, shutdown, failure recovery, and
ownership before adding clients. Test malformed inputs and partial initialization.
For editor changes, expose every operation through the product's machine-readable
interface and share its implementation with the graphical interface. Test underlying
document/resource contracts without desktop windows. Prefer actual CLI/JSON agent
operations for integrated validation; supplement with computer use for human-style
interaction and unexpected visual/UI failures.
Keep temporary investigation scripts in ignored build storage; remove obsolete
probes instead of promoting them into permanent test targets.

Write an honest milestone entry with verification results before reviewing and
committing a cohesive change. Report environment failures with exact diagnostics.

