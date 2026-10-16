import {input, ScriptComponent, type Vec3} from 'dot:engine';

/**
 * T resets the authored slider and native velocity. Collision and stopping at
 * the wall belong to Jolt; the script only bounds the demonstration area.
 * T 重置编辑的滑块位置及原生速度，墙体碰撞由 Jolt 处理；脚本只限制展示区域。
 */
export class CollisionDemo extends ScriptComponent<{spawn: Vec3}> {
  #frame = '';

  private validate(): void {
    const spawn = this.properties.spawn;
    const body = this.self.rigidBody;

    if (!spawn || ![spawn.x, spawn.y, spawn.z].every(Number.isFinite)
        || Math.max(Math.abs(spawn.x), Math.abs(spawn.y), Math.abs(spawn.z)) > 100 || !body
        || body.motion !== 'dynamic' || body.sensor) {
      throw Error('Collision demonstration requires its authored solid body and bounded spawn');
    }
  }

  onStart(): void {
    this.validate();
  }

  onUpdate(): void {
    const frame = input.snapshot();

    const fresh = frame.index !== this.#frame;
    this.#frame = frame.index;

    if (fresh && frame.keys.T.pressed) {
      const pose = this.self.transform;
      pose.position = {...this.properties.spawn};
      this.self.transform = pose;
      this.self.setVelocity(this.self.rigidBody!.velocity);
      this.self.setAngularVelocity({x: 0, y: 0, z: 0});
    }
  }

  onFixedUpdate(): void {
    if (this.self.transform.position.x >= 0) {
      this.self.setVelocity({x: 0, y: 0, z: 0});
    }
  }

  onSaveState(): {frame: string} {
    return {frame: this.#frame};
  }

  onLoadState(state: {frame: string}): void {
    if (!state || typeof state.frame !== 'string' || state.frame.length > 20
        || !/^[0-9]*$/.test(state.frame)) {
      throw Error('Invalid collision demonstration checkpoint');
    }

    this.validate();
    this.#frame = state.frame;
  }

  onStop(): void {
    this.self.setVelocity({x: 0, y: 0, z: 0});
  }
}
