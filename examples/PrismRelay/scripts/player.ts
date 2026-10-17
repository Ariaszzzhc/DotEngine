import {audio, Entity, gameClock, hud, input, type InputFrame, scene, ScriptComponent, type Vec3} from 'dot:engine';

import {type CameraState, ShoulderCamera} from './camera';
import {progressFailure, progressStatus, readCheckpoint, saveCheckpoint} from './progress';

interface Settings {
  walkSpeed: number;
  runSpeed: number;
  uiFontSize: number;
}

interface RuntimeState {
  camera: CameraState;
  velocity: Vec3;
  edgeFrame: string;
  lookFrame: string;
  progressFrame: string;
  jumpBuffer: number;
  grounded: boolean;
  clip: number;
}

/**
 * Camera-relative intent drives the native capsule; gravity, support and collision
 * remain in Jolt. The native animation player owns sampled poses and cross-fades.
 * 相机朝向的运动意图驱动原生胶囊；重力、支撑及碰撞仍由 Jolt 处理，原生动画播放器采样并混合姿态。
 */
export class RelayPlayer extends ScriptComponent<Settings> {
  #camera!: ShoulderCamera;
  #model!: Entity;
  #listener!: Entity;
  #velocity: Vec3 = {x: 0, y: 0, z: 0};
  #edgeFrame = '';
  #lookFrame = '';
  #progressFrame = '';
  #jumpBuffer = 0;
  #grounded = true;
  #clip = 0;
  #menuVisible = false;

  private bind(): void {
    const camera = scene.primaryCamera;
    const listener = scene.primaryListener;
    const models = scene.findEntities('Surveyor');
    const model = models.length === 1 ? models[0] : undefined;

    if (!camera || !model || !listener || listener.parent !== null || !listener.audioListener
        || model.parent?.id.value !== this.self.id.value || !this.self.character
        || !Number.isFinite(this.properties.walkSpeed) || this.properties.walkSpeed <= 0
        || !Number.isFinite(this.properties.runSpeed) || this.properties.runSpeed > 12
        || this.properties.runSpeed < this.properties.walkSpeed
        || !Number.isFinite(this.properties.uiFontSize) || this.properties.uiFontSize < 8
        || this.properties.uiFontSize > 64) {
      throw Error(
          'Prism Relay requires its authored character, camera, model and valid movement/UI settings');
    }

    this.#camera = new ShoulderCamera(camera, this.self.id);
    this.#model = model;
    this.#listener = listener;
  }

  onStart(): void {
    this.bind();
    input.capturePointer(true);
    this.#model.configureAnimation(1, true);
    this.#model.playAnimation(0, true);
    this.#camera.update(this.self.characterState.position, 0);
    this.listen();
    hud.set('controls', hud.label({
      text:
          'PRISM RELAY | WASD move | Shift sprint | Space jump | Mouse orbit\nF6 save grounded checkpoint | F7 resume checkpoint | M menu',
      size: {x: .96, y: .14},
      fontSize: this.properties.uiFontSize
    }));
    progressStatus('F6 saves a grounded checkpoint; F7 restores it', this.properties.uiFontSize);
  }

  private edges(frame: InputFrame): void {
    if (frame.index !== this.#edgeFrame) {
      this.#edgeFrame = frame.index;

      if (frame.keys.Space.pressed) {
        this.#jumpBuffer = 0.14;
      }
    }
  }

  onFixedUpdate(seconds: number): void {
    const frame = input.snapshot();
    this.edges(frame);
    const x = Number(frame.keys.D.down || frame.keys.Right.down)
        - Number(frame.keys.A.down || frame.keys.Left.down);
    const z = Number(frame.keys.S.down || frame.keys.Down.down)
        - Number(frame.keys.W.down || frame.keys.Up.down);
    const magnitude = Math.hypot(x, z);
    const speed = frame.keys.LeftShift.down || frame.keys.RightShift.down
        ? this.properties.runSpeed
        : this.properties.walkSpeed;
    const scale = magnitude > 0 ? speed / magnitude : 0;
    const yaw = this.#camera.yaw;
    const wanted = {
      x: (Math.cos(yaw) * x + Math.sin(yaw) * z) * scale,
      z: (-Math.sin(yaw) * x + Math.cos(yaw) * z) * scale,
    };
    const grounded = this.self.characterState.ground === 'grounded';
    const blend = 1 - Math.exp(-(grounded ? 16 : 4) * seconds);
    this.#velocity.x += (wanted.x - this.#velocity.x) * blend;
    this.#velocity.z += (wanted.z - this.#velocity.z) * blend;
    const jump = this.#jumpBuffer > 0 && grounded;
    this.#jumpBuffer = jump ? 0 : Math.max(0, this.#jumpBuffer - seconds);
    this.self.moveCharacter(this.#velocity, jump);

    if (Math.hypot(this.#velocity.x, this.#velocity.z) > 0.05) {
      this.self.setCharacterYaw(Math.atan2(-this.#velocity.x, -this.#velocity.z) * 180 / Math.PI);
    }
  }

  private animate(): void {
    const state = this.self.characterState;
    const grounded = state.ground === 'grounded';
    const animation = this.#model.animationState;
    const finished = animation.timeSeconds >= animation.durationSeconds - 0.02;
    const speed = Math.hypot(state.velocity.x, state.velocity.z);
    let clip: number;

    if (!grounded) {
      clip = this.#grounded && state.velocity.y > 0               ? 4
          : this.#clip === 4 && !finished && state.velocity.y > 0 ? 4
                                                                  : 5;
    } else if (
        !this.#grounded
        || (this.#clip === 6 && !finished && (speed < 0.15 || animation.timeSeconds < 0.15))) {
      clip = 6;
    } else {
      clip = speed < 0.15 ? 0 : speed < 2 ? 1 : speed < 4.5 ? 2 : 3;
    }

    if (clip !== this.#clip) {
      this.#model.configureAnimation(1, clip < 4 || clip === 5);
      this.#model.crossFadeAnimation(clip, clip === 4 ? 0.06 : 0.12);
      this.#clip = clip;
    }

    this.#grounded = grounded;
  }

  onFrame(_seconds: number): void {
    const frame = input.snapshot();
    let progressMessage: string|null = null;

    if (frame.index !== this.#progressFrame) {
      this.#progressFrame = frame.index;

      if (frame.keys.M.pressed) {
        gameClock.setPaused(!gameClock.snapshot().paused);
      }

      try {
        if (frame.keys.F7.pressed) {
          const saved = readCheckpoint();
          this.self.teleportCharacter(saved.feet, saved.yawDegrees);
          audio.resetMotion();
          this.#camera.restore(saved.camera);
          this.#velocity = {x: 0, y: 0, z: 0};
          this.#jumpBuffer = 0;
          this.#grounded = this.self.characterState.ground === 'grounded';
          this.#clip = 0;
          this.#model.configureAnimation(1, true);
          this.#model.playAnimation(0, true);
          this.#camera.update(this.self.characterState.position, 0);
          this.listen();
          progressMessage = 'Checkpoint restored';
        } else if (frame.keys.F6.pressed) {
          const state = this.self.characterState;

          if (state.ground !== 'grounded') {
            throw Error('Land before saving a checkpoint');
          }

          saveCheckpoint({
            format: 'prism-relay-checkpoint',
            path: 'scenes/main.dotscene',
            feet: {...state.position},
            yawDegrees: Math.atan2(
                            2 * state.rotation.w * state.rotation.y,
                            1 - 2 * state.rotation.y * state.rotation.y)
                * 180 / Math.PI,
            camera: this.#camera.state,
          });
          progressMessage = 'Checkpoint saved';
        }
      } catch (error) {
        progressFailure(error, this.properties.uiFontSize);
      }
    }

    if (progressMessage !== null) {
      progressStatus(progressMessage, this.properties.uiFontSize);
    }

    const paused = gameClock.snapshot().paused;

    if (paused !== this.#menuVisible) {
      this.#menuVisible = paused;
      input.capturePointer(!paused);

      if (paused) {
        hud.set('menu', hud.label({
          text: 'GAME PAUSED\nF6 Save checkpoint\nF7 Restore checkpoint\nM Return to game',
          position: {x: .26, y: .32},
          size: {x: .48, y: .36},
          fontSize: this.properties.uiFontSize,
          background: {x: .02, y: .03, z: .05, w: .96}
        }));
      } else {
        hud.remove('menu');
      }
    }
  }

  onUpdate(seconds: number): void {
    const frame = input.snapshot();
    this.edges(frame);

    if (frame.index !== this.#lookFrame) {
      this.#lookFrame = frame.index;
      this.#camera.look(frame);
    }

    this.#camera.update(this.self.characterState.position, seconds);
    this.listen();
    this.animate();
  }

  /**
   * Hear from the avatar with camera orientation. Orbiting a boom must not create
   * artificial listener velocity or Doppler spikes.
   * 听者位于角色头部并采用镜头朝向，环绕镜头不产生虚假的听者速度或多普勒突变。
   */
  private listen(): void {
    const feet = this.self.characterState.position;
    const pose = this.#camera.camera.transform;
    pose.position = {x: feet.x, y: feet.y + 1.5, z: feet.z};
    this.#listener.transform = pose;
  }

  onSaveState(): RuntimeState {
    return {
      camera: this.#camera.state,
      velocity: {...this.#velocity},
      edgeFrame: this.#edgeFrame,
      lookFrame: this.#lookFrame,
      progressFrame: this.#progressFrame,
      jumpBuffer: this.#jumpBuffer,
      grounded: this.#grounded,
      clip: this.#clip,
    };
  }

  onLoadState(state: RuntimeState): void {
    if (!state || !state.velocity || ![state.velocity.x, state.velocity.z].every(Number.isFinite)
        || state.velocity.y !== 0 || Math.hypot(state.velocity.x, state.velocity.z) > 12.01
        || !Number.isFinite(state.jumpBuffer) || state.jumpBuffer < 0 || state.jumpBuffer > 0.14
        || typeof state.grounded !== 'boolean' || !Number.isInteger(state.clip) || state.clip < 0
        || state.clip > 6 || typeof state.edgeFrame !== 'string'
        || !/^[0-9]*$/.test(state.edgeFrame) || typeof state.lookFrame !== 'string'
        || !/^[0-9]*$/.test(state.lookFrame) || typeof state.progressFrame !== 'string'
        || !/^[0-9]*$/.test(state.progressFrame)) {
      throw Error('Invalid Prism Relay character checkpoint');
    }

    this.bind();
    this.#menuVisible = gameClock.snapshot().paused;
    this.#camera.restore(state.camera);
    this.#velocity = {...state.velocity};
    this.#edgeFrame = state.edgeFrame;
    this.#lookFrame = state.lookFrame;
    this.#progressFrame = state.progressFrame;
    this.#jumpBuffer = state.jumpBuffer;
    this.#grounded = state.grounded;
    this.#clip = state.clip;
  }

  onStop(): void {
    input.capturePointer(false);
    this.self.moveCharacter({x: 0, y: 0, z: 0});
    this.#model.stopAnimation();
  }
}
