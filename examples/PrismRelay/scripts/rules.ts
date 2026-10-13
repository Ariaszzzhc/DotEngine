export type Phase = 'ready'|'playing'|'paused'|'won'|'lost';

export interface Settings {
  speed: number;
  timeLimit: number;
  maxHealth: number;
}

export interface Checkpoint {
  phase: Phase;
  elapsed: number;
  health: number;
  immunity: number;
  collected: string[];
}

/**
 * Pure gameplay state; native components own bodies, rendering and audio.
 * 纯玩法状态，刚体、渲染与音频由原生组件持有。
 */
export class RelayRules {
  readonly settings: Readonly<Settings>;
  readonly targets: readonly string[];
  #state: Checkpoint;

  constructor(settings: Settings, targets: readonly string[]) {
    if (!Number.isFinite(settings.speed) || settings.speed <= 0 || settings.speed > 12
        || !Number.isFinite(settings.timeLimit) || settings.timeLimit <= 0
        || settings.timeLimit > 300 || !Number.isInteger(settings.maxHealth)
        || settings.maxHealth < 1 || settings.maxHealth > 3 || targets.length === 0
        || targets.length > 32 || targets.some(name => name.length === 0)
        || new Set(targets).size !== targets.length) {
      throw new Error('Invalid relay settings or targets');
    }

    this.settings = Object.freeze({...settings});
    this.targets = Object.freeze([...targets]);
    this.#state = this.initial();
  }

  private initial(): Checkpoint {
    return {
      phase: 'ready',
      elapsed: 0,
      health: this.settings.maxHealth,
      immunity: 0,
      collected: []
    };
  }

  get state(): Checkpoint {
    return {...this.#state, collected: [...this.#state.collected]};
  }

  reset(): void {
    this.#state = this.initial();
  }

  start(): void {
    if (this.#state.phase === 'ready') {
      this.#state.phase = 'playing';
    }
  }

  pause(): void {
    if (this.#state.phase === 'playing') {
      this.#state.phase = 'paused';
    } else if (this.#state.phase === 'paused') {
      this.#state.phase = 'playing';
    }
  }

  tick(seconds: number): void {
    if (!Number.isFinite(seconds) || seconds < 0 || seconds > 1) {
      throw new Error('Frame duration must be finite and within [0,1]');
    }

    if (this.#state.phase !== 'playing') {
      return;
    }

    this.#state.elapsed = Math.min(this.settings.timeLimit, this.#state.elapsed + seconds);
    this.#state.immunity = Math.max(0, this.#state.immunity - seconds);

    if (this.#state.elapsed >= this.settings.timeLimit) {
      this.#state.phase = 'lost';
    }
  }

  collect(name: string): boolean {
    if (!this.targets.includes(name)) {
      throw new Error('Unknown relay target: ' + name);
    }

    if (this.#state.phase !== 'playing' || this.#state.collected.includes(name)) {
      return false;
    }

    this.#state.collected.push(name);

    if (this.#state.collected.length === this.targets.length) {
      this.#state.phase = 'won';
    }

    return true;
  }

  hit(): boolean {
    if (this.#state.phase !== 'playing' || this.#state.immunity > 0) {
      return false;
    }

    --this.#state.health;
    this.#state.immunity = 1;

    if (this.#state.health === 0) {
      this.#state.phase = 'lost';
    }

    return true;
  }

  /**
   * Owned bounded checkpoint for hot reload; failed restore leaves state intact.
   * 用于热重载的拥有、有界检查点，恢复失败保留当前状态。
   */
  restore(value: Checkpoint): void {
    const phases: readonly string[] = ['ready', 'playing', 'paused', 'won', 'lost'];

    if (!value || !phases.includes(value.phase) || !Number.isFinite(value.elapsed)
        || value.elapsed < 0 || value.elapsed > this.settings.timeLimit
        || !Number.isInteger(value.health) || value.health < 0
        || value.health > this.settings.maxHealth || !Number.isFinite(value.immunity)
        || value.immunity < 0 || value.immunity > 1 || !Array.isArray(value.collected)
        || value.collected.length > this.targets.length
        || value.collected.some(name => !this.targets.includes(name))
        || new Set(value.collected).size !== value.collected.length) {
      throw new Error('Invalid relay checkpoint');
    }

    const complete = value.collected.length === this.targets.length;
    const expired = value.health === 0 || value.elapsed === this.settings.timeLimit;

    if ((value.phase === 'won' && (!complete || expired))
        || (value.phase === 'lost' && (!expired || complete))
        || ((value.phase === 'playing' || value.phase === 'paused') && (complete || expired))
        || (value.phase === 'ready'
            && (value.elapsed !== 0 || value.immunity !== 0
                || value.health !== this.settings.maxHealth || value.collected.length !== 0))) {
      throw new Error('Inconsistent relay checkpoint');
    }

    this.#state = {...value, collected: [...value.collected]};
  }
}
