import {gameState, input, type JsonValue, type SaveTicket, saves, scenes, ScriptComponent, type Transform} from 'dot:engine';

/**
 * M requests native relative mouse look. The frontend owns focus and Escape;
 * only acknowledged frames rotate the camera, independently of fixed-step count.
 * M 请求原生相对鼠标观察；前端负责焦点与 Escape，仅实际确认的帧更新镜头，独立于固定步数。
 */
interface CameraSave {
  ticket: SaveTicket<'read'|'write'>;
  pose: {path: string; pose: Transform}|null;
  revision: string;
}

export class CameraDemo extends ScriptComponent<{targetScene?: string}> {
  #request: string|null = null;
  #save: CameraSave|null = null;

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

  private pollSave(): void {
    const pending = this.#save;

    if (pending === null) {
      return;
    }

    try {
      if (pending.ticket.operation === 'write') {
        const receipt = saves.take({id: pending.ticket.id, operation: 'write'});

        if (receipt.state === 'pending' || receipt.state === 'running') {
          return;
        }

        this.#save = null;

        if (receipt.state !== 'succeeded') {
          throw Error(receipt.error?.message ?? 'Camera save cancelled');
        }

        gameState.set('cameraCheckpoint', pending.pose as unknown as JsonValue);
        gameState.set('cameraCheckpointStatus', 'Camera checkpoint saved');
      } else {
        const receipt = saves.take({id: pending.ticket.id, operation: 'read'});

        if (receipt.state === 'pending' || receipt.state === 'running') {
          return;
        }

        this.#save = null;

        if (receipt.state !== 'succeeded') {
          throw Error(receipt.error?.message ?? 'Camera load cancelled');
        }

        const checkpoint = this.checkpoint(receipt.result.data.cameraCheckpoint);
        gameState.replace(receipt.result.data, pending.revision);

        if (checkpoint.path === scenes.current().path) {
          this.restoreCamera();
        } else {
          gameState.set('restoreCamera', true);
          this.#request = scenes.request(checkpoint.path);
        }
      }
    } catch (error) {
      this.#save = null;
      this.failure(error);
    }
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
    this.pollSave();

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

    if (this.#save === null && input.key('F6').pressed) {
      try {
        const path = scenes.current().path;

        if (path === null) {
          throw new Error('A camera checkpoint requires a saved scene path');
        }

        const pose = {path, pose: this.self.transform};
        const snapshot = gameState.snapshot();
        snapshot.data.cameraCheckpoint = pose as unknown as JsonValue;
        this.#save = {ticket: saves.write('camera-checkpoint', true, snapshot), pose, revision: snapshot.revision};
        gameState.set('cameraCheckpointStatus', 'Saving camera checkpoint...');
      } catch (error) {
        this.failure(error);
      }
    }

    if (this.#save === null && input.key('F7').pressed && this.#request === null) {
      try {
        gameState.set('cameraCheckpointStatus', 'Loading camera checkpoint...');
        this.#save = {ticket: saves.read('camera-checkpoint'), pose: null, revision: gameState.snapshot().revision};
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

  onSaveState(): {request: string|null; save: CameraSave|null} {
    return {request: this.#request, save: this.#save};
  }

  onLoadState(state: {request: string|null; save: CameraSave|null}): void {
    if (!state || (state.request !== null && typeof state.request !== 'string')) {
      throw Error('Invalid camera demo state');
    }

    if (state.save !== null) {
      if (!state.save || !state.save.ticket || !/^[1-9][0-9]*$/.test(state.save.revision)
          || (state.save.ticket.operation !== 'read' && state.save.ticket.operation !== 'write')) {
        throw Error('Invalid camera save operation');
      }

      saves.status(state.save.ticket);

      if (state.save.ticket.operation === 'write') {
        this.checkpoint(state.save.pose as unknown as JsonValue);
      }
    }

    this.#request = state.request;
    this.#save = state.save;
  }
}
