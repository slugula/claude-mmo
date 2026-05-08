import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import type { GameState, PlayerState, ServerStatePatch, TileData } from '../src/shared/types';
import {
  TICK_DURATION_MS,
  PLAYER_START_X, PLAYER_START_Y,
} from '../src/shared/constants';
import { createWorldFromTiles, findWalkableTileNear } from '../src/world/WorldState';
import { spawnNPC } from '../src/systems/NPCSystem';
import { createDefaultSkills } from '../src/systems/SkillSystem';
import { createEmptyInventory } from '../src/systems/InventorySystem';
import { createEmptyBank } from '../src/systems/BankSystem';
import { processTick } from '../src/engine/TickSystem';
import type { GameAction } from '../src/shared/types';

const __dirname = dirname(fileURLToPath(import.meta.url));

interface WorldMapJSON {
  width:       number;
  height:      number;
  tiles:       TileData[][];
  pixelWidth:  number;
  pixelHeight: number;
  pixels:      number[];
}

function loadWorldMap(): WorldMapJSON {
  const mapPath = join(__dirname, '../public/maps/worldMap.json');
  try {
    const raw = readFileSync(mapPath, 'utf-8');
    return JSON.parse(raw) as WorldMapJSON;
  } catch {
    console.error('[GameLoop] Failed to load worldMap.json — run `npm run export-map` first. Using fallback.');
    const PW = 64 * 3, PH = 64 * 3;
    const tiles: TileData[][] = [];
    for (let y = 0; y < 64; y++) {
      tiles[y] = [];
      for (let x = 0; x < 64; x++) {
        tiles[y][x] = { x, y, walkable: true, type: 'grass', obstacle: 'none', blocksRanged: false, groundColor: '#7ec850' };
      }
    }
    return { width: 64, height: 64, tiles, pixelWidth: PW, pixelHeight: PH, pixels: new Array(PW * PH).fill(0x007ec850) };
  }
}

type BroadcastFn = (patch: ServerStatePatch) => void;

export class GameLoop {
  private state:       GameState;
  private pendingActions = new Map<string, GameAction[]>();
  private timer:       ReturnType<typeof setInterval> | null = null;
  private broadcast:   BroadcastFn;
  private worldTiles:  TileData[][];
  private worldPixels: number[];
  private worldPixelW: number;
  private worldPixelH: number;

  constructor(broadcast: BroadcastFn) {
    this.broadcast = broadcast;

    const mapData = loadWorldMap();
    this.worldTiles  = mapData.tiles;
    this.worldPixels = mapData.pixels;
    this.worldPixelW = mapData.pixelWidth;
    this.worldPixelH = mapData.pixelHeight;

    const world = createWorldFromTiles(mapData.tiles);

    const NPC_CHICKEN_X = 31, NPC_CHICKEN_Y = 38;
    const chickenOffsets = [
      { dx: -1, dy: 0 }, { dx:  1, dy: 2 }, { dx: -2, dy: 3 },
      { dx:  2, dy: 1 }, { dx:  0, dy: 4 },
    ];
    const chickenSpawns = chickenOffsets.map(o =>
      findWalkableTileNear(world, NPC_CHICKEN_X + o.dx, NPC_CHICKEN_Y + o.dy),
    );
    const shopkeeperSpawn = findWalkableTileNear(world, 33, 30);

    // Find chest tile position from map data
    let chestX = PLAYER_START_X - 2, chestY = PLAYER_START_Y - 2;
    outer: for (let ty = 0; ty < world.height; ty++) {
      for (let tx = 0; tx < world.width; tx++) {
        if (world.tiles[ty]?.[tx]?.obstacle === 'chest') { chestX = tx; chestY = ty; break outer; }
      }
    }

    this.state = {
      tick: 0,
      world,
      players: {},
      npcs: [
        ...chickenSpawns.map((s, i) => spawnNPC(`chicken-${i + 1}`, 'chicken', s.x, s.y)),
        spawnNPC('shopkeeper-1', 'shopkeeper', shopkeeperSpawn.x, shopkeeperSpawn.y),
      ],
      droppedItems: [
        { id: 'rack-pickaxe',         itemId: 'pickaxe',         quantity: 1,   tileX: chestX - 2, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-iron-axe',        itemId: 'iron_axe',        quantity: 1,   tileX: chestX - 1, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-basic-chaingun',  itemId: 'basic_chaingun',  quantity: 1,   tileX: chestX,     tileY: chestY + 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-kinetic-charges', itemId: 'kinetic_charges', quantity: 500, tileX: chestX + 1, tileY: chestY + 2, droppedAtTick: 0, permanent: true },
      ],
      pendingRespawns: [],
      messages: {},
      depletedTrees: {},
      treeHealth: {},
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
        chatMessage: '',
        chatMessageTick: -999,
        lastHitTick:    -999,
        lastAttackTick: -999,
        lastChopTick:   -999,
        dying:         false,
        dyingTick:     -999,
        lastRegenTick: -999,
      };
    } else {
      const spawn = findWalkableTileNear(this.state.world, PLAYER_START_X, PLAYER_START_Y);
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

  getWorldTiles():   TileData[][] { return this.worldTiles; }
  getWorldPixels():  number[]    { return this.worldPixels; }
  getPixelWidth():   number      { return this.worldPixelW; }
  getPixelHeight():  number      { return this.worldPixelH; }

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
    dying: false,
    dyingTick: -999,
    lastRegenTick: -999,
  };
}
