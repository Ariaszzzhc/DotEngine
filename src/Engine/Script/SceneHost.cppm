module;

#include <memory>

export module Dot.Script.SceneHost;

export import Dot.Script.Host;
import Dot.Core.Result;
import Dot.World.Scene;

export namespace Dot::Script {
  /** Scene/entity/transform/hierarchy and component authoring services. Bindings retain shared
   * scene ownership and its original identity; moving/replacing that Scene invalidates calls.
   * Creation/operations require the scene's owner thread. IDs cross JSON as {scene,value} canonical
   * positive decimal strings, never JS numbers. Each method validates exact argument/object shapes
   * before its native mutation; getters return owned snapshots; null removes a component. Authoring
   * does not initialize physics/audio/script backends or load assets. Primary lookups return null
   * when absent; exists is false only for same-scene missing IDs. Successful earlier calls and
   * native side effects are not script transactions.
   * 场景/实体/变换/层级及组件编辑服务；绑定共享拥有场景并保留原身份，移动/替换该 Scene
   * 会使调用失效。创建/操作须在场景所有者线程进行；ID 经 JSON 传输为 {scene,value}
   * 规范正十进制字符串，不使用 JS 数字。每个方法在原生修改前校验精确参数/对象形状；
   * getter 返回拥有快照，null 移除组件；编辑不初始化物理/音频/脚本后端、不加载资源。
   * 无主组件时查询返回 null；exists 仅对本场景缺失 ID 返回 false。
   * 已成功调用及原生副作用不构成脚本事务。 */
  [[nodiscard]] auto sceneBindings(std::shared_ptr<World::Scene> scene)
      -> Core::Result<HostBindings>;
}  // namespace Dot::Script
