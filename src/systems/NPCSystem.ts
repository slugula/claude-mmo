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

export function processNPCs(npcs: NPCState[], world: WorldState): NPCState[] {
  return npcs.map(npc => processNPC(npc, world));
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

function processNPC(npc: NPCState, world: WorldState): NPCState {
  if (npc.dying) return npc;

  const def = getNPCDef(npc.kind);
  if (def.ai === 'static') return npc;

  // Waiting phase
  if (npc.waitTicks > 0) return { ...npc, waitTicks: npc.waitTicks - 1 };

  // Advance along existing path
  if (npc.path.length > 0) {
    const [next, ...rest] = npc.path;
    const facing = directionBetween({ x: npc.tileX, y: npc.tileY }, next);
    const waitTicks = rest.length === 0 ? randInt(WAIT_MIN, WAIT_MAX) : 0;
    return { ...npc, tileX: next.x, tileY: next.y, facing, path: rest, waitTicks };
  }

  // Plan a new short wander path
  const target = pickWanderTarget(npc, world);
  if (!target) return { ...npc, waitTicks: WAIT_MIN };

  const fullPath = findPath(world, { x: npc.tileX, y: npc.tileY }, target);
  if (fullPath.length === 0) return { ...npc, waitTicks: WAIT_MIN };

  const steps = Math.min(fullPath.length, randInt(WANDER_STEPS_MIN, WANDER_STEPS_MAX));
  const [next, ...rest] = fullPath.slice(0, steps);
  const facing = directionBetween({ x: npc.tileX, y: npc.tileY }, next);
  const waitTicks = rest.length === 0 ? randInt(WAIT_MIN, WAIT_MAX) : 0;
  return { ...npc, tileX: next.x, tileY: next.y, facing, path: rest, waitTicks };
}

function pickWanderTarget(npc: NPCState, world: WorldState): GridPosition | null {
  for (let i = 0; i < 8; i++) {
    const dx = randInt(-WANDER_RADIUS, WANDER_RADIUS);
    const dy = randInt(-WANDER_RADIUS, WANDER_RADIUS);
    const x = npc.homeX + dx;
    const y = npc.homeY + dy;
    if (x >= 0 && y >= 0 && x < world.width && y < world.height && world.tiles[y][x].walkable) {
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
