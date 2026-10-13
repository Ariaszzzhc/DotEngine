import {invoke} from 'dot:host';

import {audio, components, Entity, type HostError, input, type JsonValue, keys, mouseButtons, physics, scene, ScriptComponent, type Transform} from './dot/engine.js';

/**
 * Equality checks compare DTO contents independently of member order, with native float tolerance.
 * DTO 内容独立于对象成员顺序进行比较。
 */
function equal(first: unknown, second: unknown): boolean {
  if (first === second) {
    return true;
  }

  if (typeof first === 'number' && typeof second === 'number') {
    return Number.isFinite(first) && Number.isFinite(second)
        && Math.abs(first - second) <= 1e-6 * Math.max(1, Math.abs(first), Math.abs(second));
  }

  if (!first || !second || typeof first !== 'object' || typeof second !== 'object') {
    return false;
  }

  const a = first as Record<string, unknown>;
  const b = second as Record<string, unknown>;
  const names = Object.keys(a).sort();
  return names.join('|') === Object.keys(b).sort().join('|')
      && names.every(name => equal(a[name], b[name]));
}

/**
 * Native scene metadata stores completed checks; it is never an inferred claim.
 * 原生场景元数据保存已完成检查，覆盖声明必须来自实际断言。
 */
export class Audit extends ScriptComponent<{contract: string}> {
  #checked = new Set<string>();
  #starts = 0;
  #fixed = 0;
  #updates = 0;
  #stops = 0;
  #errors = 0;

  private check(name: string, condition: boolean): void {
    if (!condition) {
      throw new Error('Feature Lab failed: ' + name);
    }

    this.#checked.add(name);
  }

  private expectedError(action: () => void, code: string): void {
    try {
      action();
    } catch (error) {
      const value = error as HostError;
      this.check(
          'HostError',
          value.name === 'DotHostError' && value.code === code && value.message.length > 0);
      ++this.#errors;
      return;
    }

    throw new Error('Expected native failure: ' + code);
  }

  private entity(name: string): Entity {
    const found = scene.entities().find(entity => entity.name === name);

    if (!found) {
      throw new Error('Missing authored probe: ' + name);
    }

    return found;
  }

  private inputChecks(): void {
    const frame = input.snapshot();
    this.check('keys', Object.isFrozen(keys) && new Set(keys).size === keys.length);
    this.check(
        'mouseButtons',
        Object.isFrozen(mouseButtons) && new Set(mouseButtons).size === mouseButtons.length);
    this.check('input.snapshot', frame.index === input.index);
    this.check('input.key', keys.every(key => equal(input.key(key), frame.keys[key])));
    this.check(
        'input.button',
        mouseButtons.every(button => equal(input.button(button), frame.buttons[button])));
    this.check('input.pointer.get', equal(input.pointer, frame.pointer));
    this.check('input.pointerDelta.get', equal(input.pointerDelta, frame.pointerDelta));
    this.check('input.wheel.get', equal(input.wheel, frame.wheel));
    this.check('input.text.get', input.text === frame.text);
    this.check('input.index.get', /^(0|[1-9][0-9]*)$/.test(input.index));
  }

  private sceneChecks(): void {
    const before = scene.info();
    this.check(
        'scene.info',
        before.entities === scene.entities().length && before.id === this.self.id.scene);
    scene.rename('Feature Lab');
    this.check('scene.rename', scene.info().name === 'Feature Lab');
    this.check('scene.primaryCamera.get', scene.primaryCamera?.name === 'Camera');
    this.check('scene.primaryListener.get', scene.primaryListener?.name === 'Camera');
    const scratch = scene.create('Scratch');
    this.check('scene.create', scratch.exists && scene.info().entities === before.entities + 1);
    const id = {...scratch.id};
    const view = new Entity(id);
    id.value = '0';
    this.check(
        'Entity.constructor', view.id.value === scratch.id.value && Object.isFrozen(view.id));
    this.check('Entity.id', view.id.scene === before.id && /^[1-9][0-9]*$/.test(view.id.value));
    this.check('Entity.id.precision', BigInt(view.id.value) > 9007199254740992n);
    this.check('Entity.info', view.info().id.value === view.id.value);
    this.check('Entity.name.get', view.name === 'Scratch');
    view.name = 'Renamed';
    this.check('Entity.name.set', view.name === 'Renamed');
    const pose = view.transform;
    pose.position.x = 2;
    this.check('Entity.transform.get', view.transform.position.x === 0);
    view.transform = pose;
    this.check('Entity.transform.set', view.transform.position.x === 2);
    const parent = scene.create('Parent');
    const parentPose = parent.transform;
    parentPose.position.x = 1;
    parent.transform = parentPose;
    view.parent = parent;
    this.check('Entity.parent.set', view.parent?.id.value === parent.id.value);
    this.check('Entity.parent.get', view.info().parent?.value === parent.id.value);
    this.check(
        'Entity.worldMatrix', view.worldMatrix().length === 16 && view.worldMatrix()[12] === 3);
    view.parent = null;
    this.check(
        'scene.entities', scene.entities().some(entity => entity.id.value === view.id.value));

    const camera = components.camera();
    this.check(
        'components.camera', camera.projection === 'perspective' && camera.nearPlane === 0.1);
    view.camera = camera;
    this.check('Entity.camera.set', equal(view.camera, camera));
    this.check('Entity.camera.get', view.info().camera?.primary === false);
    view.camera = null;
    const mesh = components.mesh({kind: 'quad'});
    this.check('components.mesh', mesh.asset === '' && mesh.visible);
    view.mesh = mesh;
    this.check('Entity.mesh.set', equal(view.mesh, mesh));
    this.check('Entity.mesh.get', view.info().mesh?.kind === 'quad');
    view.mesh = null;
    const body = components.rigidBody({gravityFactor: 0});
    this.check('components.rigidBody', body.motion === 'dynamic' && body.shape === 'sphere');
    view.rigidBody = body;
    this.check('Entity.rigidBody.set', equal(view.rigidBody, body));
    this.check('Entity.rigidBody.get', view.info().rigidBody?.mass === 1);
    view.rigidBody = null;
    const source = components.audioSource('assets/tone.wav');
    this.check('components.audioSource', source.playOnStart && source.voice.volume === 1);
    view.audioSource = source;
    this.check('Entity.audioSource.set', equal(view.audioSource, source));
    this.check('Entity.audioSource.get', view.info().audioSource?.asset === source.asset);
    view.audioSource = null;
    const listener = components.audioListener();
    this.check('components.audioListener', listener.primary === false);
    view.audioListener = listener;
    this.check('Entity.audioListener.set', equal(view.audioListener, listener));
    this.check('Entity.audioListener.get', view.info().audioListener?.primary === false);
    view.audioListener = null;
    const script = components.script('audit.ts', 'AuditReport');
    script.enabled = false;
    script.properties = {owned: [true, 1, 'UTF-8']};
    this.check(
        'components.script', script.source === 'audit.ts' && script.className === 'AuditReport');
    view.script = script;
    this.check('Entity.script.set', equal(view.script, script));
    this.check('Entity.script.get', view.info().script?.enabled === false);
    view.script = null;
    this.check(
        'components.remove',
        view.camera === null && view.mesh === null && view.rigidBody === null
            && view.audioSource === null && view.audioListener === null && view.script === null);
    this.expectedError(() => {
      view.mesh = components.mesh({kind: 'asset', asset: '../bad.obj'});
    }, 'InvalidArgument');
    this.check('components.failedWrite', view.mesh === null);
    view.destroy();
    parent.destroy();
    this.check('Entity.destroy', scene.info().entities === before.entities);
    this.check('Entity.exists.get', !view.exists);
    this.expectedError(() => {
      view.info();
    }, 'NotFound');
    this.expectedError(() => {
      invoke('missing.method', []);
    }, 'NotFound');
    this.check('invoke', (invoke('scene.info', []) as {id: string}).id === scene.info().id);
  }

  private physicsChecks(): void {
    const dynamic = this.entity('Dynamic');
    const kinematic = this.entity('Kinematic');
    const initial = physics.body(dynamic.id);
    this.check('physics.body', initial.motion === 'dynamic' && !initial.sensor);
    this.check('Entity.physicsBody.get', equal(dynamic.physicsBody, initial));
    this.check('physics.stats', physics.stats().bodies === 5 && !physics.stats().faulted);
    this.check('physics.contacts.get', Array.isArray(physics.contacts.events));
    const hit = physics.rayCast({x: -2, y: 3, z: 0}, {x: 0, y: -6, z: 0});
    this.check('physics.rayCast', hit !== null && hit.fraction >= 0 && hit.fraction <= 1);
    physics.setVelocity(dynamic.id, {x: 1, y: 0, z: 0});
    this.check('physics.setVelocity', dynamic.physicsBody.velocity.x === 1);
    dynamic.setVelocity({x: 0, y: 0, z: 0});
    this.check('Entity.setVelocity', dynamic.physicsBody.velocity.x === 0);
    physics.setAngularVelocity(dynamic.id, {x: 0, y: 0, z: 0.25});
    this.check('physics.setAngularVelocity', dynamic.physicsBody.angularVelocity.z === 0.25);
    dynamic.setAngularVelocity({x: 0, y: 0, z: 0});
    this.check('Entity.setAngularVelocity', dynamic.physicsBody.angularVelocity.z === 0);
    physics.addImpulse(dynamic.id, {x: 1, y: 0, z: 0});
    this.check('physics.addImpulse', dynamic.physicsBody.velocity.x > 0.9);
    dynamic.addImpulse({x: -1, y: 0, z: 0});
    this.check('Entity.addImpulse', Math.abs(dynamic.physicsBody.velocity.x) < 0.001);
    physics.moveKinematic(kinematic.id, {x: 2.2, y: 0, z: 0}, {x: 0, y: 0, z: 0, w: 1}, 0.1);
    this.check('physics.moveKinematic', kinematic.physicsBody.velocity.x > 1.9);
    kinematic.moveKinematic({x: 2.1, y: 0, z: 0}, {x: 0, y: 0, z: 0, w: 1}, 0.1);
    this.check('Entity.moveKinematic', kinematic.physicsBody.velocity.x > 0.9);
    this.expectedError(() => {
      physics.setVelocity(dynamic.id, {x: 501, y: 0, z: 0});
    }, 'InvalidArgument');
    this.check('physics.failedControl', Math.abs(dynamic.physicsBody.velocity.x) < 0.001);
  }

  private audioChecks(): void {
    const voice = this.entity('Voice');
    const state = audio.voice(voice.id);
    this.check(
        'audio.voice',
        state.clip.channels === 1 && state.clip.sampleRate === 48000
            && BigInt(state.clip.frames) > 0n && state.clip.durationSeconds > 0);
    this.check('Entity.audioVoice.get', equal(voice.audioVoice, state));
    this.check(
        'audio.stats',
        audio.stats().assets === 1 && audio.stats().voices === 1 && !audio.stats().faulted);
    this.check(
        'audio.listener',
        equal(audio.listener().position, this.entity('Camera').transform.position));
    audio.play(voice.id);
    this.check('audio.play', voice.audioVoice.playing);
    audio.pause(voice.id);
    this.check('audio.pause', !voice.audioVoice.playing);
    audio.seek(voice.id, '3');
    this.check('audio.seek', voice.audioVoice.sourceFrame === '3');
    audio.stop(voice.id);
    this.check('audio.stop', !voice.audioVoice.playing && voice.audioVoice.sourceFrame === '0');
    voice.playAudio();
    this.check('Entity.playAudio', audio.voice(voice.id).playing);
    voice.pauseAudio();
    this.check('Entity.pauseAudio', !audio.voice(voice.id).playing);
    voice.seekAudio('5');
    this.check('Entity.seekAudio', audio.voice(voice.id).sourceFrame === '5');
    voice.stopAudio();
    this.check('Entity.stopAudio', !audio.voice(voice.id).playing);
    audio.setVolume(0.25);
    this.check('audio.setVolume', audio.stats().volume === 0.25);
    this.expectedError(() => {
      audio.setVolume(-1);
    }, 'InvalidArgument');
    this.check('audio.failedControl', audio.stats().volume === 0.25);
    voice.playAudio();
  }

  private publish(): void {
    const report = this.entity('Report');
    const description = report.script;

    if (!description || description.enabled) {
      throw new Error('Report requires a disabled authored script descriptor');
    }

    description.properties = {
      checked: [...this.#checked].sort(),
      starts: this.#starts,
      fixed: this.#fixed,
      updates: this.#updates,
      stops: this.#stops,
      errors: this.#errors,
      input: input.snapshot() as unknown as JsonValue,
      physics: physics.stats() as unknown as JsonValue,
      contacts: physics.contacts as unknown as JsonValue,
      audio: audio.stats() as unknown as JsonValue
    };
    report.script = description;
    scene.rename('Feature Lab | ' + this.#checked.size + ' checks | D moves body');
  }

  onStart(): void {
    ++this.#starts;
    this.check('ScriptComponent.constructor', this.self.exists && Object.isFrozen(this.properties));
    this.check('ScriptComponent.self', this.self.info().script?.className === 'Audit');
    this.check('ScriptComponent.properties', this.properties.contract === 'feature-lab-v1');
    this.check('ScriptComponent.onStart', super.onStart() === undefined);
    this.inputChecks();
    this.sceneChecks();
    this.physicsChecks();
    this.audioChecks();
    this.publish();
  }

  onFixedUpdate(seconds: number): void {
    ++this.#fixed;
    this.check('ScriptComponent.onFixedUpdate', super.onFixedUpdate(seconds) === undefined);
    this.entity('Dynamic').setVelocity({x: input.key('D').down ? 1 : 0, y: 0, z: 0});
  }

  onUpdate(seconds: number): void {
    ++this.#updates;
    this.check('ScriptComponent.onUpdate', super.onUpdate(seconds) === undefined);
    this.inputChecks();
    this.publish();
  }

  onStop(): void {
    ++this.#stops;
    this.check('ScriptComponent.onStop', super.onStop() === undefined);
    this.entity('Voice').stopAudio();
    this.entity('Dynamic').setVelocity({x: 0, y: 0, z: 0});
    this.publish();
  }

  onSaveState():
      {checked: string[]; starts: number; fixed: number; updates: number; errors: number} {
    return {
      checked: [...this.#checked],
      starts: this.#starts,
      fixed: this.#fixed,
      updates: this.#updates,
      errors: this.#errors
    };
  }

  onLoadState(state: {
    checked: string[]; starts: number; fixed: number; updates: number; errors: number
  }): void {
    if (!state || !Array.isArray(state.checked)
        || state.checked.some(key => typeof key !== 'string') ||
        [state.starts, state.fixed, state.updates, state.errors].some(
            value => !Number.isInteger(value) || value < 0)) {
      throw new Error('Invalid feature lab checkpoint');
    }

    this.#checked = new Set(state.checked);
    this.#starts = state.starts;
    this.#fixed = state.fixed;
    this.#updates = state.updates;
    this.#errors = state.errors;
  }
}

/**
 * Disabled metadata-only report; it cannot inflate running lifecycle counts.
 * 禁用的纯元数据报告，不增加运行生命周期计数。
 */
export class AuditReport extends ScriptComponent {}
