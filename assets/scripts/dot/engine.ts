import {invoke} from 'dot:host';

/** Scene-scoped decimal IDs retain all uint64 bits through JavaScript.
 * 场景范围十进制 ID，在 JavaScript 中保留 uint64 全部位。 */
export interface EntityId {
  readonly scene: string;
  readonly value: string;
}

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface Quat extends Vec3 {
  w: number;
}

/** Owned local transform snapshot; writing it validates the complete native TRS.
 * 拥有的局部变换快照，写回会校验完整原生 TRS。 */
export interface Transform {
  position: Vec3;
  rotation: Quat;
  scale: Vec3;
}

export interface EntityInfo {
  readonly id: EntityId;
  name: string;
  parent: EntityId | null;
  transform: Transform;
}

export interface SceneInfo {
  readonly id: string;
  readonly name: string;
  readonly entities: number;
}

export type HostErrorCode = 'InvalidArgument' | 'NotFound' | 'Io' | 'Unavailable' | 'InvalidState' | 'External';

export interface HostError extends Error {
  readonly name: 'DotHostError';
  readonly code: HostErrorCode;
}

/** Native view with owned identity. Destroyed/foreign handles fail explicitly;
 * getters return snapshots, and assignment commits one validated native edit.
 * 持有身份的原生视图；删除/跨场景句柄明确失败，getter 返回快照，赋值提交一次校验修改。 */
export class Entity {
  readonly id: EntityId;

  constructor(id: EntityId) {
    this.id = Object.freeze({...id});
  }

  info(): EntityInfo {
    return invoke('entity.get', [this.id]) as EntityInfo;
  }

  get name(): string {
    return this.info().name;
  }

  set name(value: string) {
    invoke('entity.rename', [this.id, value]);
  }

  get transform(): Transform {
    return invoke('transform.get', [this.id]) as Transform;
  }

  set transform(value: Transform) {
    invoke('transform.set', [this.id, value]);
  }

  get parent(): Entity | null {
    const parent = this.info().parent;
    return parent === null ? null : new Entity(parent);
  }

  set parent(value: Entity | null) {
    invoke('hierarchy.setParent', [this.id, value === null ? null : value.id]);
  }

  /** Right-handed column-major world matrix with 16 float values.
   * 右手、列主序世界矩阵，含 16 个浮点值。 */
  worldMatrix(): readonly number[] {
    return invoke('transform.world', [this.id]) as number[];
  }

  /** Removes this entity and its whole native subtree.
   * 删除本实体及完整原生子树。 */
  destroy(): void {
    invoke('entity.destroy', [this.id]);
  }
}

export const scene = Object.freeze({
  info(): SceneInfo {
    return invoke('scene.info', []) as SceneInfo;
  },

  rename(name: string): void {
    invoke('scene.rename', [name]);
  },

  create(name = 'Entity'): Entity {
    return new Entity(invoke('entity.create', [name]) as EntityId);
  },

  /** Deterministic creation-ID order; wrappers do not own native entities.
   * 按创建 ID 确定性排序，包装器不拥有原生实体。 */
  entities(): Entity[] {
    return (invoke('entity.list', []) as EntityId[]).map(id => new Entity(id));
  },
});
