import type {
  PlayerState, NPCState, DroppedItemState, GameAction, WorldState, GridPosition, RespawnEntry, Direction,
  EquipSlot, ItemStack,
} from '../shared/types';
import { findPath } from '../world/Pathfinder';
import { getNPCDef } from '../npcs/NPCRegistry';
import { getItem } from '../items/ItemRegistry';
import { addXP } from './SkillSystem';

const PLAYER_ATTACK_SPEED = 12;
const DEATH_TICKS = 5;

// ---- Equipment bonus helpers ----

function getEquipBonuses(equipped: Partial<Record<EquipSlot, ItemStack>>): {
  attackBonus: number; strengthBonus: number; defenceBonus: number;
} {
  let atk = 0, str = 0, def = 0;
  for (const stack of Object.values(equipped)) {
    if (!stack) continue;
    const item = getItem(stack.itemId);
    if (!item?.stats) continue;
    atk += item.stats.attackBonus   ?? 0;
    str += item.stats.strengthBonus ?? 0;
    def += item.stats.defenseBonus  ?? 0;
  }
  return { attackBonus: atk, strengthBonus: str, defenceBonus: def };
}

// ---- Attack resolution ----

/**
 * Rolls whether an attack lands.
 * attackRoll and defenceRoll are positive integers (higher = better).
 */
function rollHit(attackRoll: number, defenceRoll: number): boolean {
  return Math.random() < attackRoll / (attackRoll + defenceRoll);
}

/**
 * Computes the player's max hit based on warrior level and equipment strength bonus.
 * Simplified OSRS formula.
 */
function playerMaxHit(warriorLevel: number, strengthBonus: number): number {
  return Math.max(1, Math.floor(0.5 + warriorLevel * (strengthBonus + 64) / 640));
}

/**
 * NPC max hit based on attack stat (OSRS-style: floor(attack / 2)).
 */
function npcMaxHit(attack: number): number {
  return Math.floor(attack / 2);
}

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
        const idx = nextNPCs.findIndex(n => n.id === target.id);

        // --- Accuracy & damage roll ---
        const bonuses      = getEquipBonuses(nextPlayer.equipped);
        const warriorLevel = nextPlayer.skills.warrior?.level ?? 1;
        const attackRoll   = warriorLevel + bonuses.attackBonus + 9;
        const defenceRoll  = def.defense + 9;
        const hit          = rollHit(attackRoll, defenceRoll);
        const maxHit       = playerMaxHit(warriorLevel, bonuses.strengthBonus);
        const damage       = hit ? (Math.floor(Math.random() * maxHit) + 1) : 0;

        // --- Apply to NPC ---
        const newHp = target.hp - damage;
        if (newHp <= 0) {
          nextNPCs[idx] = { ...target, hp: 0, dying: true, dyingTick: tick, lastHitTick: tick, lastHitDamage: damage };
          nextPlayer = { ...nextPlayer, attackTargetId: null, lastAttackTick: tick };
          messages.push(`You have defeated the ${def.name}.`);
        } else {
          nextNPCs[idx] = { ...target, hp: newHp, lastHitTick: tick, lastHitDamage: damage };
          nextPlayer = { ...nextPlayer, lastAttackTick: tick };
        }

        // --- Award Warrior XP: damage dealt × 4 ---
        if (damage > 0) {
          const xpResult = addXP(nextPlayer.skills.warrior, damage * 4);
          const newSkills = { ...nextPlayer.skills, warrior: xpResult.skill };
          nextPlayer = { ...nextPlayer, skills: newSkills };
          if (xpResult.levelsGained > 0) {
            messages.push(`Level up! Warrior is now level ${xpResult.skill.level}.`);
          }
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

    // --- NPC accuracy vs player defence ---
    const playerBonuses = getEquipBonuses(nextPlayer.equipped);
    const defenceLevel  = nextPlayer.skills.defence?.level ?? 1;
    const npcAttackRoll  = def.attack + 9;
    const playerDefRoll  = defenceLevel + playerBonuses.defenceBonus + 9;
    const npcHit         = rollHit(npcAttackRoll, playerDefRoll);
    const maxDmg         = npcMaxHit(def.attack);
    const damage         = npcHit ? (maxDmg > 0 ? Math.floor(Math.random() * maxDmg) + 1 : 0) : 0;

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
