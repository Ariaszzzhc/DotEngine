import {audio, Entity, gameClock, hud, ui, input, type InputFrame, scene, ScriptComponent, type Vec3} from 'dot:engine';

import {type CameraState, ShoulderCamera} from './camera';
import {progressFailure, progressStatus, readCheckpoint, saveCheckpoint} from './progress';

interface Settings {
  walkSpeed: number;
  runSpeed: number;
}

interface RuntimeState {
  camera: CameraState;
  velocity: Vec3;
  edgeFrame: string;
  lookFrame: string;
  progressFrame: string;
  uiSequence: string;
  jumpBuffer: number;
}

/**
 * Camera-relative intent drives the native capsule; gravity, support and collision
 * remain in Jolt. Typed intent drives the saved native animation state machine;
 * its state, clocks, fades and masked layers survive script reloads independently.
 * 相机朝向意图驱动原生胶囊；Jolt 处理重力/支撑/碰撞，类型意图驱动保存的原生动画状态机；
 * 状态、时钟、淡化及遮罩层独立保留，不由脚本重载重新创建。
 */
export class RelayPlayer extends ScriptComponent<Settings> {
  #camera!: ShoulderCamera;
  #model!: Entity;
  #listener!: Entity;
  #velocity: Vec3 = {x: 0, y: 0, z: 0};
  #edgeFrame = '';
  #lookFrame = '';
  #progressFrame = '';
  #uiSequence = '0';
  #jumpBuffer = 0;
  #menuVisible = false;

  private bind(): void {
    const camera = scene.primaryCamera;
    const listener = scene.primaryListener;
    const models = scene.findEntities('Surveyor');
    const model = models.length === 1 ? models[0] : undefined;

    if (!camera || !model || !listener || listener.parent !== null || !listener.audioListener
        || model.parent?.id.value !== this.self.id.value || !this.self.character
        || !model.animationState.controller
        || !model.animationState.layers.some(layer => layer.name === 'Inspection')
        || !Number.isFinite(this.properties.walkSpeed) || this.properties.walkSpeed <= 0
        || !Number.isFinite(this.properties.runSpeed) || this.properties.runSpeed > 12
        || this.properties.runSpeed < this.properties.walkSpeed
        || !ui.snapshot()?.nodes.some(node => node.key === 'menu' && node.kind === 'panel')) {
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
    this.#model.resetAnimationController();
    this.#camera.update(this.self.characterState.position, 0);
    this.listen();
    ui.visible('menu', false);
    progressStatus('F6 saves a grounded checkpoint; F7 restores it');
  }

  private edges(frame: InputFrame): void {
    if (frame.index !== this.#edgeFrame) {
      this.#edgeFrame = frame.index;

      if (frame.keys.Space.pressed && !hud.inputState().keyboard) {
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
    this.#model.setAnimationParameters({
      Speed: Math.hypot(state.velocity.x, state.velocity.z),
      Grounded: state.ground === 'grounded',
      Vertical: state.velocity.y,
    });

    const inspection = this.#model.animationState.layers.find(layer => layer.name === 'Inspection')!;

    if (inspection.active && !inspection.playing && !inspection.paused && !inspection.fade) {
      this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 0, seconds: .15, stop: true});
    }
  }

  /** Trigger/cancel intent only; native layers own clocks, fades and masked mixing.
   * 仅表达触发/取消意图；时钟、淡化及遮罩混合均由原生动画层负责。 */
  private inspectAction(): void {
    const layer = this.#model.animationState.layers.find(value => value.name === 'Inspection')!;

    if (layer.active) {
      this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 0, seconds: .12, stop: true});
    } else {
      this.#model.controlAnimationLayer('Inspection', {action: 'weight', weight: 0});
      this.#model.controlAnimationLayer('Inspection', {action: 'play', restart: true});
      this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 1, seconds: .12, stop: false});
    }
  }

  private checkpoint(load: boolean): void {
    let progressMessage: string|null = null;

    try {
      if (load) {
        const saved = readCheckpoint();
        this.self.teleportCharacter(saved.feet, saved.yawDegrees);
        audio.resetMotion();
        this.#camera.restore(saved.camera);
        this.#velocity = {x: 0, y: 0, z: 0};
        this.#jumpBuffer = 0;
        this.#model.resetAnimationController();
        this.#model.controlAnimationLayer('Inspection', {action: 'stop'});
        this.#camera.update(this.self.characterState.position, 0);
        this.listen();
        progressMessage = 'Checkpoint restored';
      } else {
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
      progressFailure(error);
    }

    if (progressMessage !== null) {
      progressStatus(progressMessage);
    }
  }

  onFrame(_seconds: number): void {
    const frame = input.snapshot();

    if (frame.index !== this.#progressFrame) {
      this.#progressFrame = frame.index;

      if (frame.keys.M.pressed) {
        gameClock.setPaused(!gameClock.snapshot().paused);
      }

      if (frame.keys.N.pressed && gameClock.snapshot().paused) {
        gameClock.step();
      }

      if (frame.keys.F7.pressed) {
        this.checkpoint(true);
      } else if (frame.keys.F6.pressed) {
        this.checkpoint(false);
      }

      if (!gameClock.snapshot().paused && frame.keys.E.pressed && !hud.inputState().keyboard) {
        this.inspectAction();
      }
    }

    const actions = ui.events(this.#uiSequence);

    if (actions.lost) {
      this.#uiSequence = actions.sequence;
      progressFailure(Error('Menu action history expired'));
    } else {
      /** Bound synchronous checkpoint work per frame; acknowledgements queue intent.
       * 每帧限制同步检查点工作量；确认仅表示意图入队。 */
      for (const action of actions.events.slice(0, 8)) {
        this.#uiSequence = action.sequence;

        if (!gameClock.snapshot().paused) {
          continue;
        }

        if (action.key === 'menu-0-return') {
          gameClock.setPaused(false);
        } else if (action.key === 'menu-1-save') {
          this.checkpoint(false);
        } else if (action.key === 'menu-2-restore') {
          this.checkpoint(true);
        }
      }
    }

    const paused = gameClock.snapshot().paused;

    if (paused !== this.#menuVisible) {
      input.capturePointer(!paused);
      input.requestTextInput(false);

      ui.visible('menu', paused);

      if (paused) {
        ui.focus('menu-0-return');
      }

      this.#menuVisible = paused;
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
      uiSequence: this.#uiSequence,
      jumpBuffer: this.#jumpBuffer,
    };
  }

  onLoadState(state: RuntimeState): void {
    if (!state || !state.velocity || ![state.velocity.x, state.velocity.z].every(Number.isFinite)
        || state.velocity.y !== 0 || Math.hypot(state.velocity.x, state.velocity.z) > 12.01
        || !Number.isFinite(state.jumpBuffer) || state.jumpBuffer < 0 || state.jumpBuffer > 0.14
        || typeof state.edgeFrame !== 'string'
        || !/^[0-9]*$/.test(state.edgeFrame) || typeof state.lookFrame !== 'string'
        || !/^[0-9]*$/.test(state.lookFrame) || typeof state.progressFrame !== 'string'
        || !/^[0-9]*$/.test(state.progressFrame) || typeof state.uiSequence !== 'string'
        || !/^(0|[1-9][0-9]*)$/.test(state.uiSequence)) {
      throw Error('Invalid Prism Relay character checkpoint');
    }

    this.bind();
    ui.events(state.uiSequence);
    this.#menuVisible = gameClock.snapshot().paused;
    this.#camera.restore(state.camera);
    this.#velocity = {...state.velocity};
    this.#edgeFrame = state.edgeFrame;
    this.#lookFrame = state.lookFrame;
    this.#progressFrame = state.progressFrame;
    this.#uiSequence = state.uiSequence;
    this.#jumpBuffer = state.jumpBuffer;
  }

  onStop(): void {
    input.capturePointer(false);
    this.self.moveCharacter({x: 0, y: 0, z: 0});
    this.#model.pauseAnimation();
  }
}
