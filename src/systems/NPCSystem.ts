import type { NPCState, NPCKind, WorldState, Direction, GridPosition, RespawnEntry } from '../shared/types';
import { findPath } from '../world/Pathfinder';
import { getNPCDef } from '../npcs/NPCRegistry';

const WANDER_RADIUS = 8;
const WAIT_MIN = 20;
const WAIT_MAX = 80;
const WANDER_STEPS_MIN = 2;
const WANDER_STEPS_MAX = 5;

export function spawnNPC(id: string, kind: NPCKind, x: number, y: number): NPCState {
  const def = getNPCDef(kind);
  return {
    id,
    kind,
    tileX: x,
    tileY: y,
    facing: 'south',
    path: [],
    hp: def.maxHp,
    maxHp: def.maxHp,
    homeX: x,
    homeY: y,
    waitTicks: WAIT_MIN,
    dying: false,
    dyingTick: 0,
    lastAttackTick: -999,
    lastHitTick: 0,
    lastHitDamage: 0,
  };
}

// Returns a map from npcId → set of tile keys (key = x*10000+y) it occupies.
// Accounts for sizeX/sizeY: a 2×2 NPC at (3,4) occupies keys for (3,4),(4,4),(3,5),(4,5).
function buildNPCOccupied(npcs: NPCState[]): Map<string, Set<number>> {
  const occupied = new Map<string, Set<number>>();
  for (const npc of npcs) {
    if (npc.dying) continue;
    const def = getNPCDef(npc.kind);
    const tiles = new Set<number>();
    for (let dy = 0; dy < (def.sizeY ?? 1); dy++) {
      for (let dx = 0; dx < (def.sizeX ?? 1); dx++) {
        tiles.add((npc.tileX + dx) * 10000 + (npc.tileY + dy));
      }
    }
    occupied.set(npc.id, tiles);
  }
  return occupied;
}

export function processNPCs(npcs: NPCState[], world: WorldState): NPCState[] {
  const npcOccupied = buildNPCOccupied(npcs);
  return npcs.map(npc => processNPC(npc, world, npcOccupied));
}

export function processRespawns(
  pending: RespawnEntry[],
  npcs: NPCState[],
  world: WorldState,
  tick: number,
): { pending: RespawnEntry[]; npcs: NPCState[] } {
  const stillPending: RespawnEntry[] = [];
  const nextNPCs = [...npcs];

  for (const entry of pending) {
    if (tick < entry.respawnAtTick) { stillPending.push(entry); continue; }
    const spawnTile = world.tiles[entry.homeY]?.[entry.homeX]?.walkable
      ? { x: entry.homeX, y: entry.homeY }
      : findWalkable(world, entry.homeX, entry.homeY);
    nextNPCs.push(spawnNPC(entry.id, entry.kind, spawnTile.x, spawnTile.y));
  }
  return { pending: stillPending, npcs: nextNPCs };
}

function findWalkable(world: WorldState, cx: number, cy: number): GridPosition {
  for (let r = 1; r <= 6; r++) {
    for (let dy = -r; dy <= r; dy++) {
      for (let dx = -r; dx <= r; dx++) {
        const x = cx + dx, y = cy + dy;
        if (x >= 0 && y >= 0 && x < world.width && y < world.height && world.tiles[y][x].walkable) {
          return { x, y };
        }
      }
    }
  }
  return { x: cx, y: cy };
}

function processNPC(npc: NPCState, world: WorldState, npcOccupied: Map<string, Set<number>>): NPCState {
  if (npc.dying) return npc;

  const def = getNPCDef(npc.kind);
  if (def.ai === 'static') return npc;

  // Waiting phase
  if (npc.waitTicks > 0) return { ...npc, waitTicks: npc.waitTicks - 1 };

  // Build the dynamic blocked set: union of all other NPCs' occupied tiles.
  const blocked = new Set<number>();
  for (const [id, tiles] of npcOccupied) {
    if (id === npc.id) continue;
    for (const t of tiles) blocked.add(t);
  }

  // Advance along existing path — revalidate the next step against current
  // positions so NPCs don't walk through each other even mid-path.
  if (npc.path.length > 0) {
    const [next, ...rest] = npc.path;
    const nextKey = next.x * 10000 + next.y;
    if (blocked.has(nextKey)) {
      // Next tile is now occupied — abandon path and wait a tick
      return { ...npc, path: [], waitTicks: 1 };
    }
    const facing = directionBetween({ x: npc.tileX, y: npc.tileY }, next);
    const waitTicks = rest.length === 0 ? randInt(WAIT_MIN, WAIT_MAX) : 0;
    return { ...npc, tileX: next.x, tileY: next.y, facing, path: rest, waitTicks };
  }

  // Plan a new short wander path (blocked set excludes this NPC's own tiles)
  const target = pickWanderTarget(npc, world, blocked);
  if (!target) return { ...npc, waitTicks: WAIT_MIN };

  const fullPath = findPath(world, { x: npc.tileX, y: npc.tileY }, target, blocked);
  if (fullPath.length === 0) return { ...npc, waitTicks: WAIT_MIN };

  const steps = Math.min(fullPath.length, randInt(WANDER_STEPS_MIN, WANDER_STEPS_MAX));
  const [next, ...rest] = fullPath.slice(0, steps);
  const facing = directionBetween({ x: npc.tileX, y: npc.tileY }, next);
  const waitTicks = rest.length === 0 ? randInt(WAIT_MIN, WAIT_MAX) : 0;
  return { ...npc, tileX: next.x, tileY: next.y, facing, path: rest, waitTicks };
}

function pickWanderTarget(npc: NPCState, world: WorldState, blocked: ReadonlySet<number>): GridPosition | null {
  for (let i = 0; i < 8; i++) {
    const dx = randInt(-WANDER_RADIUS, WANDER_RADIUS);
    const dy = randInt(-WANDER_RADIUS, WANDER_RADIUS);
    const x = npc.homeX + dx;
    const y = npc.homeY + dy;
    const k = x * 10000 + y;
    if (x >= 0 && y >= 0 && x < world.width && y < world.height
        && world.tiles[y][x].walkable && !blocked.has(k)) {
      return { x, y };
    }
  }
  return null;
}

function directionBetween(from: GridPosition, to: GridPosition): Direction {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  if (Math.abs(dx) >= Math.abs(dy)) return dx > 0 ? 'east' : 'west';
  return dy > 0 ? 'south' : 'north';
}

function randInt(min: number, max: number): number {
  return Math.floor(Math.random() * (max - min + 1)) + min;
}
