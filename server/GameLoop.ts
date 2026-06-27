import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { loadWorldManifest, assembleWorld } from './world/assembleWorld';
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
  // OSRS-style shaped surface layer (v3+). Relayed to the client for rendering.
  overlayTiles?: { tileX: number; tileY: number; shape: number; materialId: number; rotation?: number }[];
  // Wall + pillar edge features — relayed to the client for rendering.
  walls?: { tileX: number; tileY: number; orient: number; pillar: boolean; objectId: string; length?: number }[];
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
  private lastTickAt = 0;
  private broadcast: BroadcastFn;
  private worldTiles: TileData[][];
  private waterTiles: { tileX: number; tileY: number }[] = [];
  private overlayTiles: { tileX: number; tileY: number; shape: number; materialId: number; rotation?: number }[] = [];
  private walls: { tileX: number; tileY: number; orient: number; pillar: boolean; objectId: string; length?: number }[] = [];
  // Manifest spawn (global tile coords) when a multi-chunk world is loaded;
  // null in legacy single-map mode (falls back to PLAYER_START_X/Y clamping).
  private worldSpawn: { x: number; y: number } | null = null;
  // Chest positions collected at load (assembly) or by the legacy scan.
  private chestTiles: { x: number; y: number }[] = [];
  // Multi-chunk streaming metadata. chunkSize > 0 and assignedChunks non-empty
  // only when a world.json manifest is loaded; otherwise the server uses the
  // legacy whole-map init (no streaming).
  private chunkSize = 0;
  private assignedChunks = new Set<string>();   // "cx,cy" cells with map data
  private chunkMusic: Record<string, string> = {};   // "cx,cy" -> music file

  constructor(broadcast: BroadcastFn) {
    this.broadcast = broadcast;

    // Fire-and-forget DB load: JSON registries are already populated synchronously;
    // this hot-swaps them with DB data if available.
    void loadEntitiesFromDB();

    // Multi-chunk overworld: when public/maps/world.json exists, assemble the
    // referenced chunk maps into one global world. Otherwise load the single
    // worldMap.json exactly as before.
    const manifestPath = join(__dirname, '../public/maps/world.json');
    const manifest = loadWorldManifest(manifestPath);

    interface LoadedWorld {
      tiles: TileData[][];
      vertexHeights?: number[];
      npcSpawns?: NPCSpawn[];
      permanentItems?: PermanentItemSpawn[];
      waterTiles?: { tileX: number; tileY: number }[];
      overlayTiles?: { tileX: number; tileY: number; shape: number; materialId: number; rotation?: number }[];
      walls?: { tileX: number; tileY: number; orient: number; pillar: boolean; objectId: string; length?: number }[];
    }
    let mapData: LoadedWorld;
    if (manifest) {
      const assembled = assembleWorld(manifestPath, manifest);
      mapData = assembled;
      this.worldSpawn = assembled.spawn;
      this.chestTiles = assembled.chests;
      this.chunkSize = manifest.chunkSize;
      for (const c of manifest.chunks) {
        this.assignedChunks.add(`${c.cx},${c.cy}`);
        if (c.music) this.chunkMusic[`${c.cx},${c.cy}`] = c.music;
      }
    } else {
      mapData = loadWorldMap();
    }
    this.worldTiles = mapData.tiles;
    this.waterTiles = mapData.waterTiles ?? [];
    // Overlay layer (v3+). Legacy maps only have waterTiles — migrate each into
    // a full-tile (shape 0) water overlay so the client has one source of truth.
    // kWaterMaterialId == 3 (mirror of shared::kWaterMaterialId in C++).
    this.overlayTiles = mapData.overlayTiles ?? (mapData.waterTiles ?? []).map(
      w => ({ tileX: w.tileX, tileY: w.tileY, shape: 0, materialId: 3 }),
    );
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
      // Find chest tile for legacy rack placement. Assembly collects chest
      // positions during the chunk copy; the tile scan only runs in legacy
      // single-map mode where the world is small.
      let chestX = Math.max(0, Math.floor(world.width / 2) - 4);
      let chestY = Math.max(0, Math.floor(world.height / 2) - 4);
      if (this.chestTiles.length > 0) {
        chestX = this.chestTiles[0].x;
        chestY = this.chestTiles[0].y;
      } else if (!manifest) {
        outer: for (let ty = 0; ty < world.height; ty++) {
          for (let tx = 0; tx < world.width; tx++) {
            if (world.tiles[ty]?.[tx]?.obstacle === 'chest') { chestX = tx; chestY = ty; break outer; }
          }
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
        useTargetX: null,
        useTargetY: null,
        eatUntilTick: -999,
        activeToolItemId: '',
        lastLevelUpTick: -999,
        chatMessage: '',
        chatMessageTick: -999,
        lastHitTick:    -999,
        lastAttackTick: -999,
        lastChopTick:   -999,
        lastMineTick:   -999,
        lastFishTick:   -999,
        lastProduceTick: -999,
        dying:         false,
        dyingTick:     -999,
        lastRegenTick: -999,
      };
      // World-layout safety: if the saved position is out of bounds or lands on
      // a non-walkable tile (e.g. a chunk was moved/erased in the manifest since
      // last login, leaving the spot in void), relocate to the world spawn.
      const w = this.state.world;
      const t = w.tiles[player.tileY]?.[player.tileX];
      if (!t || !t.walkable) {
        const home = this.defaultSpawn();
        console.warn(`[GameLoop] saved position (${player.tileX},${player.tileY}) for ${name} ` +
                     `is void/non-walkable — relocating to spawn (${home.x},${home.y})`);
        player = {
          ...player,
          tileX: home.x, tileY: home.y,
          destinationX: home.x, destinationY: home.y,
        };
      }
    } else {
      const spawn = this.defaultSpawn();
      player = createInitialPlayer(spawn.x, spawn.y, name);
    }

    this.state = {
      ...this.state,
      players: { ...this.state.players, [playerId]: player },
    };
    this.pendingActions.set(playerId, []);
  }

  /** Walkable spawn tile: the manifest's world spawn in multi-chunk mode, or
   *  the legacy PLAYER_START constants clamped to the map. */
  private defaultSpawn(): { x: number; y: number } {
    const startX = this.worldSpawn?.x ?? Math.min(PLAYER_START_X, this.state.world.width - 1);
    const startY = this.worldSpawn?.y ?? Math.min(PLAYER_START_Y, this.state.world.height - 1);
    return findWalkableTileNear(this.state.world, startX, startY);
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
  getOverlayTiles() { return this.overlayTiles; }
  getWalls() { return this.walls; }
  getVertexHeights(): number[] { return Array.from(this.state.world.vertexHeights); }

  // ---- Multi-chunk streaming ------------------------------------------------
  // Streaming is active only when a world.json manifest was loaded.
  isStreaming(): boolean { return this.chunkSize > 0; }
  getChunkSize(): number { return this.chunkSize; }
  // Per-chunk music: { "cx,cy": "song.ogg" } from the world manifest.
  getChunkMusic(): Record<string, string> { return this.chunkMusic; }
  getWorldDims(): { width: number; height: number } {
    return { width: this.state.world.width, height: this.state.world.height };
  }
  getSpawn(): { x: number; y: number } {
    return this.worldSpawn ?? this.defaultSpawn();
  }
  /** Cells (in Chebyshev chunk-distance ≤ radius of (pcx,pcy)) that have map
   *  data assigned — i.e. are worth streaming. */
  chunksInRange(pcx: number, pcy: number, radius: number): { cx: number; cy: number }[] {
    const out: { cx: number; cy: number }[] = [];
    for (let cy = pcy - radius; cy <= pcy + radius; cy++) {
      for (let cx = pcx - radius; cx <= pcx + radius; cx++) {
        if (cx < 0 || cy < 0) continue;
        if (this.assignedChunks.has(`${cx},${cy}`)) out.push({ cx, cy });
      }
    }
    return out;
  }

  /** Slices the assembled flat world for one chunk cell. Coordinates are GLOBAL
   *  (already-assembled, flip-correct) so the client writes them straight back
   *  at the same indices. */
  getChunkData(cx: number, cy: number): {
    cx: number; cy: number; gx0: number; gy0: number; w: number; h: number;
    tiles: TileData[][];
    vrow0: number; vcol0: number; vrows: number; vcols: number; vh: number[];
    walls: typeof GameLoop.prototype.walls;
    overlayTiles: typeof GameLoop.prototype.overlayTiles;
  } | null {
    const S = this.chunkSize;
    if (S <= 0) return null;
    const W = this.state.world.width, H = this.state.world.height;
    const gx0 = cx * S, gy0 = cy * S;
    if (gx0 >= W || gy0 >= H) return null;
    const w = Math.min(S, W - gx0), h = Math.min(S, H - gy0);

    const tiles: TileData[][] = [];
    for (let y = 0; y < h; y++) tiles.push(this.worldTiles[gy0 + y].slice(gx0, gx0 + w));

    // Vertex heights span the flipped band [H-(cy+1)*S .. H-cy*S] × [gx0 .. gx0+w].
    const vh = this.state.world.vertexHeights;
    const vrow0 = Math.max(0, H - (cy + 1) * S);
    const vrow1 = H - cy * S;                 // inclusive bottom edge of band
    const vrows = vrow1 - vrow0 + 1;
    const vcol0 = gx0, vcols = w + 1;
    const sub: number[] = [];
    for (let r = 0; r < vrows; r++)
      for (let c = 0; c < vcols; c++)
        sub.push(vh[(vrow0 + r) * (W + 1) + (vcol0 + c)] ?? 0);

    const inRect = (tx: number, ty: number) =>
      tx >= gx0 && tx < gx0 + w && ty >= gy0 && ty < gy0 + h;
    const walls = this.walls.filter(wl => inRect(wl.tileX, wl.tileY));
    const overlayTiles = this.overlayTiles.filter(o => inRect(o.tileX, o.tileY));

    return { cx, cy, gx0, gy0, w, h, tiles, vrow0, vcol0, vrows, vcols, vh: sub, walls, overlayTiles };
  }

  /** Returns a snapshot of all currently-connected players for checkpoint saves. */
  getPlayerStates(): Map<string, PlayerState> {
    const result = new Map<string, PlayerState>();
    for (const [id, player] of Object.entries(this.state.players)) {
      result.set(id, player);
    }
    return result;
  }

  private tick(): void {
    // Detect inter-tick gaps: if the previous tick (or anything else on the
    // event loop) blocked, the gap since the last tick will exceed the interval.
    const now = Date.now();
    if (this.lastTickAt !== 0) {
      const gap = now - this.lastTickAt;
      if (gap > TICK_DURATION_MS * 3) {
        console.warn(`[GameLoop] event-loop stall: ${gap}ms since last tick ` +
                     `(expected ~${TICK_DURATION_MS}ms)`);
      }
    }
    this.lastTickAt = now;

    const playerActions = new Map<string, GameAction[]>();
    for (const [id, actions] of this.pendingActions) {
      playerActions.set(id, actions.splice(0));
    }

    // A throw here must NOT kill the loop or corrupt state: log it (with stack)
    // and skip the tick, keeping the last good state, so the server stays up and
    // the cause is visible instead of silently wedging.
    let next: GameState;
    try {
      next = processTick(this.state, playerActions);
    } catch (err) {
      console.error('[GameLoop] processTick threw — skipping tick:', err);
      return;
    }
    this.state = next;

    const elapsed = Date.now() - now;
    if (elapsed > TICK_DURATION_MS) {
      console.warn(`[GameLoop] slow tick: ${elapsed}ms (tick ${this.state.tick})`);
    }

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
    useTargetX: null,
    useTargetY: null,
    lastProduceTick: -999,
    eatUntilTick: -999,
    activeToolItemId: '',
    lastLevelUpTick: -999,
    dying: false,
    dyingTick: -999,
    lastRegenTick: -999,
  };
}
