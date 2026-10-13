# Native process execution

`Dot.Core.Process::runProcess` executes an explicit absolute executable with literal
UTF-8 arguments and an absolute working directory. It invokes no shell and performs
no PATH search. It inherits the caller environment and does not change the parent's
cwd or signal disposition. Inputs require UTF-8 without NUL, path lengths at most
4096 bytes, at most 4096 arguments and 1 MiB argument payload. Empty arguments,
spaces, quotes, backslashes, line breaks, Unicode, and shell syntax remain data.
Environment must not be concurrently mutated outside the runner.

Stdin is EOF. Separate stdout/stderr capture raw bytes, including NUL/non-UTF-8,
under one combined output limit (default 1 MiB, range 1 byte..64 MiB). Streams are
drained fairly in bounded chunks so filling either pipe does not deadlock capture
or skip deadline checks. Normal completion requires the direct child to exit and
both streams to reach EOF. A normal nonzero exit returns its actual status/output;
compiler consumers must explicitly reject failed compilation status. Windows exit
codes retain their full unsigned 32 bits. Process launch, I/O, POSIX signal,
timeout, and output-limit failures return contextual errors instead of fake success;
partial output is not returned on failure.

Timeout defaults to 10 seconds and accepts 1 ms..10 minutes. It covers waiting
for child completion and stream EOF after launch. OS startup/filesystem/teardown
can block outside cooperative checks; this is not a hard real-time process sandbox.
Temporary capture strings may exceed the output payload by allocator capacity,
and argv/OS internals are outside this payload budget.

macOS/Linux use posix_spawn with child-only cwd actions, a new process group,
closed stdin, explicit stdio duplication, and nonblocking capture. macOS closes
unmentioned descriptors by spawn flags; current Linux uses glibc closefrom spawn
actions. Low-numbered pipe descriptors are moved above stdio before configuring
duplication. Child signal mask/defaults are set without editing parent state.
The direct child stays waitable while group teardown occurs, so its PID cannot be
reused as another group identity before cleanup. Group members are terminated and
the direct child is reaped on exit/failure; orphaned grandchildren are reaped by
the OS. Descendants must not detach into another group/session, and application
SIGCHLD handlers must not reap this runner's children. Ignored SIGCHLD/SA_NOCLDWAIT
is rejected before spawning. External ownership interference yields an explicit
error and avoids signaling an identifier whose lifetime is no longer controlled.

Windows uses an explicit CreateProcessW application, a writable UTF-16 command
line, and [CRT argument quoting](https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments?view=msvc-170).
An inherited-handle allowlist contains only stdin/stdout/stderr. The child starts
suspended, joins a kill-on-close Job Object, then resumes; startup failures terminate
it before releasing handles. Job cancellation covers descendants, and teardown
waits for active job processes when queryable. Concurrent pipe inspection avoids
blocking on an empty stream. Command lines exceeding the Win32 32767-character
limit fail explicitly. Only macOS has been compiled/executed locally; Windows and
current glibc Linux implementation paths remain unverified.

`processResources` exposes process-wide runner-owned handles/direct-child counters
for quiescent lifetime checks; it does not enumerate child-internal resources.
Calls can run concurrently with independent outputs/directories. Descriptor, spawn
attribute/action, process/thread/job, and inheritance-list ownership is RAII.

Run `DotRuntimeAcceptance --process-test <fixture-executable> <existing-directory>` using
`build/debug/bin/DotProcessFixture` (or Release) for actual status/stdin/stdout/
stderr capture, output beyond pipe capacity, timeout and cleanup. Focused tests also
cover literal/Unicode arguments and paths, binary output, combined/exact limits,
missing/invalid inputs, descendants retaining pipes, repeated/concurrent calls,
signals, descriptor inheritance, closed parent stdin, and unchanged inherited
environment. Process tests use their own target so changing a fixture path does not
recompile unrelated unit tests. TypeScript compiler integration remains pending.
