import {animation, audio, components, Entity, gameClock, gameState, type HostError, hud, ui, input, type JsonValue, type SaveTicket, keys, mouseButtons, physics, saves, scene, scenes, ScriptComponent, type Transform} from 'dot:engine';
import {invoke} from 'dot:host';

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
  #authored = new Map<string, Entity>();
  /**
   * Audio's prepared pose precedes Start callbacks, which may move the camera.
   * 音频准备位姿早于可移动相机的 Start 回调。
   */
  #preparedListenerPosition = this.entity('Camera').transform.position;
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

  private expectedError(action: () => void, code: string): HostError {
    try {
      action();
    } catch (error) {
      const value = error as HostError;
      this.check(
          'HostError',
          value.name === 'DotHostError' && value.code === code && value.message.length > 0);
      ++this.#errors;
      return value;
    }

    throw new Error('Expected native failure: ' + code);
  }

  private entity(name: string): Entity {
    /**
     * Authored probes keep scene-scoped IDs; resolve names once per script realm.
     * 固定编辑探针保留场景范围 ID，每个脚本 realm 只解析一次名称。
     */
    if (this.#authored.size === 0) {
      for (const entity of scene.entities()) {
        this.#authored.set(entity.name, entity);
      }
    }

    const found = this.#authored.get(name);

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
    this.check('input.pointerCaptured.get', input.pointerCaptured === frame.pointerCaptured);
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
    const children = parent.children;
    this.check('Entity.children.get', children.length === 1 && children[0].id.value === view.id.value
        && children[0].parent?.id.value === parent.id.value);
    this.check(
        'Entity.worldMatrix', view.worldMatrix().length === 16 && view.worldMatrix()[12] === 3);
    view.parent = null;
    this.check(
        'scene.entities', scene.entities().some(entity => entity.id.value === view.id.value));
    this.check(
        'scene.findEntities',
        scene.findEntities(view.name).some(entity => entity.id.value === view.id.value));

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

  private characterChecks(): void {
    const actor = this.entity('Character controller');
    const description = components.character();
    this.check('components.character', description.radius === 0.35 && description.mass === 75);
    actor.character = description;
    this.check('Entity.character.set', equal(actor.character, description));
    this.check('Entity.character.get', equal(actor.info().character, description));
    const state = physics.character(actor.id);
    this.check('physics.character', Number.isFinite(state.position.y));
    this.check('Entity.characterState.get', equal(actor.characterState, state));
    state.position.x = 999;
    this.check('character.ownedSnapshot', actor.characterState.position.x !== 999);
    this.check(
        'physics.setCharacterInput',
        physics.setCharacterInput(actor.id, {velocity: {x: 0, y: 0, z: 0}, jump: false})
            === undefined);
    this.check('Entity.moveCharacter', actor.moveCharacter({x: 0, y: 0, z: 0}) === undefined);
    physics.setCharacterYaw(actor.id, 90);
    this.check('physics.setCharacterYaw', actor.characterState.rotation.y > 0.7);
    actor.setCharacterYaw(0);
    this.check('Entity.setCharacterYaw', actor.characterState.rotation.w === 1);
    const feet = actor.characterState.position;
    physics.teleportCharacter(actor.id, feet, 90);
    this.check('physics.teleportCharacter', actor.characterState.rotation.y > 0.7);
    actor.teleportCharacter(feet, 0);
    this.check('Entity.teleportCharacter', actor.characterState.velocity.x === 0);
    const origin = {x: feet.x, y: feet.y + 1, z: feet.z + 3};
    const travel = {x: 0, y: 0, z: -6};
    this.check(
        'character.rayIdentity', physics.rayCast(origin, travel)?.entity.value === actor.id.value);
    this.check(
        'character.sphereIdentity',
        physics.sphereCast(origin, .2, travel)?.entity.value === actor.id.value);
    this.check(
        'character.querySelfFilter',
        physics.sphereCast(origin, .2, travel, {ignoreEntity: actor.id}) === null);
  }

  private characterContactChecks(): void {
    if (this.#checked.has('character.virtualContact')) {
      return;
    }

    const actor = this.entity('Character controller');
    const floor = this.entity('Static');
    const event = physics.contacts.events.find(
        event => event.first.value === floor.id.value && event.second.value === actor.id.value
            && event.geometry?.kind === 'character');

    if (event?.geometry?.kind === 'character') {
      const geometry = event.geometry;
      this.check(
          'character.virtualContact',
          !geometry.characterIsFirst && geometry.normal.y > .99
              && Number.isFinite(geometry.separation) &&
              [geometry.point.x, geometry.point.y, geometry.point.z].every(Number.isFinite));
    }
  }

  private physicsChecks(): void {
    const dynamic = this.entity('Dynamic');
    const kinematic = this.entity('Kinematic');
    const initial = physics.body(dynamic.id);
    this.check(
        'physics.body',
        initial.motion === 'dynamic' && initial.collisionDetection === 'continuous'
            && !initial.sensor);
    this.check('Entity.physicsBody.get', equal(dynamic.physicsBody, initial));
    this.check('physics.stats', physics.stats().bodies === 8 && !physics.stats().faulted);
    this.check('physics.contacts.get', Array.isArray(physics.contacts.events));
    const duct = this.entity('Mesh duct');
    const meshBody = duct.rigidBody!;
    this.check('physics.mesh.authored', meshBody.shape === 'mesh'
        && meshBody.motion === 'static' && meshBody.meshAsset === 'assets/collision-duct.dotmesh');
    duct.rigidBody = {...meshBody, friction: 0.45};
    this.check('physics.mesh.write', Math.abs(duct.rigidBody!.friction - 0.45) < 0.001);
    this.expectedError(() => { duct.rigidBody = {...meshBody, motion: 'dynamic'}; }, 'InvalidArgument');
    this.expectedError(() => { duct.rigidBody = {...meshBody, shape: 'box'}; }, 'InvalidArgument');
    this.check('physics.mesh.failedWrite', duct.rigidBody!.shape === 'mesh');
    this.check('physics.mesh.opening', physics.rayCast({x: 6, y: 1, z: 3}, {x: 0, y: 0, z: -4}) === null);
    this.check('physics.mesh.surface', physics.rayCast({x: 6.87, y: 1, z: 3}, {x: 0, y: 0, z: -4})?.entity.value === duct.id.value);
    this.check('physics.mesh.sweepOpening', physics.sphereCast({x: 6, y: 1, z: 3}, 0.04, {x: 0, y: 0, z: -4}) === null);

    const hit = physics.rayCast({x: -2, y: 3, z: 0}, {x: 0, y: -6, z: 0});
    this.check('physics.rayCast', hit !== null && hit.fraction >= 0 && hit.fraction <= 1);
    const swept = physics.sphereCast({x: 0, y: 3, z: 0}, 0.2, {x: 0, y: -6, z: 0});
    this.check(
        'physics.sphereCast',
        swept?.entity.value === this.entity('Static').id.value && swept.center.y > swept.point.y
            && (swept.normal?.y ?? 0) > 0.9);
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
    this.check('audio.listener', equal(audio.listener().position, this.#preparedListenerPosition));
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
    audio.resetMotion();
    this.check('audio.resetMotion', audio.stats().motionResetPending);
    this.expectedError(() => {
      audio.setVolume(-1);
    }, 'InvalidArgument');
    this.check('audio.failedControl', audio.stats().volume === 0.25);
    voice.playAudio();
  }

  private animationChecks(): void {
    const panel = this.entity('Animated panel');
    const authored = panel.transform;
    const snapshot = animation.state(panel.id);
    this.check('animation.state', snapshot.clip === 0 && snapshot.name === 'Panel lift');
    snapshot.timeSeconds = 10;
    this.check('Entity.animationState.get', panel.animationState.timeSeconds === 0);
    animation.play(panel.id, 0, true);
    this.check('animation.play', panel.animationState.playing);
    animation.pause(panel.id);
    this.check('animation.pause', !panel.animationState.playing);
    animation.seek(panel.id, 1);
    this.check('animation.seek', panel.transform.position.y === 1.5);
    animation.configure(panel.id, -1, false);
    this.check(
        'animation.configure', panel.animationState.speed === -1 && !panel.animationState.looping);
    animation.stop(panel.id);
    this.check('animation.stop', equal(panel.transform, authored) && !panel.animationState.playing);
    panel.playAnimation(1, true);
    this.check(
        'Entity.playAnimation',
        panel.animationState.clip === 1 && panel.animationState.timeSeconds === 2);
    panel.pauseAnimation();
    this.check('Entity.pauseAnimation', !panel.animationState.playing);
    panel.seekAnimation(1);
    this.check(
        'Entity.seekAnimation',
        panel.animationState.timeSeconds === 1 && Math.abs(panel.transform.rotation.y) > 0.3);
    panel.configureAnimation(1, true);
    this.check(
        'Entity.configureAnimation',
        panel.animationState.speed === 1 && panel.animationState.looping);
    panel.stopAnimation();
    this.check(
        'Entity.stopAnimation', equal(panel.transform, authored) && !panel.animationState.playing);
    this.expectedError(() => {
      panel.playAnimation(2);
    }, 'InvalidArgument');
    this.check(
        'animation.failedControl',
        panel.animationState.clip === 1 && !panel.animationState.playing);
    animation.crossFade(panel.id, 0, 0.25);
    this.check(
        'animation.crossFade',
        panel.animationState.transition?.durationSeconds === 0.25
            && equal(panel.transform, authored));
    panel.crossFadeAnimation(1, 0.5);
    this.check(
        'Entity.crossFadeAnimation',
        panel.animationState.clip === 1 && panel.animationState.transition?.elapsedSeconds === 0
            && equal(panel.transform, authored));
    panel.stopAnimation();
    panel.playAnimation(0, true);
  }

  private layerChecks(): void {
    const panel = this.entity('Animated panel');
    panel.pauseAnimation();
    panel.seekAnimation(1);
    const base = panel.transform;
    animation.layer(panel.id, 'PanelTurn', {action: 'configure', speed: 2, looping: true});
    animation.layer(panel.id, 'PanelTurn', {action: 'play', restart: true});
    panel.controlAnimationLayer('PanelTurn', {action: 'seek', seconds: 1});
    const posed = panel.transform;
    const state = panel.animationState.layers[0]!;
    this.check('animation.layer', equal(posed.position, base.position)
        && equal(posed.scale, base.scale) && !equal(posed.rotation, base.rotation));
    this.check('Entity.controlAnimationLayer', state.name === 'PanelTurn' && state.timeSeconds === 1
        && state.weight === .5 && state.speed === 2 && state.active);
    state.weight = 99;
    this.check('animation.layer.snapshot', panel.animationState.layers[0]!.weight === .5);
    panel.controlAnimationLayer('PanelTurn', {action: 'pause'});
    this.check('animation.layer.pause', panel.animationState.layers[0]!.paused);
    panel.controlAnimationLayer('PanelTurn', {action: 'weight', weight: .75});
    this.check('animation.layer.weight', panel.animationState.layers[0]!.weight === .75);
    panel.controlAnimationLayer('PanelTurn', {action: 'fade', weight: 0, seconds: .15, stop: true});
    this.check('animation.layer.fade', panel.animationState.layers[0]!.fade?.durationSeconds === .15);
    panel.controlAnimationLayer('PanelTurn', {action: 'stop'});
    this.check('animation.layer.stop', !panel.animationState.layers[0]!.active && equal(panel.transform, base));
    this.expectedError(() => {
      panel.controlAnimationLayer('PanelTurn', {action: 'weight', weight: 2});
    }, 'InvalidArgument');
    this.check('animation.layer.failedControl', !panel.animationState.layers[0]!.active);
    panel.configureAnimation(1, true);
    panel.playAnimation(0, true);
  }

  private controllerChecks(): void {
    const glass = this.entity('Glass');
    const before = glass.animationState.controller;
    this.check('animation.controller.authored', before?.state === 'Forward');
    animation.parameters(glass.id, {Reverse: true});
    this.check('animation.parameters', glass.animationState.controller?.parameters.Reverse === true);
    glass.setAnimationParameters({Reverse: false});
    this.check('Entity.setAnimationParameters', glass.animationState.controller?.parameters.Reverse === false);
    animation.resetController(glass.id);
    this.check('animation.resetController', glass.animationState.controller?.transitions === '0');
    glass.resetAnimationController();
    this.check('Entity.resetAnimationController', glass.animationState.controller?.elapsedSeconds === 0);
    this.expectedError(() => glass.setAnimationParameters({Reverse: 1}), 'InvalidArgument');
    this.expectedError(() => animation.parameters(glass.id, {Missing: false}), 'InvalidArgument');
    this.expectedError(() => glass.playAnimation(0, true), 'InvalidState');
    this.check('animation.controller.failedControl', glass.animationState.controller?.parameters.Reverse === false);
    glass.setAnimationParameters({Reverse: true});
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
      audio: audio.stats() as unknown as JsonValue,
      animation: this.entity('Animated panel').animationState as unknown as JsonValue
    };
    report.script = description;
    scene.rename(
        'Feature Lab | ' + this.#checked.size
        + ' checks | D move | T reset CCD | 1/2 animation | 3 layer | Space pause | L studio');
  }

  private sceneFlowChecks(): void {
    this.check('scenes.current', scenes.current().scope === this.self.id.scene);
    this.expectedError(() => {
      scenes.request('../invalid.dotscene');
    }, 'InvalidArgument');
    this.check('scenes.request', scenes.current().scope === this.self.id.scene);
    this.expectedError(() => {
      scenes.status('0');
    }, 'InvalidArgument');
    this.check('scenes.status', scenes.current().scope === this.self.id.scene);
    const instances = scenes.instances();
    this.check('scenes.instances', Array.isArray(instances)
        && instances.every(value => value.root.scene === this.self.id.scene));
    const append = this.expectedError(() => scenes.append('../invalid.dotscene'), 'InvalidArgument');
    this.check('scenes.append', append.code === 'InvalidArgument');
    const remove = this.expectedError(
        () => scenes.remove({scene: this.self.id.scene, value: '18446744073709551614'}), 'NotFound');
    this.check('scenes.remove', remove.code === 'NotFound');
  }

  #saveTicket: SaveTicket|null = null;
  #saveStep = 0;
  #saveRevision = '1';

  private gameDataChecks(): void {
    const initial = gameState.snapshot();
    this.check('gameState.snapshot', typeof initial.revision === 'string');
    gameState.set('featureAudit', {visits: 1});
    this.check('gameState.set', typeof gameState.snapshot().revision === 'string');
    this.check('gameState.get', equal(gameState.get('featureAudit'), {visits: 1}));
    gameState.remove('featureAudit');
    this.check('gameState.remove', gameState.get('featureAudit') === undefined);
    gameState.replace(initial.data, gameState.snapshot().revision);
    this.check('gameState.replace', equal(gameState.snapshot().data, initial.data));
    const snapshot = gameState.snapshot();
    snapshot.data.featureAudit = {visits: 1};
    this.#saveTicket = saves.write('feature-lab-audit', true, snapshot);
  }

  private pollSaveAudit(): void {
    const ticket = this.#saveTicket;

    if (ticket === null) {
      return;
    }

    this.check('saves.jobs', saves.jobs().some(job => job.id === ticket.id));
    this.check('saves.status', saves.status(ticket).operation === ticket.operation);
    const receipt = saves.take(ticket);
    this.check('saves.take', receipt.id === ticket.id);

    if (receipt.state === 'pending' || receipt.state === 'running') {
      return;
    }

    if (receipt.state !== 'succeeded') {
      if (this.#saveStep === 4 && receipt.error?.code === 'InvalidState') {
        this.check('saves.load', true);
        this.#saveTicket = saves.remove('feature-lab-audit');
        this.#saveStep = 5;
        return;
      }

      throw Error(receipt.error?.message ?? 'Feature Lab save audit cancelled');
    }

    if (this.#saveStep === 0) {
      const slot = receipt.result as {name: string; bytes: string};
      this.check('saves.write', slot.name === 'feature-lab-audit' && Number(slot.bytes) > 0);
      this.#saveTicket = saves.list();
    } else if (this.#saveStep === 1) {
      this.check('saves.list', (receipt.result as {name: string}[]).some(slot => slot.name === 'feature-lab-audit'));
      this.#saveTicket = saves.read('feature-lab-audit');
    } else if (this.#saveStep === 2) {
      this.check('saves.read', equal((receipt.result as {data: {[key: string]: JsonValue}}).data.featureAudit, {visits: 1}));
      const current = gameState.snapshot();
      this.#saveRevision = current.revision;
      this.#saveTicket = saves.write('feature-lab-audit', true, current);
    } else if (this.#saveStep === 3) {
      try {
        this.#saveTicket = saves.load('feature-lab-audit', this.#saveRevision);
      } catch (error) {
        if ((error as HostError).code !== 'InvalidState') {
          throw error;
        }

        this.check('saves.load', true);
        this.#saveTicket = saves.remove('feature-lab-audit');
        this.#saveStep = 5;
        return;
      }
    } else if (this.#saveStep === 4) {
      this.check('saves.load', typeof (receipt.result as {revision: string}).revision === 'string');
      this.#saveTicket = saves.remove('feature-lab-audit');
    } else if (this.#saveStep === 5) {
      this.check('saves.remove', receipt.result === null);
      this.#saveTicket = saves.list();
    } else {
      this.check('saves.list', !(receipt.result as {name: string}[]).some(slot => slot.name === 'feature-lab-audit'));
      this.expectedError(() => saves.cancel({id: '0', operation: 'list'}), 'InvalidArgument');
      this.check('saves.cancel', true);
      this.#saveTicket = null;
    }

    ++this.#saveStep;
  }

  onStart(): void {
    ++this.#starts;
    this.check('ScriptComponent.constructor', this.self.exists && Object.isFrozen(this.properties));
    this.check('ScriptComponent.self', this.self.info().script?.className === 'Audit');
    this.check('ScriptComponent.properties', this.properties.contract === 'feature-lab-v1');
    this.check('ScriptComponent.onStart', super.onStart() === undefined);
    this.inputChecks();
    const captured = input.pointerCaptured;
    input.capturePointer(true);
    input.capturePointer(false);
    this.check('input.capturePointer', input.pointerCaptured === captured);
    this.sceneFlowChecks();
    this.sceneChecks();
    this.physicsChecks();
    this.audioChecks();
    this.publish();
  }

  private hierarchyChecks(): void {
    const parent = scene.create('Hierarchy example parent');
    const a = scene.create('Hierarchy example A');
    const b = scene.create('Hierarchy example B');
    const pose = parent.transform;
    pose.position.x = 10;
    parent.transform = pose;
    const before = a.worldMatrix();
    a.reparent(parent, 'world');
    this.check('Entity.reparent', equal(a.worldMatrix(), before));
    const group = scene.group([a, b], 'Hierarchy example assembly');
    this.check(
        'scene.group', a.parent?.id.value === group.id.value && equal(a.worldMatrix(), before));
    const children = scene.ungroup([group]);
    this.check(
        'scene.ungroup', children.length === 2 && !group.exists && equal(a.worldMatrix(), before));
    a.destroy();
    b.destroy();
    parent.destroy();
  }

  onFixedUpdate(seconds: number): void {
    ++this.#fixed;

    if (!this.#checked.has('scene.group')) {
      this.hierarchyChecks();
    }

    this.check('ScriptComponent.onFixedUpdate', super.onFixedUpdate(seconds) === undefined);
    this.entity('Dynamic').setVelocity({x: input.key('D').down ? 1 : 0, y: 0, z: 0});
  }

  #uiSequence = '0';
  #buttonActions = 0;

  onFrame(seconds: number): void {
    this.pollSaveAudit();
    this.check('ScriptComponent.onFrame', super.onFrame(seconds) === undefined);

    if (!this.#checked.has('input.requestTextInput')) {
      const before = input.textInputRequested;
      input.requestTextInput(!before);
      this.check('input.textInputRequested.get', input.textInputRequested === !before);
      input.requestTextInput(before);
      this.check('input.requestTextInput', input.textInputRequested === before);
      this.check('input.textInputActive.get', input.textInputActive === input.snapshot().textInputActive);
    }

    const actions = hud.events(this.#uiSequence);

    if (actions.lost) {
      throw Error('Feature Lab button history expired');
    }

    for (const action of actions.events) {
      this.#uiSequence = action.sequence;

      if (action.key === 'feature-button') {
        ++this.#buttonActions;
        gameState.set('hudButtonCount', this.#buttonActions);
      } else if (action.key === 'ui-action') {
        const previous = gameState.get('uiClickCount');

        if (previous !== undefined && (typeof previous !== 'number' || !Number.isSafeInteger(previous) || previous < 0)) {
          throw Error('Invalid native UI click count');
        }

        const count = (previous === undefined ? 0 : previous as number) + 1;
        gameState.set('uiClickCount', count);
        ui.text('ui-caption', 'Native clicks: ' + count);
      }
    }

    if (!this.#checked.has('gameClock.setPaused')) {
      const before = gameClock.snapshot();
      this.check('gameClock.snapshot', typeof before.paused === 'boolean');
      const revision = gameClock.setPaused(before.paused);
      this.check('gameClock.setPaused', revision === before.revision);
    }

    if (!this.#checked.has('gameClock.step') && !gameClock.snapshot().paused) {
      this.expectedError(() => { gameClock.step(); }, 'InvalidState');
      this.check('gameClock.step', gameClock.snapshot().step === null);
    }
  }

  private uiChecks(): void {
    const initial = ui.snapshot();
    this.check('ui.snapshot', initial !== null && initial.asset === 'assets/interface.dotui');
    ui.text('ui-caption', 'Native UI ready');
    this.check('ui.text', ui.snapshot()!.nodes.find(node => node.key === 'ui-caption')!.box.text === 'Native UI ready');
    ui.focus('ui-action');
    this.check('ui.focus', hud.snapshot().focused === 'ui-action');
    ui.visible('ui-panel', false);
    this.check('ui.visible', !hud.snapshot().focused && !ui.snapshot()!.nodes.find(node => node.key === 'ui-action')!.box.visible);
    ui.enabled('ui-panel', false);
    this.check('ui.enabled', !ui.snapshot()!.nodes.find(node => node.key === 'ui-action')!.enabled);
    ui.update([{key: 'ui-panel', visible: true, enabled: true}, {key: 'ui-caption', text: 'UI resource / 原生界面'}]);
    this.check('ui.update', ui.snapshot()!.nodes.every(node => node.enabled && node.box.visible));
    const events = ui.events(this.#uiSequence);
    this.check('ui.events', !events.lost && events.sequence === hud.snapshot().sequence);
    const before = ui.snapshot();
    this.expectedError(() => { ui.update([{key: 'ui-panel', visible: false}, {key: 'missing', text: 'bad'}]); }, 'InvalidArgument');
    this.check('ui.failedUpdate', JSON.stringify(before) === JSON.stringify(ui.snapshot()));
    this.expectedError(() => { hud.remove('ui-panel'); }, 'InvalidArgument');
    this.expectedError(() => { ui.text('ui-panel', 'Panel text'); }, 'InvalidArgument');
    ui.reset();
    this.check('ui.reset', ui.snapshot()!.nodes.find(node => node.key === 'ui-caption')!.box.text === 'UI resource / 原生界面' && ui.snapshot()!.nodes.every(node => node.enabled && node.box.visible));
    ui.focus(null);
  }

  onUpdate(seconds: number): void {
    ++this.#updates;

    if (!this.#checked.has('hud.clear')) {
      const label = hud.label({text: 'FEATURE LAB | native text | 原生文字'});
      this.check('hud.label', label.position.x === .02);
      const revision = hud.set('audit', label);
      this.check('hud.set', revision === hud.snapshot().revision);
      this.check('hud.snapshot', hud.snapshot().labels.find(layer => layer.key === 'audit')?.value.text === label.text);
      hud.remove('audit');
      this.check('hud.remove', !hud.snapshot().labels.some(layer => layer.key === 'audit'));
      hud.clear();
      this.check('hud.clear', hud.snapshot().labels.every(layer => ui.snapshot()!.nodes.some(node => node.key === layer.key)));
      hud.set('controls', label);
      const button = hud.button({text: 'Click / 点击', position: {x: .72, y: .02}, size: {x: .26, y: .1}});
      this.check('hud.button', button.enabled && button.visible);
      const sequence = hud.setButton('feature-button', button);
      this.check('hud.setButton', sequence === hud.snapshot().revision);
      hud.focus('feature-button');
      this.check('hud.focus', hud.snapshot().focused === 'feature-button');
      const activated = hud.activate('feature-button');
      this.check('hud.activate', activated === hud.snapshot().sequence);
      const events = hud.events('0');
      this.check('hud.events', !events.lost && events.events.at(-1)?.key === 'feature-button');
      this.#uiSequence = events.sequence;
      this.check('hud.inputState', typeof hud.inputState().pointer === 'boolean');
      hud.focus(null);
    }
    this.check('ScriptComponent.onUpdate', super.onUpdate(seconds) === undefined);

    if (!this.#checked.has('animation.failedControl')) {
      this.animationChecks();
    } else if (!this.#checked.has('animation.layer.failedControl')) {
      this.layerChecks();
    } else if (!this.#checked.has('Entity.setCharacterYaw')) {
      this.characterChecks();
    } else if (!this.#checked.has('animation.controller.failedControl')) {
      this.controllerChecks();
    } else if (!this.#checked.has('ui.reset')) {
      this.uiChecks();
    }

    if (this.#checked.has('Entity.setCharacterYaw') && !this.#checked.has('gameState.snapshot')) {
      this.gameDataChecks();
    }

    this.characterContactChecks();
    this.inputChecks();
    const panel = this.entity('Animated panel');

    if (input.key('Digit1').pressed || input.key('Digit2').pressed) {
      panel.crossFadeAnimation(input.key('Digit2').pressed ? 1 : 0, 0.25);
    }

    if (input.key('Digit3').pressed) {
      const layer = panel.animationState.layers[0]!;

      if (layer.active) {
        panel.controlAnimationLayer('PanelTurn', {action: 'fade', weight: 0, seconds: .15, stop: true});
      } else {
        panel.controlAnimationLayer('PanelTurn', {action: 'weight', weight: 0});
        panel.controlAnimationLayer('PanelTurn', {action: 'play', restart: true});
        panel.controlAnimationLayer('PanelTurn', {action: 'fade', weight: .5, seconds: .15, stop: false});
      }
    }

    if (input.key('Space').pressed) {
      if (panel.animationState.playing) {
        panel.pauseAnimation();
      } else {
        panel.playAnimation();
      }
    }

    this.publish();
  }

  onStop(): void {
    ++this.#stops;
    this.check('ScriptComponent.onStop', super.onStop() === undefined);
    this.entity('Voice').stopAudio();
    this.entity('Animated panel').stopAnimation();
    this.entity('Dynamic').setVelocity({x: 0, y: 0, z: 0});
    this.publish();
  }

  onSaveState():
      {checked: string[]; starts: number; fixed: number; updates: number; errors: number; uiSequence: string; buttonActions: number; saveTicket: SaveTicket|null; saveStep: number; saveRevision: string} {
    return {
      checked: [...this.#checked],
      starts: this.#starts,
      fixed: this.#fixed,
      updates: this.#updates,
      errors: this.#errors,
      uiSequence: this.#uiSequence,
      buttonActions: this.#buttonActions,
      saveTicket: this.#saveTicket,
      saveStep: this.#saveStep,
      saveRevision: this.#saveRevision
    };
  }

  onLoadState(state: {
    checked: string[]; starts: number; fixed: number; updates: number; errors: number; uiSequence: string; buttonActions: number; saveTicket: SaveTicket|null; saveStep: number; saveRevision: string
  }): void {
    if (!state || !Array.isArray(state.checked)
        || state.checked.some(key => typeof key !== 'string') ||
        [state.starts, state.fixed, state.updates, state.errors, state.buttonActions].some(
            value => !Number.isInteger(value) || value < 0) || typeof state.uiSequence !== 'string'
        || !/^(0|[1-9][0-9]*)$/.test(state.uiSequence)) {
      throw new Error('Invalid feature lab checkpoint');
    }

    if (!Number.isInteger(state.saveStep) || state.saveStep < 0 || state.saveStep > 7
        || typeof state.saveRevision !== 'string' || !/^[1-9][0-9]*$/.test(state.saveRevision)) {
      throw Error('Invalid save audit state');
    }

    if (state.saveTicket !== null) {
      saves.status(state.saveTicket);
    }

    this.#saveTicket = state.saveTicket;
    this.#saveStep = state.saveStep;
    this.#saveRevision = state.saveRevision;
    hud.events(state.uiSequence);
    this.#uiSequence = state.uiSequence;
    this.#buttonActions = state.buttonActions;
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
