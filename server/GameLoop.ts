import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import type { GameState, PlayerState, ServerStatePatch, TileData, NPCSpawn, PermanentItemSpawn } from '../src/shared/types';
import {
  TICK_DURATION_MS,
  PLAYER_START_X, PLAYER_START_Y,
} from '../src/shared/constants';
import { createWorldFromTiles, findWalkableTileNear } from '../src/world/WorldState';
import { spawnNPC } from '../src/systems/NPCSystem';
import { loadEntitiesFromDB } from './db/EntityLoader';
import { createDefaultSkills } from '../src/systems/SkillSystem';
import { createEmptyInventory } from '../src/systems/InventorySystem';
import { createEmptyBank } from '../src/systems/BankSystem';
import { processTick } from '../src/engine/TickSystem';
import type { GameAction } from '../src/shared/types';

const __dirname = dirname(fileURLToPath(import.meta.url));

interface WorldMapJSON {
  version?: number;
  width:    number;
  height:   number;
  tiles:    TileData[][];
  // v2 fields
  npcSpawns?:      NPCSpawn[];
  permanentItems?: PermanentItemSpawn[];
  vertexHeights?:  number[];
  // Water tiles — separate list used by the renderer; walkable is authoritative
  // source of truth for pathfinding, but old maps may have saved these as
  // walkable=true before the editor bug was fixed. We re-enforce on load.
  waterTiles?: { tileX: number; tileY: number }[];
  // Wall + pillar edge features — relayed to the client for rendering.
  walls?: { tileX: number; tileY: number; orient: number; pillar: boolean; objectId: string }[];
  // legacy v1 fields (ignored by new renderer)
  pixelWidth?:  number;
  pixelHeight?: number;
  pixels?:      number[];
}


const DEFAULT_PERMANENT_ITEMS: PermanentItemSpawn[] = [];

function loadWorldMap(): WorldMapJSON {
  const mapPath = join(__dirname, '../public/maps/worldMap.json');
  try {
    const raw = readFileSync(mapPath, 'utf-8');
    const data = JSON.parse(raw) as WorldMapJSON;
    // Normalize tile heights for legacy maps that predate the height field
    data.tiles = data.tiles.map(row =>
      row.map(tile => ({ ...tile, height: (tile as TileData & { height?: number }).height ?? 0 })),
    );
    return data;
  } catch {
    console.error('[GameLoop] Failed to load worldMap.json — using blank fallback.');
    const W = 64, H = 64;
    const tiles: TileData[][] = [];
    for (let y = 0; y < H; y++) {
      tiles[y] = [];
      for (let x = 0; x < W; x++) {
        tiles[y][x] = { x, y, walkable: true, type: 'grass', obstacle: 'none', blocksRanged: false, groundColor: '#7ec850', height: 0 };
      }
    }
    return { version: 2, width: W, height: H, tiles, npcSpawns: [], permanentItems: DEFAULT_PERMANENT_ITEMS };
  }
}

type BroadcastFn = (patch: ServerStatePatch) => void;

export class GameLoop {
  private state:     GameState;
  private pendingActions = new Map<string, GameAction[]>();
  private timer:     ReturnType<typeof setInterval> | null = null;
  private broadcast: BroadcastFn;
  private worldTiles: TileData[][];
  private waterTiles: { tileX: number; tileY: number }[] = [];
  private walls: { tileX: number; tileY: number; orient: number; pillar: boolean; objectId: string }[] = [];

  constructor(broadcast: BroadcastFn) {
    this.broadcast = broadcast;

    // Fire-and-forget DB load: JSON registries are already populated synchronously;
    // this hot-swaps them with DB data if available.
    void loadEntitiesFromDB();

    const mapData = loadWorldMap();
    this.worldTiles = mapData.tiles;
    this.waterTiles = mapData.waterTiles ?? [];
    this.walls      = mapData.walls ?? [];

    const world = createWorldFromTiles(mapData.tiles, mapData.vertexHeights, this.walls);

    // Walkability is authored per-tile (water rendering is a separate visual
    // layer), so we trust the saved tile.walkable. This lets terrain raised
    // above the waterline over a water tile remain walkable while the water
    // plane still renders there. Deep/impassable water is painted non-walkable
    // in the editor (PaintWater defaults to blocked).

    // NPC spawns: fully data-driven from map file
    const npcSpawnDefs = mapData.npcSpawns ?? [];

    const npcs = npcSpawnDefs.map((def, i) => {
      const pos = findWalkableTileNear(world, def.tileX, def.tileY);
      return spawnNPC(`${def.kind}-${i + 1}`, def.kind, pos.x, pos.y);
    });

    // Permanent items: use map file's list (v2) or legacy defaults
    const itemDefs = mapData.permanentItems ?? DEFAULT_PERMANENT_ITEMS;

    // Legacy fallback: if no items defined and old map format, place a starter item rack
    // near the chest tile so returning players can still equip
    let droppedItems = itemDefs.map((def, i) => ({
      id:           `perm-${def.itemId}-${i}`,
      itemId:       def.itemId,
      quantity:     def.quantity,
      tileX:        def.x,
      tileY:        def.y,
      droppedAtTick: 0,
      permanent:    true,
    }));

    if (droppedItems.length === 0) {
      // Find chest tile for legacy rack placement
      let chestX = Math.max(0, Math.floor(world.width / 2) - 4);
      let chestY = Math.max(0, Math.floor(world.height / 2) - 4);
      outer: for (let ty = 0; ty < world.height; ty++) {
        for (let tx = 0; tx < world.width; tx++) {
          if (world.tiles[ty]?.[tx]?.obstacle === 'chest') { chestX = tx; chestY = ty; break outer; }
        }
      }
      droppedItems = [
        { id: 'rack-pickaxe',         itemId: 'pickaxe',         quantity: 1,   tileX: chestX - 2, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-iron-axe',        itemId: 'iron_axe',        quantity: 1,   tileX: chestX - 1, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-basic-chaingun',  itemId: 'basic_chaingun',  quantity: 1,   tileX: chestX,     tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-kinetic-charges', itemId: 'kinetic_charges', quantity: 500, tileX: chestX + 1, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-fishing-rod',     itemId: 'fishing_rod',     quantity: 1,   tileX: chestX + 2, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
      ];
    }

    this.state = {
      tick: 0,
      world,
      players: {},
      npcs,
      droppedItems,
      pendingRespawns: [],
      messages: {},
      depletedTrees: {},
      treeHealth: {},
      depletedRocks: {},
      rockHealth: {},
    };
  }

  start(): void {
    this.timer = setInterval(() => this.tick(), TICK_DURATION_MS);
  }

  stop(): void {
    if (this.timer !== null) clearInterval(this.timer);
    this.timer = null;
  }

  addPlayer(playerId: string, name: string, savedState?: PlayerState): void {
    let player: PlayerState;
    if (savedState) {
      // Restore returning player — keep their stats, position, etc.
      // Reset transient combat state so they don't resume mid-fight.
      // Recalculate maxHp from the hitpoints skill level so it stays in sync.
      // Merge defaults under saved skills so any skill added since the player's
      // last login (e.g. 'gunner') starts at level 1 rather than crashing.
      const skills = { ...createDefaultSkills(), ...savedState.skills };
      const restoredMaxHp = skills.hitpoints?.level ?? 10;
      player = {
        ...savedState,
        skills,
        bank: savedState.bank ?? createEmptyBank(),
        hp: Math.min(savedState.hp, restoredMaxHp),
        maxHp: restoredMaxHp,
        path: [],
        attackTargetId: null,
        talkTargetId: null,
        pickupItemId: null,
        chopTargetX: null,
        chopTargetY: null,
        mineTargetX: null,
        mineTargetY: null,
        fishTargetX: null,
        fishTargetY: null,
        chatMessage: '',
        chatMessageTick: -999,
        lastHitTick:    -999,
        lastAttackTick: -999,
        lastChopTick:   -999,
        lastMineTick:   -999,
        lastFishTick:   -999,
        dying:         false,
        dyingTick:     -999,
        lastRegenTick: -999,
      };
    } else {
      // Clamp spawn to actual map dimensions so old maps (64×64) work with new constants (128,128)
      const startX = Math.min(PLAYER_START_X, this.state.world.width - 1);
      const startY = Math.min(PLAYER_START_Y, this.state.world.height - 1);
      const spawn = findWalkableTileNear(this.state.world, startX, startY);
      player = createInitialPlayer(spawn.x, spawn.y, name);
    }

    this.state = {
      ...this.state,
      players: { ...this.state.players, [playerId]: player },
    };
    this.pendingActions.set(playerId, []);
  }

  /** Removes the player and returns their final state for persistence. */
  removePlayer(playerId: string): PlayerState | undefined {
    const state = this.state.players[playerId];
    const { [playerId]: _removed, ...rest } = this.state.players;
    this.state = { ...this.state, players: rest };
    this.pendingActions.delete(playerId);
    return state;
  }

  enqueueActions(playerId: string, actions: GameAction[]): void {
    const existing = this.pendingActions.get(playerId);
    if (existing) existing.push(...actions);
  }

  getWorldTiles(): TileData[][] { return this.worldTiles; }
  getWaterTiles(): { tileX: number; tileY: number }[] { return this.waterTiles; }
  getWalls() { return this.walls; }
  getVertexHeights(): number[] { return Array.from(this.state.world.vertexHeights); }

  /** Returns a snapshot of all currently-connected players for checkpoint saves. */
  getPlayerStates(): Map<string, PlayerState> {
    const result = new Map<string, PlayerState>();
    for (const [id, player] of Object.entries(this.state.players)) {
      result.set(id, player);
    }
    return result;
  }

  private tick(): void {
    const playerActions = new Map<string, GameAction[]>();
    for (const [id, actions] of this.pendingActions) {
      playerActions.set(id, actions.splice(0));
    }

    this.state = processTick(this.state, playerActions);

    const { world: _world, ...patch } = this.state;
    this.broadcast(patch as ServerStatePatch);
  }
}

function createInitialPlayer(tileX: number, tileY: number, name: string): PlayerState {
  const inventory = createEmptyInventory();
  // Starter items are on the floor near spawn (permanent test rack) — players pick up what they need.
  // To restore starting inventory, uncomment these lines:
  // inventory[0] = { itemId: 'pickaxe', quantity: 1 };
  // inventory[1] = { itemId: 'iron_axe', quantity: 1 };
  // inventory[2] = { itemId: 'basic_chaingun', quantity: 1 };

  const skills = createDefaultSkills();
  const startHp = skills.hitpoints.level; // 1 HP per hitpoints level (starts at level 10)

  return {
    tileX,
    tileY,
    facing: 'south',
    path: [],
    destinationX: tileX,
    destinationY: tileY,
    skills,
    inventory,
    bank: createEmptyBank(),
    equipped: {},
    hp: startHp,
    maxHp: startHp,
    attackTargetId: null,
    talkTargetId: null,
    lastAttackTick: -999,
    pickupItemId: null,
    lastHitTick: -999,
    lastHitDamage: 0,
    playerName: name,
    shirtColor: 'blue',
    skinColor: 'fair',
    chatMessage: '',
    chatMessageTick: -999,
    chopTargetX: null,
    chopTargetY: null,
    lastChopTick: -999,
    mineTargetX: null,
    mineTargetY: null,
    lastMineTick: -999,
    fishTargetX: null,
    fishTargetY: null,
    lastFishTick: -999,
    dying: false,
    dyingTick: -999,
    lastRegenTick: -999,
  };
}
