# DotEngine development

- Build from the foundation upward. Each subsystem must have a runnable acceptance
  case and focused tests before consumers depend on it.
- Work inside this repository. Do not inspect other local projects.
- Keep the engine a static C++26 library with C++ module interfaces under
  `src/Engine`. Runtime and editor executables consume modules, without an installed
  public header tree.
- Match `.clang-format`: two spaces, attached braces, PascalCase types, camelCase
  functions, `m_` members, and trailing return types. Use explicit ownership and RAII.
- Document contracts and non-obvious decisions with bilingual English/Chinese
  JavaDoc-style Doxygen comments. Do not add comments that repeat the code.
- Treat recoverable external failures as `std::expected` results with context.
  Validate input at subsystem boundaries; never silently substitute fake success.
- Keep dependencies in the pinned vcpkg manifest. Preserve the caller's compiler
  and linker environment; do not hardcode local LLVM paths.
- Support Windows, macOS, and Linux in implementation. Validate locally on macOS;
  do not claim Windows/Linux validation without evidence.
- Add behavioral tests for normal paths, failures, resource lifetime, and boundary
  conditions. Use CTest; tests must also run with assertions disabled.
- Before each commit, review the diff for correctness, ownership, error handling,
  platform assumptions, formatting, and appropriate test coverage. Run relevant
  checks and record actual results in the development log.
- Commit cohesive stages with Conventional Commits subjects under 50 characters,
  no body. Push reviewed commits to the configured repository without force.
- Use `skills/engine-development/SKILL.md` for subsystem implementation and
  `skills/engine-validation/SKILL.md` for verification and release preparation.
- Keep the development log honest: planned features are not implemented features.

