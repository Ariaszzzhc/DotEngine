import {Entity, type EntityId, type InputFrame, physics, type Vec3} from 'dot:engine';

export interface CameraState {
  yaw: number;
  pitch: number;
  distance: number;
  boom: number;
}

const radius = 0.22;
const clearance = 0.035;

/**
 * The game owns orbit/follow policy; native Jolt sphere sweeps own obstruction.
 * Tightening is immediate, recovery is damped along the newly swept clear path.
 * 游戏定义环绕/跟随策略，原生 Jolt 球体扫描负责遮挡；收缩立即生效，沿当前安全路径平滑恢复。
 */
export class ShoulderCamera {
  #state: CameraState = {yaw: 0, pitch: -0.12, distance: 3.2, boom: 3.2};
  readonly camera: Entity;
  readonly #ignore: EntityId|null;

  constructor(camera: Entity, ignore: EntityId|null = null) {
    if (!camera.camera || camera.parent !== null) {
      throw Error('Shoulder camera requires a root camera entity');
    }

    this.camera = camera;
    this.#ignore = ignore;
  }

  get yaw(): number {
    return this.#state.yaw;
  }

  get state(): CameraState {
    return {...this.#state};
  }

  restore(state: CameraState): void {
    if (!state || ![state.yaw, state.pitch, state.distance, state.boom].every(Number.isFinite)
        || Math.abs(state.yaw) > Math.PI || state.pitch < -1.1 || state.pitch > 0.75
        || state.distance < 1.5 || state.distance > 5 || state.boom < 0 || state.boom > 5.1) {
      throw Error('Invalid shoulder camera checkpoint');
    }

    this.#state = {...state};
  }

  look(frame: InputFrame): void {
    if (!frame.pointerCaptured) {
      return;
    }

    const yaw = this.#state.yaw - frame.pointerDelta.x * 0.003;
    this.#state.yaw = Math.atan2(Math.sin(yaw), Math.cos(yaw));
    this.#state.pitch
        = Math.max(-1.1, Math.min(0.75, this.#state.pitch - frame.pointerDelta.y * 0.003));
    this.#state.distance = Math.max(1.5, Math.min(5, this.#state.distance - frame.wheel.y * 0.3));
  }

  update(feet: Vec3, seconds: number): void {
    const {yaw, pitch, distance} = this.#state;
    let pivot = {x: feet.x, y: feet.y + 1.5, z: feet.z};
    const wanted = {
      x: pivot.x + Math.cos(yaw) * 0.45 + Math.sin(yaw) * Math.cos(pitch) * distance,
      y: pivot.y - Math.sin(pitch) * distance,
      z: pivot.z - Math.sin(yaw) * 0.45 + Math.cos(yaw) * Math.cos(pitch) * distance,
    };

    for (let attempt = 0; attempt < 4; ++attempt) {
      const offset = {x: wanted.x - pivot.x, y: wanted.y - pivot.y, z: wanted.z - pivot.z};
      const length = Math.hypot(offset.x, offset.y, offset.z);

      if (length < 0.001) {
        throw Error('Shoulder camera has no valid sweep direction');
      }

      const hit = physics.sphereCast(pivot, radius, offset, {ignoreEntity: this.#ignore});

      if (hit && hit.fraction === 0 && hit.penetrationDepth > 0) {
        if (!hit.normal) {
          throw Error('Shoulder camera overlap has no separation normal');
        }

        const push = hit.penetrationDepth + clearance;
        pivot = {
          x: pivot.x + hit.normal.x * push,
          y: pivot.y + hit.normal.y * push,
          z: pivot.z + hit.normal.z * push,
        };
        continue;
      }

      const clear = hit ? Math.max(0, length * hit.fraction - clearance) : length;
      this.#state.boom = Math.min(
          clear, this.#state.boom + (length - this.#state.boom) * (1 - Math.exp(-10 * seconds)));
      const factor = this.#state.boom / length;
      const pose = this.camera.transform;
      pose.position = {
        x: pivot.x + offset.x * factor,
        y: pivot.y + offset.y * factor,
        z: pivot.z + offset.z * factor,
      };
      pose.rotation = {
        x: Math.cos(yaw / 2) * Math.sin(pitch / 2),
        y: Math.sin(yaw / 2) * Math.cos(pitch / 2),
        z: -Math.sin(yaw / 2) * Math.sin(pitch / 2),
        w: Math.cos(yaw / 2) * Math.cos(pitch / 2),
      };
      this.camera.transform = pose;
      return;
    }

    throw Error('Shoulder camera could not separate its pivot from geometry');
  }
}
