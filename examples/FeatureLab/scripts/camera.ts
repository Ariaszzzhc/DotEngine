import {gameState, input, type JsonValue, saves, scenes, ScriptComponent, type Transform} from 'dot:engine';

/**
 * M requests native relative mouse look. The frontend owns focus and Escape;
 * only acknowledged frames rotate the camera, independently of fixed-step count.
 * M 请求原生相对鼠标观察；前端负责焦点与 Escape，仅实际确认的帧更新镜头，独立于固定步数。
 */
export class CameraDemo extends ScriptComponent<{targetScene?: string}> {
  #request: string|null = null;

  private checkpoint(value: JsonValue|undefined): {path: string; pose: Transform} {
    if (!value || typeof value !== 'object' || Array.isArray(value)
        || typeof value.path !== 'string' || !value.pose || typeof value.pose !== 'object') {
      throw new Error('The save does not contain a camera checkpoint');
    }

    return value as unknown as {
      path: string;
      pose: Transform
    };
  }

  private restoreCamera(): void {
    gameState.remove('restoreCamera');
    const checkpoint = this.checkpoint(gameState.get('cameraCheckpoint'));

    if (checkpoint.path !== scenes.current().path) {
      throw new Error('The checkpoint belongs to another scene');
    }

    this.self.transform = checkpoint.pose;
    gameState.set('cameraCheckpointStatus', 'Camera checkpoint restored');
  }

  private failure(error: unknown): void {
    gameState.remove('restoreCamera');
    gameState.set('cameraCheckpointStatus', error instanceof Error ? error.message : String(error));
  }

  onStart(): void {
    if (gameState.get('restoreCamera') === true) {
      try {
        this.restoreCamera();
      } catch (error) {
        this.failure(error);
      }
    }
  }

  onUpdate(): void {
    if (this.#request !== null) {
      const result = scenes.status(this.#request);

      if (result.state === 'failed' || result.state === 'cancelled') {
        this.#request = null;

        if (gameState.get('restoreCamera') === true) {
          this.failure(new Error(result.error?.message ?? 'Checkpoint scene request cancelled'));
        }
      }
    }

    if (this.#request === null && input.key('L').pressed) {
      this.#request
          = scenes.request(this.properties.targetScene ?? 'scenes/LightingStudio.dotscene');
      return;
    }

    if (input.key('F6').pressed) {
      try {
        const path = scenes.current().path;

        if (path === null) {
          throw new Error('A camera checkpoint requires a saved scene path');
        }

        gameState.set(
            'cameraCheckpoint', {path, pose: this.self.transform as unknown as JsonValue});
        saves.write('camera-checkpoint', true);
        gameState.set('cameraCheckpointStatus', 'Camera checkpoint saved');
      } catch (error) {
        this.failure(error);
      }
    }

    if (input.key('F7').pressed && this.#request === null) {
      try {
        const revision = gameState.snapshot().revision;
        const checkpoint = this.checkpoint(saves.read('camera-checkpoint').data.cameraCheckpoint);
        saves.load('camera-checkpoint', revision);

        if (checkpoint.path === scenes.current().path) {
          this.restoreCamera();
        } else {
          gameState.set('restoreCamera', true);
          this.#request = scenes.request(checkpoint.path);
          return;
        }
      } catch (error) {
        this.failure(error);
      }
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
