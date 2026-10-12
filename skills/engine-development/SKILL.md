---
name: engine-development
description: Implement or extend DotEngine subsystems from tested foundations to runtime and editor features.
---

Read repository `AGENTS.md` and the current `docs/Development.md` milestone before
choosing the next change. Build the smallest complete subsystem that enables the
next milestone; preserve usable command-line acceptance cases at each stage.

Keep module dependencies directed from application code toward engine subsystems
and core utilities. Vendor headers belong in a module's global fragment. Use the
project's CMake targets instead of duplicating compiler or linker settings. Separate
non-template definitions into `.cpp` implementation units so internal changes do
not invalidate interface consumers; templates remain visible in module interfaces.

For stateful services, define initialization, shutdown, failure recovery, and
ownership before adding clients. Test malformed inputs and partial initialization.
For editor changes, provide machine-readable commands for each user operation and
verify them against the same implementation used by the graphical interface.

Write an honest milestone entry with verification results before reviewing and
committing a cohesive change. Report environment failures with exact diagnostics.

