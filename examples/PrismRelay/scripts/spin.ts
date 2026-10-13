import {ScriptComponent} from "dot:engine";

/**
 * Animate a visual child so the authored sensor pose remains stable.
 * 动画只改变视觉子节点，保持编辑传感器位姿稳定。
 */
export class CrystalSpin extends ScriptComponent {
  #angle = 0;

  onUpdate(seconds: number): void {
    this.#angle = (this.#angle + seconds * 1.5) % (Math.PI * 2);
    const transform = this.self.transform;
    transform.rotation = {x: 0, y: Math.sin(this.#angle / 2), z: 0, w: Math.cos(this.#angle / 2)};
    this.self.transform = transform;
  }

  onSaveState(): {angle: number} {
    return {angle: this.#angle};
  }

  onLoadState(state: {angle: number}): void {
    if (!state || !Number.isFinite(state.angle) || state.angle < 0 || state.angle >= Math.PI * 2) {
      throw new Error('Invalid crystal animation checkpoint');
    }

    this.#angle = state.angle;
  }
}
