import {gameState, hud, type JsonValue, saves, scenes, type Vec3} from 'dot:engine';

import {type CameraState, validateCameraState} from './camera';

export interface RelayCheckpoint {
  format: 'prism-relay-checkpoint';
  path: 'scenes/main.dotscene';
  feet: Vec3;
  yawDegrees: number;
  camera: CameraState;
}

const slot = 'relay-checkpoint';
const key = 'relayCheckpoint';

function object(value: JsonValue|undefined, fields: string[]): Record<string, JsonValue> {
  if (!value || typeof value !== 'object' || Array.isArray(value)
      || Object.keys(value).length !== fields.length
      || fields.some(field => !Object.hasOwn(value, field))) {
    throw Error('Invalid Prism Relay checkpoint fields');
  }

  return value;
}

/**
 * Validate the complete owned save before any native pose, camera or live data change.
 * It is a grounded resume point, not a physics/skeleton snapshot or migration state.
 * 修改原生位姿、镜头或实时数据前校验完整存档；这是着地恢复点，不是物理/骨架快照或热重载状态。
 */
export function checkpoint(value: JsonValue|undefined): RelayCheckpoint {
  const data = object(value, ['format', 'path', 'feet', 'yawDegrees', 'camera']);
  const feet = object(data.feet, ['x', 'y', 'z']);
  const camera = object(data.camera, ['yaw', 'pitch', 'distance', 'boom']);

  if (data.format !== 'prism-relay-checkpoint' || data.path !== 'scenes/main.dotscene'
      || data.path !== scenes.current().path ||
      [feet.x, feet.y, feet.z].some(
          value => typeof value !== 'number' || !Number.isFinite(value)
              || Math.abs(value) > 1000000)
      || typeof data.yawDegrees !== 'number' || !Number.isFinite(data.yawDegrees)
      || Math.abs(data.yawDegrees) > 180) {
    throw Error('Checkpoint does not match the current Prism Relay scene or pose limits');
  }

  const state = camera as unknown as CameraState;
  validateCameraState(state);
  return {
    format: 'prism-relay-checkpoint',
    path: data.path,
    feet: {x: feet.x as number, y: feet.y as number, z: feet.z as number},
    yawDegrees: data.yawDegrees,
    camera: {...state},
  };
}

export function saveCheckpoint(value: RelayCheckpoint): void {
  const validated = checkpoint(value as unknown as JsonValue);
  const previous = gameState.get(key);
  gameState.set(key, validated as unknown as JsonValue);

  try {
    saves.write(slot, true);
  } catch (error) {
    if (previous === undefined) {
      gameState.remove(key);
    } else {
      gameState.set(key, previous);
    }

    throw error;
  }
}

export function readCheckpoint(): RelayCheckpoint {
  return checkpoint(saves.read(slot).data[key]);
}

export function progressStatus(message: string, fontSize: number): void {
  gameState.set('relayProgressStatus', message);
  const failed = message.startsWith('Checkpoint unavailable:');
  const text = failed ? (message.includes('Land before saving')
                             ? 'Checkpoint unavailable: land before saving.'
                             : 'Checkpoint unavailable. F6 saves a grounded point.')
                      : message;
  hud.set('checkpoint', hud.label({
    text,
    position: {x: .02, y: .88},
    size: {x: .65, y: .1},
    fontSize,
    color: failed ? {x: 1, y: .7, z: .55, w: 1} : {x: .6, y: 1, z: .8, w: 1}
  }));
}

export function progressFailure(error: unknown, fontSize: number): void {
  const message = error instanceof Error ? error.message : String(error);
  progressStatus('Checkpoint unavailable: ' + Array.from(message).slice(0, 256).join(''), fontSize);
}
