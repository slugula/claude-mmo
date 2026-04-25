import type {
  PlayerState, NPCState, DroppedItemState, GameAction, WorldState, GridPosition, RespawnEntry, Direction,
} from '../shared/types';
import { findPath } from '../world/Pathfinder';
import { getNPCDef } from '../npcs/NPCRegistry';

const PLAYER_ATTACK_SPEED = 12;
const PLAYER_DAMAGE = 1;
const DEATH_TICKS = 5;

export interface CombatResult {
  player: PlayerState;
  npcs: NPCState[];
  droppedItems: DroppedItemState[];
  messages: string[];
  newRespawns: RespawnEntry[];
}

function pos(e: { tileX: number; tileY: number }): GridPosition {
  return { x: e.tileX, y: e.tileY };
}

/** Cardinal (N/S/E/W) adjacency only — used for melee attack range. */
function is4Adjacent(a: GridPosition, b: GridPosition): boolean {
  const dx = Math.abs(a.x - b.x);
  const dy = Math.abs(a.y - b.y);
  return (dx === 1 && dy === 0) || (dx === 0 && dy === 1);
}

function directionTo(from: GridPosition, to: GridPosition): Direction {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  if (Math.abs(dx) >= Math.abs(dy)) return dx > 0 ? 'east' : 'west';
  return dy > 0 ? 'south' : 'north';
}

export function processCombat(
  player: PlayerState,
  npcs: NPCState[],
  droppedItems: DroppedItemState[],
  actions: GameAction[],
  world: WorldState,
  tick: number,
): CombatResult {
  let nextPlayer = { ...player };
  let nextNPCs   = npcs.map(n => ({ ...n }));
  let nextDropped = [...droppedItems];
  const messages: string[] = [];

  // --- New ATTACK_NPC actions ---
  for (const action of actions) {
    if (action.type !== 'ATTACK_NPC') continue;
    const target = nextNPCs.find(n => n.id === action.npcId && !n.dying);
    if (!target) continue;

    const def = getNPCDef(target.kind);
    if (!def.isAttackable) {
      messages.push(`You can\u2019t attack ${def.name}.`);
      continue;
    }

    if (is4Adjacent(pos(nextPlayer), pos(target))) {
      nextPlayer = {
        ...nextPlayer,
        attackTargetId: target.id,
        talkTargetId: null,
        path: [],
        facing: directionTo(pos(nextPlayer), pos(target)),
      };
      continue;
    }

    const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world, true);
    if (!spot) {
      messages.push(`I can\u2019t reach that.`);
      continue;
    }

    nextPlayer = {
      ...nextPlayer,
      attackTargetId: target.id,
      talkTargetId: null,
      path: spot.path,
      destinationX: spot.pos.x,
      destinationY: spot.pos.y,
    };
  }

  // --- Remove NPCs whose death animation is complete; spawn drops; enqueue respawns ---
  const completed = nextNPCs.filter(n => n.dying && tick - n.dyingTick >= DEATH_TICKS);
  const newRespawns: RespawnEntry[] = [];
  for (const npc of completed) {
    const def = getNPCDef(npc.kind);
    for (const drop of def.drops) {
      if (Math.random() > drop.rate) continue;
      nextDropped.push({
        id: `drop-${tick}-${npc.id}-${drop.itemId}`,
        itemId: drop.itemId,
        quantity: drop.quantity,
        tileX: npc.tileX,
        tileY: npc.tileY,
        droppedAtTick: tick,
      });
    }
    if (def.respawnTicks !== undefined) {
      newRespawns.push({
        id: npc.id,
        kind: npc.kind,
        homeX: npc.homeX,
        homeY: npc.homeY,
        respawnAtTick: tick + def.respawnTicks,
      });
    }
  }
  nextNPCs = nextNPCs.filter(n => !(n.dying && tick - n.dyingTick >= DEATH_TICKS));

  // --- Player auto-attack tick ---
  if (nextPlayer.attackTargetId && nextPlayer.path.length === 0) {
    const target = nextNPCs.find(n => n.id === nextPlayer.attackTargetId && !n.dying);

    if (!target) {
      nextPlayer = { ...nextPlayer, attackTargetId: null };
    } else if (is4Adjacent(pos(nextPlayer), pos(target))) {
      // Always face the target while in melee range
      nextPlayer = { ...nextPlayer, facing: directionTo(pos(nextPlayer), pos(target)) };

      if (tick - nextPlayer.lastAttackTick >= PLAYER_ATTACK_SPEED) {
        const def = getNPCDef(target.kind);
        const newHp = target.hp - PLAYER_DAMAGE;
        const idx = nextNPCs.findIndex(n => n.id === target.id);
        if (newHp <= 0) {
          nextNPCs[idx] = { ...target, hp: 0, dying: true, dyingTick: tick };
          nextPlayer = { ...nextPlayer, attackTargetId: null, lastAttackTick: tick };
          messages.push(`You have defeated the ${def.name}.`);
        } else {
          nextNPCs[idx] = { ...target, hp: newHp };
          nextPlayer = { ...nextPlayer, lastAttackTick: tick };
        }
      }
    } else {
      // Not cardinally adjacent — find a cardinal tile and walk there
      const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world, true);
      if (spot) {
        nextPlayer = {
          ...nextPlayer,
          path: spot.path,
          destinationX: spot.pos.x,
          destinationY: spot.pos.y,
        };
      } else {
        nextPlayer = { ...nextPlayer, attackTargetId: null };
      }
    }
  }

  // --- NPC counter-attacks on player ---
  for (let i = 0; i < nextNPCs.length; i++) {
    const npc = nextNPCs[i];
    if (npc.dying) continue;
    if (nextPlayer.attackTargetId !== npc.id) continue;

    const def = getNPCDef(npc.kind);
    if (def.attackSpeedTicks <= 0) continue;

    // Always face the player, regardless of distance or cooldown
    const facingPlayer = directionTo(pos(npc), pos(nextPlayer));
    nextNPCs[i] = { ...npc, facing: facingPlayer };

    if (!is4Adjacent(pos(npc), pos(nextPlayer))) continue;
    if (tick - npc.lastAttackTick < def.attackSpeedTicks) continue;

    const damage = rollDamage(def.attack);
    const newHp = Math.max(0, nextPlayer.hp - damage);
    nextPlayer = {
      ...nextPlayer,
      hp: newHp,
      lastHitTick: tick,
      lastHitDamage: damage,
    };
    nextNPCs[i] = { ...nextNPCs[i], lastAttackTick: tick };
  }

  return { player: nextPlayer, npcs: nextNPCs, droppedItems: nextDropped, messages, newRespawns };
}

// Chicken has attack=1 → max hit=0 → always 0 damage (blue hitsplat).
function rollDamage(attack: number): number {
  const max = Math.floor(attack / 2);
  return Math.floor(Math.random() * (max + 1));
}

export function findReachableAdjacent(
  from: GridPosition,
  target: GridPosition,
  world: WorldState,
  onlyCardinal = false,
): { pos: GridPosition; path: GridPosition[] } | null {
  const offsets = onlyCardinal
    ? [         { dx:  0, dy: -1 },
        { dx: -1, dy:  0 }, { dx: 1, dy: 0 },
                 { dx:  0, dy:  1 }]
    : [
        { dx: -1, dy: -1 }, { dx: 0, dy: -1 }, { dx: 1, dy: -1 },
        { dx: -1, dy:  0 },                     { dx: 1, dy:  0 },
        { dx: -1, dy:  1 }, { dx: 0, dy:  1 }, { dx: 1, dy:  1 },
      ];

  const neighbors: GridPosition[] = [];
  for (const { dx, dy } of offsets) {
    const x = target.x + dx;
    const y = target.y + dy;
    if (x < 0 || y < 0 || x >= world.width || y >= world.height) continue;
    if (!world.tiles[y][x].walkable) continue;
    neighbors.push({ x, y });
  }

  neighbors.sort((a, b) => {
    const da = Math.abs(a.x - from.x) + Math.abs(a.y - from.y);
    const db = Math.abs(b.x - from.x) + Math.abs(b.y - from.y);
    return da - db;
  });

  for (const p of neighbors) {
    if (p.x === from.x && p.y === from.y) return { pos: p, path: [] };
    const path = findPath(world, from, p);
    if (path.length > 0) return { pos: p, path };
  }
  return null;
}
