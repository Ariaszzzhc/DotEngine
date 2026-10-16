import {input, scenes, ScriptComponent} from 'dot:engine';

/**
 * M requests native relative mouse look. The frontend owns focus and Escape;
 * only acknowledged frames rotate the camera, independently of fixed-step count.
 * M 请求原生相对鼠标观察；前端负责焦点与 Escape，仅实际确认的帧更新镜头，独立于固定步数。
 */
export class CameraDemo extends ScriptComponent<{targetScene?: string}> {
  #request: string|null = null;

  onUpdate(): void {
    if (this.#request !== null) {
      const result = scenes.status(this.#request);

      if (result.state === 'failed' || result.state === 'cancelled') {
        this.#request = null;
      }
    }

    if (this.#request === null && input.key('L').pressed) {
      this.#request = scenes.request(this.properties.targetScene ?? 'scenes/LightingStudio.dotscene');
      return;
    }

    if (input.key('M').pressed) {
      input.capturePointer(!input.pointerCaptured);
    }

    if (!input.pointerCaptured) {
      return;
    }

    const delta = input.pointerDelta;
    const pose = this.self.transform;
    const q = pose.rotation;
    const yaw = Math.atan2(2 * (q.x * q.z + q.w * q.y), 1 - 2 * (q.x * q.x + q.y * q.y))
        - delta.x * 0.003;
    const pitch = Math.max(
        -1.4,
        Math.min(
            1.4,
            Math.asin(Math.max(-1, Math.min(1, 2 * (q.w * q.x - q.y * q.z)))) - delta.y * 0.003));
    pose.rotation = {
      x: Math.cos(yaw / 2) * Math.sin(pitch / 2),
      y: Math.sin(yaw / 2) * Math.cos(pitch / 2),
      z: -Math.sin(yaw / 2) * Math.sin(pitch / 2),
      w: Math.cos(yaw / 2) * Math.cos(pitch / 2),
    };
    this.self.transform = pose;
  }

  onStop(): void {
    input.capturePointer(false);
  }
}
