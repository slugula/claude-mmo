export type Direction = 'north' | 'south' | 'east' | 'west';

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
  | 'woodcutting' | 'mining' | 'gunner';

export const ALL_SKILLS: SkillId[] = [
  'warrior', 'defence', 'hitpoints',
  'woodcutting', 'mining', 'gunner'
];

// Only skills with active gameplay systems — shown in the Skills panel
export const VISIBLE_SKILLS: SkillId[] = [
  'hitpoints', 'defence', 'warrior', 'gunner', 'woodcutting'
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
  toolType?: 'axe' | 'pickaxe';
  combatStyle?: 'melee' | 'gunner';
  twoHanded?: boolean;
}

export interface ItemStack {
  itemId: string;
  quantity: number;
}

// ---------- Tiles ----------

export type TileType = 'grass' | 'dirt' | 'stone' | 'water';

export type ObstacleType = 'tree' | 'rock' | 'chest' | 'none';

export interface TileData {
  x: number;
  y: number;
  walkable: boolean;
  type: TileType;
  obstacle: ObstacleType;
  blocksRanged: boolean;  // true for solid obstacles (trees, walls); rocks are false (safespot)
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
  dying: boolean;
  dyingTick: number;
  lastRegenTick: number;
}

// ---------- World ----------

export interface WorldState {
  width: number;
  height: number;
  tiles: TileData[][];
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
export interface AttackNPCAction   { type: 'ATTACK_NPC';  npcId: string; }
export interface TalkToAction      { type: 'TALK_TO';     npcId: string; }
export interface TakeItemAction    { type: 'TAKE_ITEM';   droppedItemId: string; }
export interface DropItemAction    { type: 'DROP_ITEM';   slotIndex: number; }
export interface MoveSlotAction    { type: 'MOVE_SLOT';   fromSlot: number; toSlot: number; }
export interface EquipItemAction   { type: 'EQUIP_ITEM';  slotIndex: number; }
export interface UnequipItemAction { type: 'UNEQUIP_ITEM'; slot: EquipSlot; }
export interface SendChatAction       { type: 'SEND_CHAT';      message: string; }
export interface SetAppearanceAction  { type: 'SET_APPEARANCE'; playerName: string; shirtColor: ShirtColor; skinColor: SkinColor; }
export interface OpenBankAction       { type: 'OPEN_BANK'; }
export interface DepositItemAction    { type: 'DEPOSIT_ITEM';  slotIndex: number; quantity: number; }
export interface DepositAllAction     { type: 'DEPOSIT_ALL'; }
export interface DepositWornAction    { type: 'DEPOSIT_WORN'; }
export interface WithdrawItemAction   { type: 'WITHDRAW_ITEM'; bankSlot: number; quantity: number; }

export type GameAction =
  | MoveToAction | ChopTreeAction | MineRockAction
  | AttackNPCAction | TalkToAction | TakeItemAction
  | DropItemAction | MoveSlotAction
  | EquipItemAction | UnequipItemAction | SendChatAction
  | SetAppearanceAction
  | OpenBankAction | DepositItemAction | DepositAllAction | DepositWornAction | WithdrawItemAction;
