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
The reference's terrain/stone artwork and pause/step are not current features.
Asset thumbnails, XYZ transform gizmos and opt-in renderer sampling have native
implementations; their boundaries are documented in Editor.md. Renderer intervals
exclude other application work and cannot be labelled game FPS. No rendered mock scene
or fixed status value may stand in for a live engine result.
已有编辑、材质/光照/环境/AO、外部源码打开、Play/Stop、导出和导航/选择可重新布局。
参考图中的地形/石拱美术及暂停/步进仍未提供；资源缩略图、XYZ 操纵器和可选渲染器
采样已有原生实现，其边界见 Editor.md。渲染区间不含整应用工作，不能标为游戏 FPS；
不得以效果图或固定状态替代实时引擎结果。

The workspace now uses real docked windows with the reference's initial proportions.
Users can drag tabs/splitters or undock panels inside the native editor window, focus
them through Window, and reset the layout. Dark/light chrome shares identical metrics
and never changes scene rendering. Assets/Console tabs show real resources and diagnostics. Assets search and action menus consume the actual
project catalog; there are no fabricated thumbnails or profiler numbers.
工作区现已使用真实停靠窗口与参考初始比例，可拖动标签/分隔线、在原生窗口内浮动、
通过 Window 聚焦与重置布局。深浅皮肤使用相同度量，不改变场景渲染；资源搜索和
操作菜单读取实际项目目录，不伪造缩略图或 profiler 数值。

The packaged font is Sarasa UI SC Regular 1.0.42, fixed by archive/hash and upstream
license revision. The UI family uses Inter for Western glyphs and integrates CJK
glyphs upstream; one scalable font resource provides mixed text with consistent
metrics. English is proportional, while Chinese/Japanese/Korean glyphs remain
available. This is the upstream UI composite, not a separately installed system font.
随包字体为固定归档/哈希及许可证提交的 Sarasa UI SC Regular 1.0.42。UI 字族以
Inter 提供西文字形并由上游整合 CJK，一份可缩放字体支持一致的混排度量，不依赖
系统安装字体。[Upstream / 上游](https://github.com/be5invis/Sarasa-Gothic#what-are-the-names).

Layout and theme currently live for the workspace session. Persisted per-user
profiles, plane/uniform-scale tools and further production tools remain planned.
The concepts and this contract do not establish a mature editor or polished game.
布局与主题当前在工作区会话内保留；用户配置持久化、平面/统一缩放操纵器及更多
生产工具仍待实现。效果图和本约定均不表示成熟编辑器或高质量游戏已经完成。

World/local Move handles now project actual selected transforms onto the live Scene
view. Their real draft/commit/cancel/Undo behavior is authoritative; concept artwork
does not substitute for later plane, uniform-scale or multi-selection tools.

Move/Rotate/Scale now expose genuine projected XYZ axes, affine-frame rotation rings
and scale endpoints. Dragging highlights the active axis and uses the actual draft
scene; cancellation and one-step Undo restore data and pixels. Mode-dependent snap
controls show meters, degrees or ratio units.
