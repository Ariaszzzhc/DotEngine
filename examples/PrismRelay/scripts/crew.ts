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
  points: Vec3[];
  corner: number;
  source: string;
  failure: string|null;
}

const stopped: Vec3 = {x: 0, y: 0, z: 0};

/**
 * Authored station intent uses native capsule collision and measured native velocity for gait.
 * Native navigation supplies corners around static obstacles; native physics decides movement.
 * Blocked or unavailable routes cannot report arrival. No script collision/pathfinding solver.
 * 原生导航提供绕开静态障碍的拐点，原生物理决定实际移动；受阻或不可用路线不能宣告到达，
 * 脚本不实现碰撞或寻路求解器。
 */
export class CrewPatrol extends ScriptComponent<Settings> {
  #model!: Entity;
  #target: 0|1 = 1;
  #remaining = 0;
  #visits = 0;
  #points: Vec3[] = [];
  #corner = 0;
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
    this.#corner = 0;
    this.#points = [];
    this.#failure = null;

    try {
      const route = navigation.route(this.self.id, target);

      if (Math.hypot(route.goal.x - target.x, route.goal.z - target.z) > .1
          || Math.abs(route.goal.y - target.y) > .35) {
        throw Error('Station is outside the baked walkable surface');
      }

      this.#points = route.points.map(point => ({...point}));
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
    const state = this.self.characterState;
    const distance = Math.hypot(target.x - state.position.x, target.z - state.position.z);

    if (distance <= .12 && Math.abs(target.y - state.position.y) <= .35
        && state.ground === 'grounded') {
      this.self.moveCharacter(stopped);
      this.#remaining = this.properties.workSeconds;
      this.#visits++;
      this.#points = [];
      this.#corner = 0;
      this.#model.controlAnimationLayer('Inspection', {action: 'weight', weight: 0});
      this.#model.controlAnimationLayer('Inspection', {action: 'play', restart: true});
      this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 1, seconds: .12, stop: false});
      this.report();
      return;
    }

    while (this.#corner < this.#points.length) {
      const point = this.#points[this.#corner];

      if (Math.hypot(point.x - state.position.x, point.z - state.position.z) > .06
          || Math.abs(point.y - state.position.y) > .35) {
        break;
      }

      ++this.#corner;
    }

    const point = this.#points[this.#corner];

    if (!point) {
      this.self.moveCharacter(stopped);
      this.#failure = 'Native route ended outside station arrival tolerance';
      this.report();
      return;
    }

    const x = point.x - state.position.x;
    const z = point.z - state.position.z;
    const cornerDistance = Math.hypot(x, z);
    const speed = cornerDistance > 0
        ? Math.min(this.properties.speed, cornerDistance / Math.max(seconds, 1e-6)) / cornerDistance : 0;
    this.self.moveCharacter({x: x * speed, y: 0, z: z * speed});

    if (cornerDistance > .01) {
      this.self.setCharacterYaw(Math.atan2(-x, -z) * 180 / Math.PI);
    }
  }

  onUpdate(): void {
    const state = this.self.characterState;
    this.#model.setAnimationParameters({
      Speed: Math.hypot(state.velocity.x, state.velocity.z),
      Grounded: state.ground === 'grounded',
      Vertical: state.velocity.y,
    });
  }

  onSaveState(): PatrolState {
    return {target: this.#target, remaining: this.#remaining, visits: this.#visits,
      points: this.#points.map(point => ({...point})), corner: this.#corner,
      source: this.#source, failure: this.#failure};
  }

  onLoadState(state: PatrolState): void {
    this.bind();

    if (!state || (state.target !== 0 && state.target !== 1)
        || !Number.isFinite(state.remaining) || state.remaining < 0
        || state.remaining > this.properties.workSeconds || !Number.isSafeInteger(state.visits)
        || state.visits < 0 || state.source !== this.#source || !Array.isArray(state.points)
        || state.points.length > 256 || !Number.isSafeInteger(state.corner) || state.corner < 0
        || state.corner > state.points.length || (state.failure !== null
            && (typeof state.failure !== 'string' || !state.failure.length || state.failure.length > 1024))
        || state.points.some(point => !point || ![point.x, point.y, point.z].every(
            value => Number.isFinite(value) && Math.abs(value) <= 1000000))
        || (state.remaining === 0 && state.failure === null && state.corner === state.points.length)) {
      throw Error('Invalid maintenance crew reload state');
    }

    this.#target = state.target;
    this.#remaining = state.remaining;
    this.#visits = state.visits;
    this.#points = state.points.map(point => ({...point}));
    this.#corner = state.corner;
    this.#failure = state.failure;
  }

  onStop(): void {
    this.self.moveCharacter(stopped);
    this.#model.pauseAnimation();
    gameState.remove('crew:' + this.self.id.value);
  }
}
