import {input, ScriptComponent} from 'dot:engine';

/**
 * Arrow keys move the real capsule; J requests a grounded jump. A child mesh
 * follows native scene writeback without scripted collision or gravity.
 * 方向键控制真实胶囊，J 请求落地跳跃；子网格跟随原生场景回写，不以脚本模拟碰撞或重力。
 */
export class CharacterDemo extends ScriptComponent {
  onFixedUpdate(): void {
    const x = Number(input.key('Right').down) - Number(input.key('Left').down);
    const z = Number(input.key('Down').down) - Number(input.key('Up').down);
    const length = Math.hypot(x, z);
    const speed = length > 0 ? 3 / length : 0;
    this.self.moveCharacter({x: x * speed, y: 0, z: z * speed}, input.key('J').pressed);

    if (length > 0) {
      this.self.setCharacterYaw(Math.atan2(-x, -z) * 180 / Math.PI);
    }
  }

  onStop(): void {
    this.self.moveCharacter({x: 0, y: 0, z: 0});
  }
}
