import {Entity, gameState, navigation, ScriptComponent, type Vec3} from 'dot:engine';

interface Settings {
  stationA: Vec3;
  stationB: Vec3;
  speed: number;
  workSeconds: number;
}

interface PatrolState {
  target: 0|1;
  remaining: number;
  visits: number;
  source: string;
  failure: string|null;
}

const stopped: Vec3 = {x: 0, y: 0, z: 0};

/**
 * Station intent delegates path following and local avoidance to the native service.
 * Actual grounded physics feet decide arrival; animation reads measured native velocity.
 * 站点意图委托原生服务跟随路径并局部避让；实际物理着地脚底决定到达，动画读取实际速度。
 */
export class CrewPatrol extends ScriptComponent<Settings> {
  #model!: Entity;
  #target: 0|1 = 1;
  #remaining = 0;
  #visits = 0;
  #source = '';
  #failure: string|null = null;

  private bind(): void {
    const points = [this.properties.stationA, this.properties.stationB];
    const models = this.self.children;
    const model = models.length === 1 ? models[0] : undefined;

    if (this.self.parent !== null || !this.self.character || !model
        || !model.animationState.controller
        || !model.animationState.layers.some(layer => layer.name === 'Inspection')
        || points.some(point => !point || ![point.x, point.y, point.z].every(
            value => Number.isFinite(value) && Math.abs(value) <= 1000000))
        || Math.hypot(points[0].x - points[1].x, points[0].z - points[1].z) < .5
        || Math.hypot(points[0].x - points[1].x, points[0].z - points[1].z) > 12
        || !Number.isFinite(this.properties.speed) || this.properties.speed < .25
        || this.properties.speed > 3.2 || !Number.isFinite(this.properties.workSeconds)
        || this.properties.workSeconds < .5 || this.properties.workSeconds > 8) {
      throw Error('Maintenance crew requires its own capsule, animated child and valid stations');
    }

    const surface = navigation.stats();

    if (!surface) {
      throw Error('Maintenance crew requires baked static navigation');
    }

    this.#source = surface.source;
    this.#model = model;
  }

  private report(): void {
    gameState.set('crew:' + this.self.id.value, {
      phase: this.#failure ? 'unavailable' : this.#remaining > 0 ? 'working' : 'walking',
      target: this.#target,
      visits: this.#visits,
      routeFailure: this.#failure,
    });
  }

  private plan(): void {
    const target = this.#target === 0 ? this.properties.stationA : this.properties.stationB;
    this.#failure = null;

    try {
      navigation.move(this.self.id, target, this.properties.speed);
    } catch (error) {
      this.#failure = String(error).slice(0, 1024);
    }
  }

  onStart(): void {
    this.bind();
    this.#model.resetAnimationController();
    this.plan();
    this.report();
  }

  onFixedUpdate(seconds: number): void {
    if (this.#failure) {
      this.self.moveCharacter(stopped);
      return;
    }

    if (this.#remaining > 0) {
      this.self.moveCharacter(stopped);
      this.#remaining = Math.max(0, this.#remaining - seconds);

      if (this.#remaining === 0) {
        this.#target = this.#target === 0 ? 1 : 0;
        this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 0, seconds: .15, stop: true});
        this.plan();
        this.report();
      }

      return;
    }

    const target = this.#target === 0 ? this.properties.stationA : this.properties.stationB;
    const state = navigation.state(this.self.id);

    if (!state || state.phase === 'failed'
        || state.goal.x !== Math.fround(target.x) || state.goal.y !== Math.fround(target.y)
        || state.goal.z !== Math.fround(target.z)) {
      navigation.stop(this.self.id);
      this.#failure = state?.error?.message ?? 'Maintenance station movement was replaced';
      this.report();
      return;
    }

    if (state.phase === 'arrived') {
      navigation.stop(this.self.id);
      this.#remaining = this.properties.workSeconds;
      this.#visits++;
      this.#model.controlAnimationLayer('Inspection', {action: 'weight', weight: 0});
      this.#model.controlAnimationLayer('Inspection', {action: 'play', restart: true});
      this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 1, seconds: .12, stop: false});
      this.report();
      return;
    }
  }

  onUpdate(): void {
    const state = this.self.characterState;
    this.#model.setAnimationParameters({
      Speed: Math.hypot(state.measuredVelocity.x, state.measuredVelocity.z),
      Grounded: state.ground === 'grounded',
      Vertical: state.velocity.y,
    });
  }

  onSaveState(): PatrolState {
    return {target: this.#target, remaining: this.#remaining, visits: this.#visits,
      source: this.#source, failure: this.#failure};
  }

  onLoadState(state: PatrolState): void {
    this.bind();

    if (!state || (state.target !== 0 && state.target !== 1)
        || !Number.isFinite(state.remaining) || state.remaining < 0
        || state.remaining > this.properties.workSeconds || !Number.isSafeInteger(state.visits)
        || state.visits < 0 || state.source !== this.#source || (state.failure !== null
            && (typeof state.failure !== 'string' || !state.failure.length || state.failure.length > 1024))) {
      throw Error('Invalid maintenance crew reload state');
    }

    this.#target = state.target;
    this.#remaining = state.remaining;
    this.#visits = state.visits;
    this.#failure = state.failure;
  }

  onStop(): void {
    navigation.stop(this.self.id);
    this.self.moveCharacter(stopped);
    this.#model.pauseAnimation();
    gameState.remove('crew:' + this.self.id.value);
  }
}
