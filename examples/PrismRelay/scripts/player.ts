import {Entity, input, physics, scene, ScriptComponent, type Transform} from "dot:engine";
import {type Checkpoint, RelayRules, type Settings} from './rules';

/**
 * Attached player behavior. Native Jolt contacts decide pickups/damage;
 * source hot reload preserves gameplay state and never recreates the scene.
 * 玩家附加行为；拾取与伤害由原生 Jolt 接触决定，源码热重载保留玩法状态，不重建场景。
 */
export class RelayPlayer extends ScriptComponent<Settings> {
  #rules!: RelayRules;
  #entities = new Map<string, Entity>();
  #spawn!: Transform;
  #contactBatch = '';
  #inputFrame = '';
  #touching = new Map<string, string>();

  private entity(name: string): Entity {
    const entity = this.#entities.get(name);

    if (!entity || !entity.exists) {
      throw new Error('Missing authored relay entity: ' + name);
    }

    return entity;
  }

  onStart(): void {
    for (const entity of scene.entities()) {
      const name = entity.name;

      if (this.#entities.has(name)) {
        throw new Error('Duplicate authored relay entity: ' + name);
      }

      this.#entities.set(name, entity);
    }

    this.#spawn = this.self.transform;
    this.#rules = new RelayRules(
        this.properties, Array.from({length: 8}, (_, index) => 'Crystal' + (index + 1)));
    this.refresh();
  }

  onFixedUpdate(): void {
    const active = this.#rules.state.phase === 'playing';
    let x = active ? Number(input.key('D').down || input.key('Right').down)
            - Number(input.key('A').down || input.key('Left').down)
                   : 0;
    let z = active ? Number(input.key('S').down || input.key('Down').down)
            - Number(input.key('W').down || input.key('Up').down)
                   : 0;
    const length = Math.hypot(x, z);

    if (length > 0) {
      x *= this.properties.speed / length;
      z *= this.properties.speed / length;
    }

    this.self.setVelocity({x, y: 0, z});
    this.self.setAngularVelocity({x: 0, y: 0, z: 0});
  }

  onUpdate(seconds: number): void {
    if (this.#inputFrame !== input.index) {
      this.#inputFrame = input.index;

      if (input.key('R').pressed) {
        this.#rules.reset();
        this.#touching.clear();
        this.self.transform = this.#spawn;
      } else if (input.key('Enter').pressed || input.key('Space').pressed) {
        this.#rules.start();
      } else if (input.key('P').pressed) {
        this.#rules.pause();
      }
    }

    this.#rules.tick(seconds);
    const contacts = physics.contacts;
    const batch = contacts.generation + ':' + contacts.index;

    if (batch !== this.#contactBatch) {
      this.#contactBatch = batch;

      for (const contact of contacts.events) {
        if (!contact.sensor) {
          continue;
        }

        const other = contact.first.value === this.self.id.value ? contact.second
            : contact.second.value === this.self.id.value        ? contact.first
                                                                 : null;

        if (!other) {
          continue;
        }

        if (contact.phase === 'end') {
          this.#touching.delete(other.value);
          continue;
        }

        const entity = new Entity(other);

        if (entity.exists) {
          this.#touching.set(other.value, entity.name);
        }
      }
    }

    /**
     * Sleeping bodies retain contact until an explicit native end event.
     * 休眠刚体保持接触，直到原生明确发送结束事件。
     */
    for (const name of this.#touching.values()) {
      if (name.startsWith('Crystal') && this.#rules.collect(name)) {
        this.self.seekAudio('0');
        this.self.playAudio();
      } else if (name.startsWith('Hazard') && this.#rules.hit()) {
        this.entity('DamageSound').seekAudio('0');
        this.entity('DamageSound').playAudio();
      }
    }

    this.refresh();
  }

  private refresh(): void {
    const state = this.#rules.state;

    for (let index = 1; index <= 8; ++index) {
      const visual = this.entity('CrystalVisual' + index);
      const mesh = visual.mesh;
      const score = this.entity('Score' + index);
      const indicator = score.mesh;

      if (!mesh || !indicator) {
        throw new Error('Relay crystal/score requires authored meshes');
      }

      const collected = state.collected.includes('Crystal' + index);
      mesh.visible = !collected;
      indicator.tint = collected ? {x: 0.2, y: 1, z: 0.55, w: 1} : {x: 0.1, y: 0.2, z: 0.25, w: 1};
      visual.mesh = mesh;
      score.mesh = indicator;
    }

    for (let index = 1; index <= 3; ++index) {
      const heart = this.entity('Health' + index);
      const mesh = heart.mesh;

      if (!mesh) {
        throw new Error('Relay health requires an authored mesh');
      }

      mesh.visible = index <= state.health;
      heart.mesh = mesh;
    }

    const timer = this.entity('Timer');
    const transform = timer.transform;
    transform.scale.x = Math.max(0.01, 14 * (1 - state.elapsed / this.properties.timeLimit));
    timer.transform = transform;
    const player = this.self.mesh;

    if (!player) {
      throw new Error('Relay player requires an authored mesh');
    }

    player.tint = state.phase === 'won'                ? {x: 0.2, y: 1, z: 0.55, w: 1}
        : state.phase === 'lost' || state.immunity > 0 ? {x: 1, y: 0.2, z: 0.2, w: 1}
                                                       : {x: 0.3, y: 0.85, z: 1, w: 1};
    this.self.mesh = player;
    const hint = state.phase === 'ready' ? 'Enter to start'
        : state.phase === 'paused'       ? 'P to resume'
        : state.phase === 'won'          ? 'Complete! R to restart'
        : state.phase === 'lost'         ? 'Game over! R to restart'
                                         : 'WASD/arrows | P pause';
    scene.rename(
        'Prism Relay | ' + hint + ' | ' + state.collected.length + '/8 | ' + state.health + ' HP | '
        + Math.ceil(this.properties.timeLimit - state.elapsed) + 's');
  }

  onSaveState(): {
    rules: Checkpoint; spawn: Transform; input: string; contacts: string;
    touching: [string, string][]
  } {
    return {
      rules: this.#rules.state,
      spawn: this.#spawn,
      input: this.#inputFrame,
      contacts: this.#contactBatch,
      touching: [...this.#touching]
    };
  }

  onLoadState(state: {
    rules: Checkpoint; spawn: Transform; input: string; contacts: string;
    touching: [string, string][]
  }): void {
    // Hot-reload candidates read authored entities before state migration; start is not replayed.
    // 热重载候选在迁移状态前读取编辑实体，不重放 start。
    this.#entities = new Map(scene.entities().map(entity => [entity.name, entity]));
    this.#rules = new RelayRules(
        this.properties, Array.from({length: 8}, (_, index) => 'Crystal' + (index + 1)));
    this.#rules.restore(state.rules);
    this.#spawn = state.spawn;
    this.#inputFrame = state.input;
    this.#contactBatch = state.contacts;
    this.#touching = new Map(state.touching);
  }

  onStop(): void {
    this.self.stopAudio();
    this.entity('DamageSound').stopAudio();
  }
}
