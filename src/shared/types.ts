export type Direction =
  | 'north' | 'north_east' | 'east' | 'south_east'
  | 'south' | 'south_west' | 'west' | 'north_west';

export interface GridPosition {
  x: number;
  y: number;
}

// ---------- Appearance ----------

export type ShirtColor = 'blue' | 'red' | 'yellow' | 'green';
export type SkinColor  = 'fair' | 'tan' | 'olive' | 'brown';

// ---------- Skills ----------

export type SkillId =
  | 'warrior' | 'defence' | 'hitpoints'
  | 'woodcutting' | 'mining' | 'fishing' | 'cooking' | 'gunner';

export const ALL_SKILLS: SkillId[] = [
  'warrior', 'defence', 'hitpoints',
  'woodcutting', 'mining', 'fishing', 'cooking', 'gunner'
];

// Only skills with active gameplay systems — shown in the Skills panel
export const VISIBLE_SKILLS: SkillId[] = [
  'hitpoints', 'defence', 'warrior', 'gunner', 'woodcutting', 'mining', 'fishing', 'cooking'
];

export interface SkillState {
  xp: number;
  level: number;
}

export type SkillsState = Record<SkillId, SkillState>;

// ---------- Items ----------

export type EquipSlot =
  | 'head' | 'neck' | 'body' | 'legs' | 'feet'
  | 'hands' | 'ring' | 'leftHand' | 'rightHand' | 'ammo';

export interface EquipStats {
  meleeAttackBonus:    number;
  meleeStrengthBonus:  number;
  meleeDefenseBonus:   number;
  rangedAttackBonus:   number;
  rangedStrengthBonus: number;
  rangedDefenseBonus:  number;
}

export interface ItemDefinition {
  id: string;
  name: string;
  stackable: boolean;
  value: number;
  tradable?: boolean;
  examine?: string;
  equipSlot?: EquipSlot;
  stats?: Partial<EquipStats>;
  requirements?: Partial<Record<SkillId, number>>;
  toolType?: 'axe' | 'pickaxe' | 'fishing_rod';
  combatStyle?: 'melee' | 'gunner';
  twoHanded?: boolean;
  // 'resource' | 'equipment' | 'food'. Food items can be eaten to heal.
  itemType?: 'resource' | 'equipment' | 'food';
  // HP restored when this food is eaten (only meaningful for itemType==='food').
  healAmount?: number;
}

export interface ItemStack {
  itemId: string;
  quantity: number;
}

// ---------- Tiles ----------

export type TileType = 'grass' | 'dirt' | 'stone' | 'water' | 'cliff' | 'wall' | 'door';

export interface TileData {
  x: number;
  y: number;
  walkable: boolean;
  // True for filler tiles in unassigned world-grid cells (multi-chunk worlds).
  // The client skips rendering these entirely; absent/false on authored tiles.
  isVoid?: boolean;
  type: TileType;
  obstacle: string;     // empty = none; otherwise a DB object ID e.g. "tree", "rock"
  blocksRanged: boolean;
  groundColor: string;  // hex color for terrain texture, e.g. '#7ec850'
  height: number;       // legacy: per-tile average height kept for backward-compat migration
  obstacleRotation?: number;  // 90° steps about Y (0..3); client renders the obstacle rotated
}

// ---------- Map file format (editor output) ----------

export interface NPCSpawn {
  kind:  string;
  tileX: number;
  tileY: number;
}

export interface PermanentItemSpawn {
  itemId: string;
  quantity: number;
  x: number;
  y: number;
}

// Wall / pillar edge feature (see SharedTypes.hpp WallSeg).
export interface WallSeg {
  tileX: number;
  tileY: number;
  orient: number;     // 0..7, 45° steps
  pillar: boolean;    // false = wall (edge), true = pillar (corner)
  objectId: string;   // variant id ("" = placeholder)
}

export interface WorldMapFile {
  version: 2;
  width: number;
  height: number;
  tiles: TileData[][];
  npcSpawns: NPCSpawn[];
  permanentItems: PermanentItemSpawn[];
  walls?: WallSeg[];
  // Per-vertex heights — flat row-major array, length (width+1)*(height+1).
  // Optional: absent in old v2 maps, which are migrated from TileData.height on load.
  vertexHeights?: number[];
}

// ---------- World manifest (multi-chunk overworld) ----------
// public/maps/world.json assigns chunk map files to cells of a world grid.
// Cell (cx,cy) owns global tiles [cx*chunkSize, cx*chunkSize+chunkSize).
// Chunk map files stay in local coordinates and are offset on assembly.
// Mirror of shared::WorldManifest in SharedTypes.hpp.

export interface WorldChunkRef {
  cx: number;        // world grid cell (non-negative in v1)
  cy: number;
  mapFile: string;   // relative to the manifest's directory
  name?: string;
  music?: string;    // looping background music file (relative to assets/music/)
}

export interface WorldManifest {
  version: number;
  chunkSize: number;                  // every chunk map must be chunkSize × chunkSize
  spawn: { x: number; y: number };    // GLOBAL tile coordinates
  chunks: WorldChunkRef[];
}

// ---------- NPCs ----------

export type NPCKind = string;

export type AIBehavior = 'static' | 'wander';

export interface NPCState {
  id: string;
  kind: NPCKind;
  tileX: number;
  tileY: number;
  facing: Direction;
  path: GridPosition[];
  hp: number;
  maxHp: number;
  homeX: number;
  homeY: number;
  waitTicks: number;
  dying: boolean;
  dyingTick: number;
  lastAttackTick: number;
  lastHitTick: number;
  lastHitDamage: number;
}

// ---------- Entities ----------

export interface PlayerState {
  tileX: number;
  tileY: number;
  facing: Direction;
  path: GridPosition[];
  destinationX: number;
  destinationY: number;
  skills: SkillsState;
  inventory: (ItemStack | null)[];
  bank: (ItemStack | null)[];
  equipped: Partial<Record<EquipSlot, ItemStack>>;
  hp: number;
  maxHp: number;
  attackTargetId: string | null;
  talkTargetId: string | null;
  lastAttackTick: number;
  pickupItemId: string | null;
  lastHitTick: number;
  lastHitDamage: number;
  playerName: string;
  shirtColor: ShirtColor;
  skinColor: SkinColor;
  chatMessage: string;
  chatMessageTick: number;
  chopTargetX: number | null;
  chopTargetY: number | null;
  lastChopTick: number;
  mineTargetX: number | null;
  mineTargetY: number | null;
  lastMineTick: number;
  fishTargetX: number | null;
  fishTargetY: number | null;
  lastFishTick: number;
  // Production facility (Preparation Table / Cooking Range) the player is using.
  useTargetX: number | null;
  useTargetY: number | null;
  lastProduceTick: number;
  // Eating: the player cannot attack/gather-roll until this tick. The heal is
  // applied immediately on EAT_FOOD; this only gates actions for the duration.
  eatUntilTick: number;
  // Item id the player visually holds while gathering (axe/pickaxe/fishing rod),
  // overriding their equipped weapon on the client. '' = none (show equipped).
  // Server-derived each tick (see systems/GatheringTool.ts); not persisted.
  activeToolItemId: string;
  // Tick stamp bumped whenever ANY skill levels up — drives the client's
  // level-up VFX (a one-shot above the head). Rises monotonically; not persisted.
  lastLevelUpTick: number;
  dying: boolean;
  dyingTick: number;
  lastRegenTick: number;
}

// ---------- World ----------

export interface WorldState {
  width: number;
  height: number;
  tiles: TileData[][];
  // Per-vertex heights — flat row-major Float32Array, length (width+1)*(height+1).
  // Index formula: row * (width+1) + col  where row ∈ [0,height], col ∈ [0,width].
  vertexHeights: Float32Array;
  // Per-tile wall-clip bitmask (blocked outgoing directions). Built from the
  // map's walls/pillars; absent = no edge blocking. See WorldState.ts.
  wallClip?: Uint8Array;
}

// ---------- Respawns ----------

export interface RespawnEntry {
  id: string;
  kind: NPCKind;
  homeX: number;
  homeY: number;
  respawnAtTick: number;
}

// ---------- Game ----------

export interface GameState {
  tick: number;
  world: WorldState;
  players: Record<string, PlayerState>;
  npcs: NPCState[];
  droppedItems: DroppedItemState[];
  pendingRespawns: RespawnEntry[];
  messages: Record<string, string[]>;
  depletedTrees: Record<string, number>;  // key="{x}-{y}", value=respawnAtTick
  treeHealth: Record<string, number>;     // key="{x}-{y}", value=ticksRemainingBeforeDepletion
  depletedRocks: Record<string, number>;  // mirror of depletedTrees for mining
  rockHealth: Record<string, number>;     // mirror of treeHealth for mining
}

// State broadcast from server — world omitted (generated locally from seed)
export type ServerStatePatch = Omit<GameState, 'world'>;

// ---------- Dropped Items ----------

export interface DroppedItemState {
  id: string;
  itemId: string;
  quantity: number;
  tileX: number;
  tileY: number;
  droppedAtTick: number;
  permanent?: boolean;  // if true: never despawns, stays on floor after pickup (test rack items)
}

// ---------- Hover / Clickbox ----------

export type ClickboxKind = 'walkable' | 'tree' | 'rock' | 'chest' | 'npc' | 'item' | 'equipped' | 'player' | 'none';

export interface HoverTarget {
  kind: ClickboxKind;
  tileX: number;
  tileY: number;
  npcId?: string;
  droppedItemId?: string;
  itemId?: string;
  equipSlot?: EquipSlot;
  playerId?: string;
  playerName?: string;
  playerLevel?: number;
}

// ---------- Actions ----------

export interface MoveToAction      { type: 'MOVE_TO';     targetX: number; targetY: number; }
export interface ChopTreeAction    { type: 'CHOP_TREE';   tileX: number;   tileY: number; }
export interface MineRockAction    { type: 'MINE_ROCK';   tileX: number;   tileY: number; }
export interface FishAction        { type: 'FISH';        tileX: number;   tileY: number; }
export interface UseFacilityAction { type: 'USE_FACILITY'; tileX: number;  tileY: number; }
export interface EatFoodAction     { type: 'EAT_FOOD';    slotIndex: number; }
export interface AttackNPCAction   { type: 'ATTACK_NPC';  npcId: string; }
export interface TalkToAction      { type: 'TALK_TO';     npcId: string; }
export interface TakeItemAction    { type: 'TAKE_ITEM';   droppedItemId: string; }
export interface DropItemAction    { type: 'DROP_ITEM';   slotIndex: number; }
export interface MoveSlotAction    { type: 'MOVE_SLOT';   fromSlot: number; toSlot: number; }
export interface MoveBankSlotAction { type: 'MOVE_BANK_SLOT'; fromSlot: number; toSlot: number; }
export interface EquipItemAction   { type: 'EQUIP_ITEM';  slotIndex: number; }
export interface UnequipItemAction { type: 'UNEQUIP_ITEM'; slot: EquipSlot; }
export interface SendChatAction       { type: 'SEND_CHAT';      message: string; }
export interface SetAppearanceAction  { type: 'SET_APPEARANCE'; playerName: string; shirtColor: ShirtColor; skinColor: SkinColor; }
export interface OpenBankAction       { type: 'OPEN_BANK'; tileX?: number; tileY?: number; }
export interface DepositItemAction    { type: 'DEPOSIT_ITEM';  slotIndex: number; quantity: number; }
export interface DepositAllAction     { type: 'DEPOSIT_ALL'; }
export interface DepositWornAction    { type: 'DEPOSIT_WORN'; }
export interface WithdrawItemAction   { type: 'WITHDRAW_ITEM'; bankSlot: number; quantity: number; }

export type GameAction =
  | MoveToAction | ChopTreeAction | MineRockAction | FishAction
  | UseFacilityAction | EatFoodAction
  | AttackNPCAction | TalkToAction | TakeItemAction
  | DropItemAction | MoveSlotAction | MoveBankSlotAction
  | EquipItemAction | UnequipItemAction | SendChatAction
  | SetAppearanceAction
  | OpenBankAction | DepositItemAction | DepositAllAction | DepositWornAction | WithdrawItemAction;
