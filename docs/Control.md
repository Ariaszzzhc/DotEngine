# Local control requests and persistent replies

`Dot.Core.Control` supplies the shared local mailbox used by project home, the
editor workspace and opt-in native players. Commands still enter each product's
ordinary implementation. It is a local caller-serialized filesystem interface,
not an authenticated remote service or isolation from hostile concurrent writers.

Native startup prints both absolute `control` and `receipts` paths. The live
endpoint owns request files and disappears on retirement. Replies live separately
under the control parent's reserved `.dot-stage-control/<session>` cache. The cache
is excluded from game packages and resource catalogs; it is not a game save store.
A handler's final close/selection result remains readable after endpoint removal
and process exit. There is no synchronous compatibility mailbox or old-layout reader.

```text
DotEditor send <control> <unique-id> <request-file>
DotEditor control-status <receipts>
DotEditor ack-control <response-file>
DotEditor clear-control <receipts>
```

DotRuntime and exported DotGame expose the same three receipt commands and `send`.
They perform filesystem operations without opening a window or starting gameplay.
Project home optionally accepts `home [frames] --control <existing-parent>` so its
transport can live in an explicitly owned location. An omitted home parent uses the
ordinary process temporary directory; an explicitly empty/missing parent fails.

## Submission and observations

`send` atomically queues one owned UTF-8 JSON object of at most 1 MiB. Its
`queued:true`/`response` path acknowledges submission, not successful execution.
Read the returned file to observe the native result:

```json
{"source":"{\"command\":\"inspect\"}","response":{"ok":true}}
```

`source` is the original text, or null if unreadable/non-UTF-8. `response` is the
handler's strict JSON object, including ordinary `ok:false` errors. IDs are 1..64
ASCII letters/digits/underscore/hyphen; device names and case aliases reject.
Identical ID/bytes retry returns the same receipt path. A different payload cannot
reuse that ID. The endpoint retains at most 4096 identities, including consumed
ones, and at most 256 pending requests; each poll dispatches at most sixteen in
lexical order. Client code must not edit/delete mailbox files directly.

Callbacks execute at most once per live endpoint. The owner tracks dispatched IDs
independently of response-file presence: an unexpectedly lost processed receipt
faults the channel rather than permitting another mutation. A failed response write,
invalid handler result or exception after dispatch is terminal and may have committed
effects. Never retry that action under a new ID without inspecting actual product data.
Normal handler errors produce an ordinary retained failure and do not fault transport.

`control-status` reads receipt metadata and indexed sizes without needing the live
endpoint. It returns `state` (ready/closed), `control`, `blocked`, `count`, `bytes`,
`capacity`, `fault`, `uncertain` and `unprocessed`. `uncertain` lists IDs whose outcomes
cannot be recovered safely. On orderly retirement, `unprocessed` lists queued IDs
that never dispatched; null means damaged queue/index data prevented proving that
list. An active session's empty list is not a claim that its queue is empty.
Malformed provenance or reply trees are explicit status errors.

A ready metadata file is not proof that a process is still alive. Use its actual
process/job handle to establish liveness. Likewise, closed describes transport
retirement; it does not prove successful application/resource teardown or exit 0.
A close acknowledgement can precede worker/device cleanup. Verify actual process
completion separately when shutdown is part of the workflow.

## Consumption, capacity and cleanup

Read/copy a response before acknowledging it. `ack-control` atomically replaces the
body with exactly `{"consumed":true}`. Repeating acknowledgement is a no-op. Later
reads and identical live-endpoint retries explicitly report consumption; they do not
recreate the original result or execute the handler again. Acknowledgement frees payload bytes, preserving
the identity tombstone and request ledger. It does not recover the 4096 identity
limit. Use bounded observations and document edit batches for substantial workflows.

Default limits are 64 MiB per native handler result, 256 MiB of retained reply-file
bytes per session and 64 sessions per control parent. Native clients can configure
2 bytes..64 MiB response size, response size + 6 MiB + 4096 bytes..1 GiB reply budget,
and 1..64 sessions. Reply accounting includes serialized source/result/tombstone
files, excluding the small fixed-format receipt metadata and the live request ledger.
The existing 4096 requests/1 MiB-per-request limits still bound that separate ledger.

Before invoking a handler, the owner reserves its maximum result plus escaped source
and record overhead. Captured invalid source can expand sixfold during JSON escaping.
Insufficient capacity sets `blocked:true` and pauses dispatch without effects, result
eviction or replay. Consuming an already-read response frees space; later native
polls observe it and resume pending work. The application keeps running. A queued
request's observation timeout does not authorize restarting it. Actual filesystem
failures remain contextual errors; there is no disk-space fallback or fake result.

`clear-control` removes only a validated closed native receipt session after its
live endpoint is gone. It rejects active/forged paths, unknown entries, malformed
records, aliases, symlinks and special files. The last cleared session removes its
empty reserved cache parent. Cleanup deletes validated bodies before metadata; a
partially cleared or already absent owned session can be cleared again. It never
scans for/deletes other sessions, save slots, project resources or ordinary caller
directories. Copy any evidence first. New
session creation rejects a full session cache; explicitly clear owned closed
sessions rather than silently evicting unread results.

Explicit close reports metadata/cleanup errors and retains live-directory ownership
for retry. Destruction attempts retirement; unpublished/unadvertised unused owners
clean empty archives, avoiding hidden receipts when later subsystem initialization
fails. Once an endpoint/archive address is exposed or a request dispatches, replies
remain until explicit cleanup. Receipt replacement/acknowledgement stages in an
owned sibling directory before atomically renaming one complete leaf into responses. Concurrent product polls never
scan the writer's temporary files; the client still serializes its own calls.
Archive publication uses a no-replace directory rename; crash durability and
automatic recovery of abandoned active sessions are not claimed.

## Product lifecycle

Home uses this same control service. Selection stops its loop, retains the create/
open result, and retires the old endpoint before entering the selected workspace.
The next startup line advertises the workspace's separate endpoint/archive. Read
old home results from its receipts path, and use the new workspace channel for
scene/resource/Play/export actions. A response failure after project creation remains
uncertain; inspect the actual destination instead of replaying creation.

Keep focused file/CPU regressions in the existing integration targets; they open no
windows or devices. Actual editor/Prism workflows verify delayed reads after native
shutdown, source/scene authoring, Play, export and relocated player use of this same
transport. Such evidence does not establish overall production-engine maturity or
Windows/Linux execution.

## Native initial preparation / 独立游戏初始准备

Native project and exported-game frontends now create an actual pending GameSession
before entering their frame loop and advertising the control endpoint. Its first
poll owns CPU resource preparation, source compilation when applicable, and deferred
native audio opening. status/close remain usable while these tasks are pending.
There is no second startup mailbox or fabricated running session.

Before initial scene services exist, player.state is preparing, scope is "0",
clock/scripts are null and preparation names resources, scripts or audio. The
preparationError field retains the initial failure cause; preparationCompileAttempts
and discardedScriptCompilations report actual initial source jobs independently
of live reload statistics and survive scene replacement. inspect, game-data and
runtime scoped operations reject before their real services exist. A ready scene
can be inspected while graphics readiness/activation still delays Running. Native
metadata includes its actual preparation label for resource/shader/activation work;
null means no initial work remains. State/labels are observations, not percentages
or frame-latency promises.

close requests the same engine cancellation used by native Escape. The frontend
continues polling teardown and presents loading frames until tasks/endpoints are
reaped, then exits; a successful reply only acknowledges the request. Keep waiting
for actual process completion before consuming the final close reply. A resource
failure is retained during teardown and ultimately exits with the real error; it
never falls back to synchronous loading or a silent audio endpoint. Process error,
logs and retired receipts remain authoritative if the process ends before another
status poll. Initial compilation/source failure never dispatches game Start.

Package auditing and its owned content snapshot, saved scene metadata/snapshot,
window/graphics/font setup, native service construction and later VM/GPU work still
have synchronous costs. OS window-close/destruction and synchronous close can join
outstanding native work. This changes initial resource/compilation/device waiting
inside the live loop; it does not complete every launch/teardown or streaming goal.

独立开发项目与导出游戏都以真实待准备会话进入主循环；任务期间可查询状态及请求关闭。
服务建立前范围为零、时钟/脚本为空，访问场景、游戏数据或运行控制明确失败。关闭等待
拥有任务及端点清理后才退出，受理回复不等于进程已退出；资源错误保留原因并真实失败。
包审计/内容快照、元数据、窗口/设备/字体建立、原生绑定及后续 VM/GPU 仍有同步开销。
