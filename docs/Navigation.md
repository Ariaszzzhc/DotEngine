# Static navigation / 静态导航

The editor bakes actual solid static collision geometry with pinned Recast/Detour.
Scripts request complete routes and submit normal native character movement intent.
Jolt remains authoritative for movement, support and dynamic contacts. A planned route
is not proof that a character has arrived or that a moving obstacle will clear.

编辑器使用固定版本的 Recast/Detour 烘焙实际实体静态碰撞几何。脚本请求完整路线，
通过正常原生角色意图行走；移动、支撑和动态接触以 Jolt 为准，规划成功不等于角色已到达。

## Authoring / 制作

Open **Environment → Navigation** in a stopped scene. Set the capsule profile and
resolution, then **Bake navigation**. This owns one cancellable CPU worker; it never
transfers the editor's live scene to another thread. The result shows actual counts
and an XZ top view of coarse polygons, combining all heights. Large previews draw
at most 4096 polygons and explicitly report that limit; queries use the whole surface.
Enter world-space start/goal positions and **Preview route**. Projection is bounded
and returned endpoints show where they actually snapped.

Choose a new `assets/*.dotnav` path and **Publish new navigation asset**. Publication
never overwrites an existing file or case alias. It rechecks the exact captured scene
revision/content, project metadata and all frozen static mesh source bytes. Even undo
back to the same content requires a fresh bake because the document revision changed.
Cancel an obsolete ready result before baking again. Project writers must be serialized;
these checks are an authoring conflict guard, not operating-system snapshot isolation.

**Bind saved navigation** is a separate scene edit with one normal undo entry; save
the scene afterwards. Detach/undo changes the binding and leaves the published asset.
Changing static geometry invalidates the baked resource; rebake to a new destination,
then rebind. Complete scene preparation rejects stale/missing/malformed navigation.
Scene-content extraction/instantiation excludes this global and preserves the target's
binding. Appending new static collision geometry invalidates existing navigation.

在停止的场景中打开 Environment 的 Navigation。烘焙任务拥有独立 CPU 线程，可取消；
不跨线程传递实时场景。预览使用实际多边形及查询结果，XZ 图合并各高度。发布前逐字复核
场景、项目描述和静态网格输入，拒绝覆盖或过期结果。发布资产与绑定场景是两个独立操作；
绑定支持正常撤销/重做，资产不会因场景撤销而删除。静态几何变化后须重新烘焙并绑定。

Each object below is one strict editor request; unknown fields/types are rejected.
Revision/session values are canonical decimal strings. Inspect the current revision.

```json
{"command":"begin-navigation","revision":"4","value":{"cellSize":0.1,"cellHeight":0.05,"agentHeight":1.83,"agentRadius":0.45,"maxClimb":0.3,"maxSlopeDegrees":50,"tileCells":128}}
{"command":"navigation-status"}
{"command":"query-navigation","session":"1","value":{"start":[-2,0,0],"goal":[2,0,0],"searchExtent":[0.5,0.5,0.5],"maxPoints":256}}
{"command":"publish-navigation","session":"1","revision":"4","path":"assets/walkable.dotnav"}
{"command":"set-navigation","revision":"4","asset":"assets/walkable.dotnav"}
{"command":"set-navigation","revision":"5","asset":null}
{"command":"cancel-navigation","session":"2"}
```

Status reports `idle`, `preparing`, `cancelling`, `cancelled`, `failed`, `ready` or
`published`, plus captured revision/settings, result counts/source identity, route and
error. Cancel targets only the current pending/ready session. Publication does not
change the scene revision. Binding does; reacquire entity handles after that edit.
Query a prepared result before binding; result queries require its captured revision.
Graphical controls call these same product operations.

## Geometry and resources / 几何与资源

Boxes use exact simulation-scaled dimensions and world transforms. Mesh colliders use
actual collision positions/indices and preserve front faces under signed scaling.
Spheres/capsules use conservative circumscribed surfaces with radial error bounded by
`0.05*cellSize`; unsupported precision/capacity fails. Shear, invalid transforms and
nonuniform round-primitive scales fail as they do at the physics boundary. Sensors,
characters, moving bodies and render-only meshes are excluded.

Inputs have at most 262144 vertices/triangles and coordinates within ±1e6 metres.
Static mesh preparation owns at most 64 source files/64 MiB. Recast input work is
bounded to 16 million triangle/AABB cell estimates and 262144 padded cells per tile.
There are at most 256 tiles, 16384 polygons per tile, 262144 total polygons, and 1048576
coarse vertices, detail vertices and detail triangles respectively. Capacity exhaustion is explicit, without dropping
islands or silently simplifying collision input. Cancellation cooperates between bounded
assets/tiles/phases; it does not preempt an individual vendor call.

Settings: cell size [0.02,1], cell height [0.01,0.5], agent height [0.2,10], radius
[0.05,5], climb [0,2] and less than height, slope [0,80] degrees; tile cells are powers
of two from 32 to 256. Resolution quantizes clearance/climb conservatively.

`.dotnav` contains eight magic bytes `DOTNAV\0\1`, then explicit little-endian float32/
integer fields for settings, origin, a 32-byte lowercase geometry identity, and bounded
engine tile/polygon/detail arrays. It is at most 64 MiB with exact length; no vendor
structs, pointers, native padding or fallback layout. Decode validates topology before
creating native data. XXH3-128 identifies canonical world triangle content, including
normalized signed zero; it is not cryptographic authentication. Editor publication also
checks exact input bytes. Prepared surfaces are immutable; copies may cross threads.

盒体与网格遵循实际仿真缩放及正面规则；球体/胶囊仅作有误差上限的保守外包。
不支持的精度、容量、错切或形状缩放明确失败，不丢弃岛屿或伪造成功。
DotNav 使用有界、精确长度的小端引擎数据，载入前校验拓扑，不落盘第三方内存布局。
几何哈希不是密码学认证，编辑发布另行精确检查来源字节。

## Runtime / 运行时

```ts
import {navigation} from 'dot:engine';

const surface = navigation.stats(); // null when the scene has no binding
const route = navigation.route(character.id, station);
// route.start/goal expose projection; route.points is owned read-only corner data.
```

`route` reads current feet and the simulation-scaled capsule through an owned native
physics reader. Pending character/ancestor edits must synchronize first; faulted
physics rejects the query. Step/stick distances remain world metres under scale. Its baked height/radius must
contain the scaled capsule; baked climb/slope cannot exceed character capabilities.
Optional search extents default to 0.5 m per axis, each in (0,10]; maximum corners
are 2..512, default 256. Every query owns private Detour scratch (8192 nodes/corridor
entries). Missing polygons, disconnected/partial corridors, node/corner exhaustion,
agent mismatch and stale geometry return contextual errors, never a straight-line
fallback or a partial route labeled complete. Queries are read-only during migration.

Creation proves triangle identity against owned collision resources. Subsequent queries
compare exact static shape/resource descriptions and world matrices, avoiding repeated
triangle cooking inside a script callback. Mesh resources stay immutable for the scope,
as in native simulation caches. Moving actors and non-geometric body changes do not
invalidate this proof; changing static shape, mesh identity or world pose does.
No query reopens project files. Scene scope/thread changes invalidate captured services.

This supports static route planning, not crowd avoidance, dynamic navmesh rebuilding,
area costs, authored jump/off-mesh links or world streaming. The PrismRelay crew is an
editable integrated example; it does not establish large-game scalability or final art.

原生查询从角色实际脚底开始，检查体型及攀爬能力；断路、局部路径或容量不足明确返回错误。
每次查询独占工作区，不重读文件；创建时证明三角面身份，后续按精确静态配置与世界矩阵
检查是否过期。运动角色不使证明失效，静态形状/资源/世界位姿变化会使其失效。
当前不提供群体避让、动态重建、区域代价、跳跃连接或世界流送。
