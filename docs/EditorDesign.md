# Editor visual direction / 编辑器视觉方向

The supplied dark/light concepts guide visual hierarchy and workspace proportions.
They are concept artwork, not evidence of engine features or performance. The actual
engine remains authoritative for scene content, controls, diagnostics and status.
深浅效果图用于视觉层级和工作区比例；它们是概念图，不证明引擎能力或性能。
场景内容、控件、诊断与状态以实际引擎为准。

Use compact menus and a toolbar above a left hierarchy, central Scene/Game view and
right Inspector. Place assets below the hierarchy/view with Inspector continuing
to the bottom. Initial proportions follow roughly 18/60/22 percent and a bottom
asset region around 25 percent; adapt to minimum supported window sizes. Keep fine
separators, aligned fields, restrained blue selection and readable secondary text.
采用紧凑菜单和工具栏，左层级、中间 Scene/Game、右 Inspector；资源位于左中下方，
Inspector 延伸到底部。参考比例为 18/60/22 与约 25% 底部资源区，小窗口须适配。
使用细分隔线、对齐字段、克制蓝色选择态与清晰的次要文字。

Dark chrome uses neutral graphite panels near #202226, surround #181A1E, fields
#17191D and accent #6C9EFF. Light chrome uses #F5F6F8 panels, #E8EBEF surround,
white fields with #CDD2DA borders and #356FD6 accent. Theme changes must preserve
scene lighting, exposure, camera, selection and all numeric values.
深色采用石墨中性色与蓝色强调，浅色采用冷灰面板、白色输入框与蓝色强调。
主题切换不得改变场景光照、曝光、相机、选择与数值。

Existing scene/document, material/light/environment/AO authoring, external source
opening, Play/Stop, exports and navigation/selection provide real controls to arrange.
The reference's terrain/stone artwork, thumbnails, transform gizmos, pause/step and
profiler require their own implementations and verification before appearing as
functional tools. Performance labels require measurements. No rendered mock scene
or fixed status value may stand in for a live engine result.
已有编辑、材质/光照/环境/AO、外部源码打开、Play/Stop、导出和导航/选择可重新布局。
参考图中的地形/石拱美术、缩略图、变换 gizmo、暂停/步进和 profiler 须先实现并验证；
性能标签必须来自测量，不得以效果图或固定状态替代实时引擎结果。

At this milestone the current workspace still uses its existing panel layout.
The navigation/selection foundation is implemented; theme/layout adaptation is
the subsequent editor stage. Neither the concepts nor this contract establishes
a mature editor or a polished 3D game.
本阶段仍使用当前面板布局，导航/选择基础已实现；主题和布局适配是后续编辑器阶段。
效果图和本约定均不表示成熟编辑器或高质量 3D 游戏已完成。
