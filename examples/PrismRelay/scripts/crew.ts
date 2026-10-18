import {Entity, gameState, ScriptComponent, type Vec3} from 'dot:engine';

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
}

const stopped: Vec3 = {x: 0, y: 0, z: 0};

/**
 * Authored station intent uses native capsule collision and measured native velocity for gait.
 * A blocked route cannot report arrival; this is a two-station duty, not navigation/pathfinding.
 * 制作的站点意图使用原生胶囊碰撞及实际原生速度驱动步态；路径受阻不能宣告到达。
 * 这是两站巡回工作，不实现导航或寻路。
 */
export class CrewPatrol extends ScriptComponent<Settings> {
  #model!: Entity;
  #target: 0|1 = 1;
  #remaining = 0;
  #visits = 0;

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

    this.#model = model;
  }

  private report(): void {
    gameState.set('crew:' + this.self.id.value, {
      phase: this.#remaining > 0 ? 'working' : 'walking',
      target: this.#target,
      visits: this.#visits,
    });
  }

  onStart(): void {
    this.bind();
    this.#model.resetAnimationController();
    this.report();
  }

  onFixedUpdate(seconds: number): void {
    if (this.#remaining > 0) {
      this.self.moveCharacter(stopped);
      this.#remaining = Math.max(0, this.#remaining - seconds);

      if (this.#remaining === 0) {
        this.#target = this.#target === 0 ? 1 : 0;
        this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 0, seconds: .15, stop: true});
        this.report();
      }

      return;
    }

    const target = this.#target === 0 ? this.properties.stationA : this.properties.stationB;
    const state = this.self.characterState;
    const x = target.x - state.position.x;
    const z = target.z - state.position.z;
    const distance = Math.hypot(x, z);

    if (distance <= .12 && Math.abs(target.y - state.position.y) <= .35
        && state.ground === 'grounded') {
      this.self.moveCharacter(stopped);
      this.#remaining = this.properties.workSeconds;
      this.#visits++;
      this.#model.controlAnimationLayer('Inspection', {action: 'weight', weight: 0});
      this.#model.controlAnimationLayer('Inspection', {action: 'play', restart: true});
      this.#model.controlAnimationLayer('Inspection', {action: 'fade', weight: 1, seconds: .12, stop: false});
      this.report();
      return;
    }

    const speed = distance > 0 ? Math.min(this.properties.speed, distance / Math.max(seconds, 1e-6)) / distance : 0;
    this.self.moveCharacter({x: x * speed, y: 0, z: z * speed});

    if (distance > .12) {
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
    return {target: this.#target, remaining: this.#remaining, visits: this.#visits};
  }

  onLoadState(state: PatrolState): void {
    this.bind();

    if (!state || (state.target !== 0 && state.target !== 1)
        || !Number.isFinite(state.remaining) || state.remaining < 0
        || state.remaining > this.properties.workSeconds || !Number.isSafeInteger(state.visits)
        || state.visits < 0) {
      throw Error('Invalid maintenance crew reload state');
    }

    this.#target = state.target;
    this.#remaining = state.remaining;
    this.#visits = state.visits;
  }

  onStop(): void {
    this.self.moveCharacter(stopped);
    this.#model.pauseAnimation();
    gameState.remove('crew:' + this.self.id.value);
  }
}
