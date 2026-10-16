import {invoke} from 'dot:host';

/**
 * Scene-scoped decimal IDs retain all uint64 bits through JavaScript.
 * 场景范围十进制 ID，在 JavaScript 中保留 uint64 全部位。
 */
export interface EntityId {
  readonly scene: string;
  readonly value: string;
}

export interface Vec2 {
  x: number;
  y: number;
}

/**
 * Physical native key names; unsupported names fail instead of reading false.
 * 原生物理按键名称，不支持的名称明确失败而非返回 false。
 */
export const keys = Object.freeze([
  'A',
  'B',
  'C',
  'D',
  'E',
  'F',
  'G',
  'H',
  'I',
  'J',
  'K',
  'L',
  'M',
  'N',
  'O',
  'P',
  'Q',
  'R',
  'S',
  'T',
  'U',
  'V',
  'W',
  'X',
  'Y',
  'Z',
  'Digit1',
  'Digit2',
  'Digit3',
  'Digit4',
  'Digit5',
  'Digit6',
  'Digit7',
  'Digit8',
  'Digit9',
  'Digit0',
  'Enter',
  'Escape',
  'Backspace',
  'Tab',
  'Space',
  'Left',
  'Right',
  'Up',
  'Down',
  'F1',
  'F2',
  'F3',
  'F4',
  'F5',
  'F6',
  'F7',
  'F8',
  'F9',
  'F10',
  'F11',
  'F12',
  'LeftControl',
  'RightControl',
  'LeftShift',
  'RightShift',
  'LeftAlt',
  'RightAlt',
  'LeftSuper',
  'RightSuper',
  'Delete',
  'Insert',
  'Home',
  'End',
  'PageUp',
  'PageDown',
  'KeypadEnter'
] as const);

export type Key = typeof keys[number];
export const mouseButtons = Object.freeze(['Left', 'Middle', 'Right', 'Back', 'Forward'] as const);
export type MouseButton = typeof mouseButtons[number];

export interface ButtonState {
  down: boolean;
  pressed: boolean;
  released: boolean;
}

export interface InputFrame {
  index: string;
  keys: Record<Key, ButtonState>;
  buttons: Record<MouseButton, ButtonState>;
  pointer: Vec2;
  pointerDelta: Vec2;
  wheel: Vec2;
  text: string;
  pointerCaptured: boolean;
}

/**
 * Observational reads of the last published frame. Returned data is owned;
 * reads do not consume edges. Fixed steps may share a frame; missing/unpublished
 * native services fail explicitly. capturePointer requests native capture; only
 * pointerCaptured acknowledges it after focus/frontend policy is applied.
 * The application owns frame publication.
 * 只读最近发布帧，返回拥有数据且不消费边沿；固定步可共用帧，未绑定/未发布服务明确
 * 失败，应用负责帧发布；capturePointer 仅请求捕获，pointerCaptured 才报告前端聚焦策略后的确认。
 */
export const input = Object.freeze({
  snapshot(): InputFrame {
    return invoke('input.snapshot', []) as InputFrame;
  },

  key(name: Key): ButtonState {
    return invoke('input.key', [name]) as ButtonState;
  },

  button(name: MouseButton): ButtonState {
    return invoke('input.button', [name]) as ButtonState;
  },

  get pointer(): Vec2 {
    return invoke('input.pointer', []) as Vec2;
  },

  get pointerDelta(): Vec2 {
    return invoke('input.pointerDelta', []) as Vec2;
  },

  get wheel(): Vec2 {
    return invoke('input.wheel', []) as Vec2;
  },

  get text(): string {
    return invoke('input.text', []) as string;
  },

  get index(): string {
    return invoke('input.index', []) as string;
  },

  capturePointer(enabled: boolean): void {
    invoke('input.capturePointer', [enabled]);
  },

  get pointerCaptured(): boolean {
    return invoke('input.pointerCaptured', []) as boolean;
  },
});

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface Vec4 extends Vec3 {
  w: number;
}

export interface Quat extends Vec4 {}

/**
 * Owned local transform snapshot; writing it validates the complete native TRS.
 * 拥有的局部变换快照，写回会校验完整原生 TRS。
 */
export interface Transform {
  position: Vec3;
  rotation: Quat;
  scale: Vec3;
}

/**
 * Authoring descriptions are complete owned snapshots. Assignment validates
 * native ranges and uniqueness; null removes a component. Assets are relative
 * project paths; configuring them does not load files or start a backend.
 * 编辑描述为完整拥有的快照；赋值校验原生范围与唯一性，null 移除组件。资源为项目
 * 相对路径；配置不加载文件、不启动后端。
 */
export interface Camera {
  projection: 'perspective'|'orthographic';
  verticalFovDegrees: number;
  orthographicHeight: number;
  nearPlane: number;
  farPlane: number;
  primary: boolean;
}

export interface MeshRenderer {
  kind: 'cube'|'quad'|'asset';
  asset: string;
  tint: Vec4;
  visible: boolean;
}

export interface RigidBody {
  motion: 'static'|'kinematic'|'dynamic';
  collisionDetection: 'discrete'|'continuous';
  shape: 'box'|'sphere'|'capsule';
  halfExtents: Vec3;
  radius: number;
  halfHeight: number;
  velocity: Vec3;
  mass: number;
  friction: number;
  restitution: number;
  gravityFactor: number;
  sensor: boolean;
}

/**
 * Feet-origin Y-up capsule authoring; mutually exclusive with RigidBody.
 * 脚底原点、Y 向上胶囊编辑描述，与刚体互斥。
 */
export interface CharacterController {
  radius: number;
  halfHeight: number;
  maxSlopeDegrees: number;
  stepHeight: number;
  stickDistance: number;
  mass: number;
  maxStrength: number;
  jumpSpeed: number;
  maxContacts: number;
}

export interface VoiceSettings {
  volume: number;
  pan: number;
  pitch: number;
  looping: boolean;
}

export interface SpatialProperties {
  enabled: boolean;
  attenuation: 'inverse'|'linear'|'exponential';
  minDistance: number;
  maxDistance: number;
  rolloff: number;
  dopplerFactor: number;
  innerConeDegrees: number;
  outerConeDegrees: number;
  outerConeGain: number;
}

export interface AudioSource {
  asset: string;
  playOnStart: boolean;
  voice: VoiceSettings;
  spatial: SpatialProperties;
}

export interface AudioListener {
  primary: boolean;
}

export type JsonValue = null|boolean|number|string|JsonValue[]|{[key: string]: JsonValue};

export interface ScriptDescription {
  source: string;
  className: string;
  properties: {[key: string]: JsonValue};
  enabled: boolean;
}

/**
 * Fresh native-default descriptions; default nested objects are independent. Overrides
 * replace whole fields (including vectors), and validation happens on assignment.
 * 新建原生默认描述，默认嵌套对象相互独立；覆盖替换完整字段（含向量），赋值时校验。
 */
export const components = Object.freeze({
  camera(overrides: Partial<Camera> = {}): Camera {
    return {
      projection: 'perspective',
      verticalFovDegrees: 60,
      orthographicHeight: 10,
      nearPlane: 0.1,
      farPlane: 1000,
      primary: false,
      ...overrides
    };
  },

  mesh(overrides: Partial<MeshRenderer> = {}): MeshRenderer {
    return {kind: 'cube', asset: '', tint: {x: 1, y: 1, z: 1, w: 1}, visible: true, ...overrides};
  },

  rigidBody(overrides: Partial<RigidBody> = {}): RigidBody {
    return {
      motion: 'dynamic',
      collisionDetection: 'discrete',
      shape: 'sphere',
      halfExtents: {x: 0.5, y: 0.5, z: 0.5},
      radius: 0.5,
      halfHeight: 0.5,
      velocity: {x: 0, y: 0, z: 0},
      mass: 1,
      friction: 0.5,
      restitution: 0,
      gravityFactor: 1,
      sensor: false,
      ...overrides
    };
  },

  audioSource(asset: string): AudioSource {
    return {
      asset,
      playOnStart: true,
      voice: {volume: 1, pan: 0, pitch: 1, looping: false},
      spatial: {
        enabled: false,
        attenuation: 'inverse',
        minDistance: 1,
        maxDistance: 1000,
        rolloff: 1,
        dopplerFactor: 0,
        innerConeDegrees: 360,
        outerConeDegrees: 360,
        outerConeGain: 0
      }
    };
  },

  audioListener(primary = false): AudioListener {
    return {primary};
  },

  character(overrides: Partial<CharacterController> = {}): CharacterController {
    return {
      radius: 0.35,
      halfHeight: 0.55,
      maxSlopeDegrees: 50,
      stepHeight: 0.35,
      stickDistance: 0.3,
      mass: 75,
      maxStrength: 100,
      jumpSpeed: 6,
      maxContacts: 256,
      ...overrides
    };
  },

  script(source: string, className = 'default'): ScriptDescription {
    return {source, className, properties: {}, enabled: true};
  },
});

/**
 * World-space runtime state, separate from the authored rigid-body description.
 * 世界坐标运行状态，与编辑刚体描述分离。
 */
export interface PhysicsBodyState {
  motion: 'static'|'kinematic'|'dynamic';
  collisionDetection: 'discrete'|'continuous';
  position: Vec3;
  rotation: Quat;
  velocity: Vec3;
  angularVelocity: Vec3;
  active: boolean;
  sensor: boolean;
}

export interface PhysicsStats {
  bodies: number;
  characters: number;
  active: number;
  faulted: boolean;
  contactPairs: number;
  queuedContactEvents: number;
  generation: string;
}

/**
 * World horizontal velocity persists; jump is consumed on the next native
 * substep, even in air. 世界水平速度持续，跳跃在下个原生子步消耗（含空中）。
 */
export interface CharacterInput {
  velocity: Vec3;
  jump: boolean;
}

/**
 * Owned world-space snapshot; support probing contributes to vertical velocity.
 * 世界空间拥有快照，竖直速度包含支撑探测；不修改返回值以控制仿真。
 */
export interface CharacterState {
  position: Vec3;
  rotation: Quat;
  velocity: Vec3;
  ground: 'grounded'|'steep'|'unsupported'|'airborne';
  groundNormal: Vec3;
  groundVelocity: Vec3;
  groundEntity: EntityId|null;
}

export interface RayHit {
  entity: EntityId;
  fraction: number;
  point: Vec3;
  normal: Vec3|null;
}

/**
 * Sensors are excluded by default; ignoreEntity must name a synchronized rigid body or character.
 * 默认排除传感器，ignoreEntity 须指向已同步刚体或角色。
 */
export interface SphereCastFilter {
  includeSensors?: boolean;
  ignoreEntity?: EntityId|null;
}

/**
 * Center is the swept sphere center, point is on the obstacle. Fraction zero
 * reports initial overlap without moving either participant. Normal may be null
 * for a degenerate overlap; penetrationDepth is nonnegative and zero for later hits.
 * center 为扫掠球心，point 为障碍表面点；零比例报告初始重叠而不移动任何对象。
 * 退化重叠可无法确定法线，深度非负且后续碰撞深度为零。
 */
export interface SphereHit extends RayHit {
  center: Vec3;
  penetrationDepth: number;
}

export interface ContactManifold {
  kind: 'manifold';
  normal: Vec3;
  penetrationDepth: number;
  points: {first: Vec3; second: Vec3}[];
}

/**
 * Native controller observation: point is on the observer's counterpart; normal
 * follows event pair ordering. Signed separation includes controller padding.
 * 原生控制器观察：点位于观察者的对方表面，法线遵循事件碰撞对顺序；有符号间距包含外壳。
 */
export interface CharacterContactGeometry {
  kind: 'character';
  normal: Vec3;
  point: Vec3;
  separation: number;
  characterIsFirst: boolean;
}

export type ContactGeometry = ContactManifold|CharacterContactGeometry;

/**
 * Removed entity IDs remain meaningful historical data; check existence before
 * resolving them. Geometry may be absent on lifecycle-only events.
 * 已删除实体 ID 仍是有意义的历史数据，解析前须检查存活；生命周期事件可无几何数据。
 */
export interface ContactEvent {
  phase: 'begin'|'stay'|'sleep'|'end';
  endReason: 'separated'|'poseChanged'|'destroyed'|'reconfigured'|null;
  first: EntityId;
  second: EntityId;
  sensor: boolean;
  step: string;
  geometry: ContactGeometry|null;
}

export interface ContactBatch {
  generation: string;
  index: string;
  events: ContactEvent[];
}

/**
 * Native scheduling publishes contacts without consuming them on script reads.
 * Queries show last synchronized state; controls act on that real simulation.
 * Native failures throw HostError, and scripts cannot advance/reset the world.
 * 原生调度发布接触数据，脚本读取不消费；查询展示最近同步状态，控制作用于真实仿真。
 * 原生失败抛出 HostError，脚本不能推进/重置世界。
 */
export const physics = Object.freeze({
  body(entity: EntityId): PhysicsBodyState {
    return invoke('physics.body', [entity]) as PhysicsBodyState;
  },

  character(entity: EntityId): CharacterState {
    return invoke('physics.character', [entity]) as CharacterState;
  },

  setCharacterInput(entity: EntityId, value: CharacterInput): void {
    invoke('physics.setCharacterInput', [entity, value]);
  },

  setCharacterYaw(entity: EntityId, degrees: number): void {
    invoke('physics.setCharacterYaw', [entity, degrees]);
  },

  stats(): PhysicsStats {
    return invoke('physics.stats', []) as PhysicsStats;
  },

  rayCast(origin: Vec3, displacement: Vec3): RayHit | null {
    return invoke('physics.rayCast', [origin, displacement]) as RayHit | null;
  },

  sphereCast(origin: Vec3, radius: number, displacement: Vec3,
      filter: SphereCastFilter = {}): SphereHit|null {
    return invoke('physics.sphereCast', [origin, radius, displacement,
      {includeSensors: false, ignoreEntity: null, ...filter}]) as SphereHit|null;
  },

  get contacts(): ContactBatch {
    return invoke('physics.contacts', []) as ContactBatch;
  },

  setVelocity(entity: EntityId, value: Vec3): void {
    invoke('physics.setVelocity', [entity, value]);
  },

  setAngularVelocity(entity: EntityId, value: Vec3): void {
    invoke('physics.setAngularVelocity', [entity, value]);
  },

  addImpulse(entity: EntityId, value: Vec3): void {
    invoke('physics.addImpulse', [entity, value]);
  },

  moveKinematic(entity: EntityId, position: Vec3, rotation: Quat, seconds: number): void {
    invoke('physics.moveKinematic', [entity, position, rotation, seconds]);
  },
});

export interface AudioClipInfo {
  channels: number;
  sampleRate: number;
  frames: string;
  durationSeconds: number;
}

export interface AudioListenerState {
  position: Vec3;
  rotation: Quat;
  velocity: Vec3;
}

export interface AudioSpatialState extends SpatialProperties, AudioListenerState {}

/**
 * Source read cursor may include backend prefetch; device/queued presentation
 * time is independent. Decimal frame strings retain all uint64 bits.
 * 源读取游标可含后端预取，设备/队列播放时间独立；十进制帧字符串保留 uint64 全部位。
 */
export interface AudioVoiceState {
  clip: AudioClipInfo;
  settings: VoiceSettings;
  spatial: AudioSpatialState;
  playing: boolean;
  atEnd: boolean;
  sourceFrame: string;
}

export interface AudioOutputStats {
  running: boolean;
  fault: 'none'|'callback'|'stopped'|'rerouted'|'interrupted'|'start'|'stop';
  queuedFrames: number;
  availableFrames: number;
  submittedFrames: string;
  consumedFrames: string;
  discardedFrames: string;
  callbackFrames: string;
  underrunFrames: string;
  callbacks: string;
  clippedSamples: string;
}

export interface AudioPlaybackStats {
  state: 'prepared'|'running'|'closed'|'faulted';
  error: {code: HostErrorCode; message: string}|null;
  output: AudioOutputStats;
}

export interface AudioStats {
  mode: 'offline'|'playback';
  generation: string;
  volume: number;
  assets: number;
  sampleBytes: string;
  voices: number;
  playing: number;
  renderedFrames: string;
  faulted: boolean;
  playback: AudioPlaybackStats|null;
}

/**
 * Real synchronized audio voices; native scheduling loads assets/updates poses.
 * Pause retains cursor, stop pauses/rewinds, and play resumes or restarts an ended
 * voice. Controls affect future mixed blocks; queued PCM remains unchanged.
 * 真实同步声部，由原生调度加载资源/更新位姿；暂停保留游标，停止暂停并归零，播放恢复
 * 或重启已结束声部。控制影响后续混音块，已排队 PCM 不变。
 */
export const audio = Object.freeze({
  voice(entity: EntityId): AudioVoiceState {
    return invoke('audio.voice', [entity]) as AudioVoiceState;
  },

  stats(): AudioStats {
    return invoke('audio.stats', []) as AudioStats;
  },

  listener(): AudioListenerState {
    return invoke('audio.listener', []) as AudioListenerState;
  },

  play(entity: EntityId): void {
    invoke('audio.play', [entity]);
  },

  pause(entity: EntityId): void {
    invoke('audio.pause', [entity]);
  },

  stop(entity: EntityId): void {
    invoke('audio.stop', [entity]);
  },

  seek(entity: EntityId, sourceFrame: string): void {
    invoke('audio.seek', [entity, sourceFrame]);
  },

  setVolume(volume: number): void {
    invoke('audio.setVolume', [volume]);
  },
});

export interface AnimationTransition {
  elapsedSeconds: number;
  durationSeconds: number;
}

export interface AnimationState {
  clip: number;
  asset: string;
  name: string;
  durationSeconds: number;
  timeSeconds: number;
  speed: number;
  looping: boolean;
  playing: boolean;
  transition: AnimationTransition|null;
}

/**
 * Native animation controls; saved descriptions remain unchanged. Same-clip play
 * resumes unless restart; a different clip restores former channels and applies
 * its directional start atomically. Stop restores original binding poses. Reads
 * describe the last synchronized player; scripts cannot advance the native clock.
 * 原生动画控制，不修改保存描述；同片默认恢复、restart 重启，换片原子恢复旧通道并应用
 * 正/反向起点。停止恢复最初绑定姿态；查询反映最近同步状态，脚本不能推进原生时钟。
 */
export const animation = Object.freeze({
  state(entity: EntityId): AnimationState {
    return invoke('animation.state', [entity]) as AnimationState;
  },

  play(entity: EntityId, clip: number|null = null, restart = false): void {
    invoke('animation.play', [entity, clip, restart]);
  },

  /**
   * Blend to a library clip's directional start over unscaled seconds [0,86400].
   * Both clip clocks advance; pause freezes the fade. Interruptions preserve the
   * visible pose. Zero switches immediately; seek/stop cancel the transition.
   * Saved descriptions remain unchanged. Failure retains pose and playback state.
   * 按非缩放秒数 [0,86400] 淡化到库内片段正/反向起点；双时钟推进，暂停冻结过渡。
   * 打断保持可见姿态，零时长立即切换；seek/stop 取消过渡，不改保存描述，失败保留状态。
   */
  crossFade(entity: EntityId, clip: number, seconds: number): void {
    invoke('animation.crossFade', [entity, clip, seconds]);
  },

  pause(entity: EntityId): void {
    invoke('animation.pause', [entity]);
  },

  stop(entity: EntityId): void {
    invoke('animation.stop', [entity]);
  },

  seek(entity: EntityId, seconds: number): void {
    invoke('animation.seek', [entity, seconds]);
  },

  configure(entity: EntityId, speed: number, looping: boolean): void {
    invoke('animation.configure', [entity, speed, looping]);
  },
});

export interface EntityInfo {
  readonly id: EntityId;
  name: string;
  parent: EntityId|null;
  transform: Transform;
  camera: Camera|null;
  mesh: MeshRenderer|null;
  rigidBody: RigidBody|null;
  character: CharacterController|null;
  audioSource: AudioSource|null;
  audioListener: AudioListener|null;
  script: ScriptDescription|null;
}

export interface SceneInfo {
  readonly id: string;
  readonly name: string;
  readonly entities: number;
}

export type HostErrorCode
    = 'InvalidArgument'|'NotFound'|'Io'|'Unavailable'|'InvalidState'|'External';

export interface HostError extends Error {
  readonly name: 'DotHostError';
  readonly code: HostErrorCode;
}

/**
 * Native view with owned identity. Destroyed/foreign handles fail explicitly;
 * getters return snapshots, and assignment commits one validated native edit.
 * 持有身份的原生视图；删除/跨场景句柄明确失败，getter 返回快照，赋值提交一次校验修改。
 */
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

  /**
   * False for deleted IDs in this scene; foreign/invalid IDs still throw.
   * 本场景已删除 ID 返回 false，跨场景/无效 ID 仍抛错。
   */
  get exists(): boolean {
    return invoke('entity.exists', [this.id]) as boolean;
  }

  get camera(): Camera|null {
    return invoke('component.get', [this.id, 'camera']) as Camera | null;
  }

  set camera(value: Camera|null) {
    invoke('component.set', [this.id, 'camera', value]);
  }

  get mesh(): MeshRenderer|null {
    return invoke('component.get', [this.id, 'mesh']) as MeshRenderer | null;
  }

  set mesh(value: MeshRenderer|null) {
    invoke('component.set', [this.id, 'mesh', value]);
  }

  get rigidBody(): RigidBody|null {
    return invoke('component.get', [this.id, 'rigidBody']) as RigidBody | null;
  }

  set rigidBody(value: RigidBody|null) {
    invoke('component.set', [this.id, 'rigidBody', value]);
  }

  get character(): CharacterController|null {
    return invoke('component.get', [this.id, 'character']) as CharacterController | null;
  }

  set character(value: CharacterController|null) {
    invoke('component.set', [this.id, 'character', value]);
  }

  get audioSource(): AudioSource|null {
    return invoke('component.get', [this.id, 'audioSource']) as AudioSource | null;
  }

  set audioSource(value: AudioSource|null) {
    invoke('component.set', [this.id, 'audioSource', value]);
  }

  get audioListener(): AudioListener|null {
    return invoke('component.get', [this.id, 'audioListener']) as AudioListener | null;
  }

  set audioListener(value: AudioListener|null) {
    invoke('component.set', [this.id, 'audioListener', value]);
  }

  get script(): ScriptDescription|null {
    return invoke('component.get', [this.id, 'script']) as ScriptDescription | null;
  }

  set script(value: ScriptDescription|null) {
    invoke('component.set', [this.id, 'script', value]);
  }

  get audioVoice(): AudioVoiceState {
    return audio.voice(this.id);
  }

  playAudio(): void {
    audio.play(this.id);
  }

  pauseAudio(): void {
    audio.pause(this.id);
  }

  stopAudio(): void {
    audio.stop(this.id);
  }

  seekAudio(sourceFrame: string): void {
    audio.seek(this.id, sourceFrame);
  }

  get animationState(): AnimationState {
    return animation.state(this.id);
  }

  playAnimation(clip: number|null = null, restart = false): void {
    animation.play(this.id, clip, restart);
  }

  crossFadeAnimation(clip: number, seconds: number): void {
    animation.crossFade(this.id, clip, seconds);
  }

  pauseAnimation(): void {
    animation.pause(this.id);
  }

  stopAnimation(): void {
    animation.stop(this.id);
  }

  seekAnimation(seconds: number): void {
    animation.seek(this.id, seconds);
  }

  configureAnimation(speed: number, looping: boolean): void {
    animation.configure(this.id, speed, looping);
  }

  get physicsBody(): PhysicsBodyState {
    return physics.body(this.id);
  }

  get characterState(): CharacterState {
    return physics.character(this.id);
  }

  moveCharacter(velocity: Vec3, jump = false): void {
    physics.setCharacterInput(this.id, {velocity, jump});
  }

  setCharacterYaw(degrees: number): void {
    physics.setCharacterYaw(this.id, degrees);
  }

  setVelocity(value: Vec3): void {
    physics.setVelocity(this.id, value);
  }

  setAngularVelocity(value: Vec3): void {
    physics.setAngularVelocity(this.id, value);
  }

  addImpulse(value: Vec3): void {
    physics.addImpulse(this.id, value);
  }

  moveKinematic(position: Vec3, rotation: Quat, seconds: number): void {
    physics.moveKinematic(this.id, position, rotation, seconds);
  }

  get parent(): Entity|null {
    const parent = this.info().parent;
    return parent === null ? null : new Entity(parent);
  }

  set parent(value: Entity|null) {
    invoke('hierarchy.setParent', [this.id, value === null ? null : value.id]);
  }

  /**
   * Preserve local or world pose explicitly; unrepresentable shear fails atomically.
   * 显式保留局部或世界姿态；无法表示的剪切导致事务失败。
   */
  reparent(parent: Entity|null, space: 'local'|'world'): void {
    invoke('hierarchy.reparent', [this.id, parent === null ? null : parent.id, space]);
  }

  /**
   * Right-handed column-major world matrix with 16 float values.
   * 右手、列主序世界矩阵，含 16 个浮点值。
   */
  worldMatrix(): readonly number[] {
    return invoke('transform.world', [this.id]) as number[];
  }

  /**
   * Removes this entity and its whole native subtree.
   * 删除本实体及完整原生子树。
   */
  destroy(): void {
    invoke('entity.destroy', [this.id]);
  }
}

export const scene = Object.freeze({
  info(): SceneInfo {
    return invoke('scene.info', []) as SceneInfo;
  },

  /** Null when no primary component exists; wrappers retain scene-scoped IDs.
   * 无主组件时返回 null，包装器保留场景范围 ID。 */
  get primaryCamera(): Entity | null {
    const id = invoke('scene.primaryCamera', []) as EntityId | null;
    return id === null ? null : new Entity(id);
  },

  get primaryListener(): Entity | null {
    const id = invoke('scene.primaryListener', []) as EntityId | null;
    return id === null ? null : new Entity(id);
  },

  rename(name: string): void {
    invoke('scene.rename', [name]);
  },

  create(name = 'Entity'): Entity {
    return new Entity(invoke('entity.create', [name]) as EntityId);
  },

  /** Group selected subtree roots at the active (last) entity while preserving world poses.
   * 将所选子树根围绕活动（末项）实体成组，保持世界姿态。 */
  group(entities: readonly Entity[], name = 'Group'): Entity {
    return new Entity(invoke('scene.group', [entities.map(e => e.id), name]) as EntityId);
  },

  /** Remove only component-free containers; returns surviving exposed children.
   * 仅拆除无功能组件容器，返回露出的存活子项。 */
  ungroup(entities: readonly Entity[]): Entity[] {
    return (invoke('scene.ungroup', [entities.map(e => e.id)]) as EntityId[]).map(id => new Entity(id));
  },

  /** Deterministic creation-ID order; wrappers do not own native entities.
   * 按创建 ID 确定性排序，包装器不拥有原生实体。 */
  entities(): Entity[] {
    return (invoke('entity.list', []) as EntityId[]).map(id => new Entity(id));
  },
});

/**
 * Constructor context is an owned JSON snapshot. Preparation permits scene
 * reads; mutations belong in lifecycle hooks. Nested properties are owned data.
 * 构造上下文为拥有的 JSON 快照；准备阶段允许读场景，修改放在生命周期钩子，嵌套属性
 * 是拥有的数据。
 */
export interface ScriptContext<Properties extends object = Record<string, unknown>> {
  readonly entity: EntityId;
  readonly properties: Properties;
}

/**
 * Optional hooks default to no action. Stop callbacks require explicit service
 * shutdown; garbage collection/destruction never invokes them.
 * 可选钩子默认无动作，停止回调须显式关闭服务，垃圾回收/销毁不调用它们。
 */
export class ScriptComponent<Properties extends object = Record<string, unknown>> {
  readonly self: Entity;
  readonly properties: Readonly<Properties>;

  constructor(context: ScriptContext<Properties>) {
    this.self = new Entity(context.entity);
    this.properties = Object.freeze({...context.properties});
  }

  onStart(): void|Promise<void> {
  }

  onUpdate(_seconds: number): void|Promise<void> {
  }

  onFixedUpdate(_seconds: number): void|Promise<void> {
  }

  onStop(): void|Promise<void> {
  }
}

/**
 * Frame-boundary scene replacement; request acceptance is not completion.
 * 帧边界场景替换；请求被接受不代表加载完成。
 */
export interface SceneLocation {
  scope: string;
  path: string|null;
}

export interface SceneRequest {
  id: string;
  path: string;
  state: 'pending'|'loading'|'succeeded'|'failed'|'cancelled';
  committed: boolean;
  error: {code: HostErrorCode; message: string}|null;
}

export const scenes = Object.freeze({
  current(): SceneLocation {
    return invoke('scenes.current', []) as SceneLocation;
  },

  request(path: string): string {
    return invoke('scenes.request', [path]) as string;
  },

  status(id: string): SceneRequest {
    return invoke('scenes.status', [id]) as SceneRequest;
  }
});

/**
 * Owned finite JSON data, independent of scene/realm lifetime. Large IDs are strings.
 * 独立于场景/realm 生命周期的有限 JSON 数据；大整数 ID 使用字符串。
 */
export type GameStateData = {
  [key: string]: JsonValue
};
export interface GameStateSnapshot {
  revision: string;
  data: GameStateData;
}

export interface SaveSlot {
  name: string;
  bytes: string;
}

/**
 * Session-wide state survives scene replacement; observations are owned copies.
 * Key names are literal. Replace requires the current revision; no-op edits retain it.
 * 游戏级状态跨场景替换保留，观察结果为独立副本；键为字面名称。替换须携带当前修订，
 * 无变化的修改保留修订。
 */
export const gameState = Object.freeze({
  snapshot(): GameStateSnapshot {
    return invoke('gameState.snapshot', []) as GameStateSnapshot;
  },

  get(key: string): JsonValue|undefined {
    const result = invoke('gameState.get', [key]) as {present: boolean; value: JsonValue};
    return result.present ? result.value : undefined;
  },

  set(key: string, value: JsonValue): string {
    return (invoke('gameState.set', [key, value]) as {revision: string}).revision;
  },

  remove(key: string): string {
    return (invoke('gameState.remove', [key]) as {revision: string}).revision;
  },

  replace(data: GameStateData, revision: string): string {
    return (invoke('gameState.replace', [data, revision]) as {revision: string}).revision;
  }
});

/**
 * Explicit save operations; construction/migration cannot access disk. No auto-load
 * or auto-save. The player uses its stable application ID's user directory; editor
 * Play uses disposable isolated storage. Loading replaces only game state, not a scene.
 * 显式存档操作，构造/迁移期间不可访问磁盘；不自动读写。独立游戏使用稳定应用身份的用户
 * 目录，编辑器 Play 使用独立临时存储。加载仅替换游戏状态，不切换场景。
 */
export const saves = Object.freeze({
  list(): SaveSlot[] {
    return invoke('saves.list', []) as SaveSlot[];
  },

  read(slot: string): GameStateSnapshot {
    return invoke('saves.read', [slot]) as GameStateSnapshot;
  },

  write(slot: string, overwrite: boolean): SaveSlot {
    return invoke('saves.write', [slot, overwrite]) as SaveSlot;
  },

  load(slot: string, revision: string): string {
    return (invoke('saves.load', [slot, revision]) as {revision: string}).revision;
  },

  remove(slot: string): void {
    invoke('saves.remove', [slot]);
  }
});
