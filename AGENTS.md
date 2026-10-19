# DotEngine development

- Build from the foundation upward. Keep focused behavioral regression tests and
  validate integrated features through actual editor/example workflows. Do not
  retain temporary visual probes or add a bespoke acceptance executable per stage.
- Work inside this repository. Do not inspect other local projects.
- Keep the engine a static C++26 library with C++ module interfaces under
  `src/Engine`. Runtime and editor executables consume modules, without an installed
  public header tree.
- Separate module interfaces (`.cppm`) from implementation units (`.cpp`). Keep
  non-template function bodies and platform/vendor implementation headers in the
  implementation units; retain templates and required constant-evaluation bodies
  in interfaces. Prefer importing specific modules in engine implementation units.
- Match `.clang-format`: two spaces, attached braces, PascalCase types, camelCase
  functions, `m_` members, and trailing return types. Use explicit ownership and RAII.
- Leave a blank line between function/type definitions and before/after control
  flow blocks (`if`, loops, `switch`, `try`) when adjacent to other statements.
  Keep `else`/`catch` attached and do not pad the inside edges of blocks.
- Document contracts and non-obvious decisions with bilingual English/Chinese
  JavaDoc-style Doxygen comments. Do not add comments that repeat the code.
- Treat recoverable external failures as `std::expected` results with context.
  Validate input at subsystem boundaries; never silently substitute fake success.
- Keep current dependencies in the pinned vcpkg manifest until the separately
  reviewed vendor migration. The project toolchain selects LLVM Clang and lld;
  Windows uses the MSVC STL, while macOS/Linux use LLVM libc++ and libunwind.
  Resolve the LLVM installation without hardcoding local paths.
- Target arm64 macOS and x86_64 Windows/Linux only. Validate locally on macOS;
  do not claim Windows/Linux validation without evidence.
- Add behavioral tests for normal paths, failures, resource lifetime, and boundary
  conditions. Use CTest; tests must also run with assertions disabled.
- Unit tests must not initialize windows, rendering devices or audio devices.
  Automated integration tests must not open desktop windows. Keep durable
  offscreen rendering regressions separate from unit tests; validate editor
  workflows through actual product agent operations, with computer use for visual
  interaction when needed. Temporary investigation
  scripts belong in ignored build storage and are not committed.
- Keep `examples/FeatureLab` as the editable engine capability project. Games
  developed as independent products belong in their own repositories.
- The editor must remain fully controllable by AI agents. Every user operation
  needs an equivalent strict machine-readable operation using the same product
  implementation. Prefer CLI/JSON control for validation; computer use supplements
  this with human-style interaction and unexpected UI failure investigation.
- Before each commit, review the diff for correctness, ownership, error handling,
  platform assumptions, formatting, and appropriate test coverage. Run relevant
  checks and record actual results in the development log.
- Commit cohesive stages with Conventional Commits subjects under 50 characters,
  no body. Push reviewed commits to the configured repository without force.
- Use `skills/engine-development/SKILL.md` for subsystem implementation and
  `skills/engine-validation/SKILL.md` for verification and release preparation.
- Keep the development log honest: planned features are not implemented features.

