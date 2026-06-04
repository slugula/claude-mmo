import type {
  PlayerState, NPCState, DroppedItemState, GameAction, WorldState, GridPosition, RespawnEntry, Direction,
  EquipSlot, ItemStack,
} from '../shared/types';
import { findPath } from '../world/Pathfinder';
import { getNPCDef } from '../npcs/NPCRegistry';
import { getItem } from '../items/ItemRegistry';
import { addXP } from './SkillSystem';
import { clearActionIntents } from './ActionIntent';
import {
  PLAYER_DEATH_TICKS, PLAYER_REGEN_INTERVAL_TICKS, RESPAWN_X, RESPAWN_Y,
  GUNNER_ATTACK_SPEED, GUNNER_ATTACK_RANGE,
} from '../shared/constants';

const PLAYER_ATTACK_SPEED = 12;
const DEATH_TICKS = 5;

// ---- Equipment bonus helpers ----

function getEquipBonuses(equipped: Partial<Record<EquipSlot, ItemStack>>): {
  meleeAttackBonus:    number; meleeStrengthBonus:  number; meleeDefenseBonus:   number;
  rangedAttackBonus:   number; rangedStrengthBonus: number; rangedDefenseBonus:  number;
} {
  let mAtk = 0, mStr = 0, mDef = 0, rAtk = 0, rStr = 0, rDef = 0;
  for (const stack of Object.values(equipped)) {
    if (!stack) continue;
    const item = getItem(stack.itemId);
    if (!item?.stats) continue;
    mAtk += item.stats.meleeAttackBonus    ?? 0;
    mStr += item.stats.meleeStrengthBonus  ?? 0;
    mDef += item.stats.meleeDefenseBonus   ?? 0;
    rAtk += item.stats.rangedAttackBonus   ?? 0;
    rStr += item.stats.rangedStrengthBonus ?? 0;
    rDef += item.stats.rangedDefenseBonus  ?? 0;
  }
  return {
    meleeAttackBonus: mAtk, meleeStrengthBonus: mStr, meleeDefenseBonus: mDef,
    rangedAttackBonus: rAtk, rangedStrengthBonus: rStr, rangedDefenseBonus: rDef,
  };
}

// ---- Attack resolution ----

function rollHit(attackRoll: number, defenceRoll: number): boolean {
  return Math.random() < attackRoll / (attackRoll + defenceRoll);
}

function meleeMaxHit(warriorLevel: number, meleeStrengthBonus: number): number {
  return Math.max(1, Math.floor(0.5 + warriorLevel * (meleeStrengthBonus + 64) / 640));
}

function gunnerMaxHit(gunnerLevel: number, rangedStrengthBonus: number): number {
  return Math.max(1, Math.floor(0.5 + gunnerLevel * (rangedStrengthBonus + 32) / 1280));
}

function npcMaxHit(strength: number): number {
  return Math.max(1, strength);
}

// ---- Range and line-of-sight helpers ----

function chebyshevDistance(a: GridPosition, b: GridPosition): number {
  return Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y));
}

/**
 * Bresenham's line — checks every intermediate tile (skips endpoints).
 * Returns false if any intermediate tile has blocksRanged: true.
 */
function hasLineOfSight(world: WorldState, from: GridPosition, to: GridPosition): boolean {
  let x0 = from.x, y0 = from.y;
  const x1 = to.x, y1 = to.y;
  const dx = Math.abs(x1 - x0);
  const dy = Math.abs(y1 - y0);
  const sx = x0 < x1 ? 1 : -1;
  const sy = y0 < y1 ? 1 : -1;
  let err = dx - dy;

  while (x0 !== x1 || y0 !== y1) {
    // Advance first, so we skip the source tile
    const e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 < dx)  { err += dx; y0 += sy; }

    // Skip the destination tile itself — it contains the NPC, not an obstacle
    if (x0 === x1 && y0 === y1) break;

    const tile = world.tiles[y0]?.[x0];
    if (tile?.blocksRanged) return false;
  }
  return true;
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

function is4Adjacent(a: GridPosition, b: GridPosition): boolean {
  const dx = Math.abs(a.x - b.x);
  const dy = Math.abs(a.y - b.y);
  return (dx === 1 && dy === 0) || (dx === 0 && dy === 1);
}

export function directionTo(from: GridPosition, to: GridPosition): Direction {
  const dx = Math.sign(to.x - from.x);
  const dy = Math.sign(to.y - from.y);
  if (dx ===  1 && dy === -1) return 'north_east';
  if (dx ===  1 && dy ===  1) return 'south_east';
  if (dx === -1 && dy ===  1) return 'south_west';
  if (dx === -1 && dy === -1) return 'north_west';
  if (dx ===  1) return 'east';
  if (dx === -1) return 'west';
  if (dy === -1) return 'north';
  return 'south';
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

  // --- Resolve dying players: respawn after PLAYER_DEATH_TICKS ---
  if (nextPlayer.dying) {
    if (tick - nextPlayer.dyingTick >= PLAYER_DEATH_TICKS) {
      nextPlayer = {
        ...nextPlayer,
        dying: false,
        dyingTick: -999,
        hp: nextPlayer.maxHp,
        tileX: RESPAWN_X,
        tileY: RESPAWN_Y,
        destinationX: RESPAWN_X,
        destinationY: RESPAWN_Y,
        path: [],
        attackTargetId: null,
        talkTargetId: null,
        chopTargetX: null,
        chopTargetY: null,
        mineTargetX: null,
        mineTargetY: null,
        fishTargetX: null,
        fishTargetY: null,
      };
      messages.push('Your mortal coil has perished.');
    } else {
      // Still in dying animation — skip all combat processing
      return { player: nextPlayer, npcs: nextNPCs, droppedItems: nextDropped, messages, newRespawns: [] };
    }
  }

  // --- New ATTACK_NPC actions ---
  for (const action of actions) {
    if (action.type !== 'ATTACK_NPC') continue;
    const target = nextNPCs.find(n => n.id === action.npcId && !n.dying);
    if (!target) continue;

    const def = getNPCDef(target.kind);
    if (!def.isAttackable) {
      messages.push(`You can’t attack ${def.name}.`);
      continue;
    }

    // First-strike: stamp the NPC lastAttackTick to now so it cannot
    // retaliate until a full attackSpeedTicks has elapsed, giving the
    // player one free action before the enemy combat cycle begins.
    const targetIdx = nextNPCs.findIndex(n => n.id === target.id);
    if (targetIdx !== -1) {
      nextNPCs[targetIdx] = { ...nextNPCs[targetIdx], lastAttackTick: tick };
    }

    // Determine combat style to decide engagement distance
    const rightHandItem = getItem(nextPlayer.equipped.rightHand?.itemId ?? '');
    const combatStyle   = rightHandItem?.combatStyle ?? 'melee';

    if (combatStyle === 'gunner') {
      const dist   = chebyshevDistance(pos(nextPlayer), pos(target));
      const hasLOS = hasLineOfSight(world, pos(nextPlayer), pos(target));
      if (dist <= GUNNER_ATTACK_RANGE && hasLOS) {
        nextPlayer = {
          ...clearActionIntents(nextPlayer),
          attackTargetId: target.id,
          path: [],
          facing: directionTo(pos(nextPlayer), pos(target)),
        };
      } else {
        const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world, true);
        if (!spot) { messages.push(`I can’t reach that.`); continue; }
        nextPlayer = {
          ...clearActionIntents(nextPlayer),
          attackTargetId: target.id,
          path: spot.path,
          destinationX: spot.pos.x,
          destinationY: spot.pos.y,
        };
      }
    } else {
      if (is4Adjacent(pos(nextPlayer), pos(target))) {
        nextPlayer = {
          ...clearActionIntents(nextPlayer),
          attackTargetId: target.id,
          path: [],
          facing: directionTo(pos(nextPlayer), pos(target)),
        };
        continue;
      }
      const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world, true);
      if (!spot) { messages.push(`I can’t reach that.`); continue; }
      nextPlayer = {
        ...clearActionIntents(nextPlayer),
        attackTargetId: target.id,
        path: spot.path,
        destinationX: spot.pos.x,
        destinationY: spot.pos.y,
      };
    }
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
  if (nextPlayer.attackTargetId) {
    const target = nextNPCs.find(n => n.id === nextPlayer.attackTargetId && !n.dying);

    if (!target) {
      nextPlayer = { ...nextPlayer, attackTargetId: null };
    } else {
      const rightHandItem = getItem(nextPlayer.equipped.rightHand?.itemId ?? '');
      const combatStyle   = rightHandItem?.combatStyle ?? 'melee';

      if (combatStyle === 'gunner') {
        // ---- Gunner auto-attack ----
        const dist   = chebyshevDistance(pos(nextPlayer), pos(target));
        const hasLOS = dist <= GUNNER_ATTACK_RANGE && hasLineOfSight(world, pos(nextPlayer), pos(target));

        if (!hasLOS) {
          // Out of range or LOS blocked — repathfind immediately
          if (nextPlayer.path.length === 0) {
            const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world, true);
            if (spot) {
              nextPlayer = { ...nextPlayer, path: spot.path, destinationX: spot.pos.x, destinationY: spot.pos.y };
            } else {
              nextPlayer = { ...nextPlayer, attackTargetId: null };
            }
          }
        } else {
          // In range and LOS clear — stop moving and shoot
          if (nextPlayer.path.length > 0) nextPlayer = { ...nextPlayer, path: [] };
          nextPlayer = { ...nextPlayer, facing: directionTo(pos(nextPlayer), pos(target)) };

          if (tick - nextPlayer.lastAttackTick >= GUNNER_ATTACK_SPEED) {
            // Check ammo
            const ammoStack = nextPlayer.equipped.ammo;
            if (!ammoStack || ammoStack.itemId !== 'kinetic_charges') {
              messages.push('You have run out of Kinetic Charges.');
              nextPlayer = { ...nextPlayer, attackTargetId: null };
            } else {
              const def = getNPCDef(target.kind);
              const idx = nextNPCs.findIndex(n => n.id === target.id);
              const bonuses      = getEquipBonuses(nextPlayer.equipped);
              const gunnerLevel  = nextPlayer.skills.gunner?.level ?? 1;
              const attackRoll   = gunnerLevel + bonuses.rangedAttackBonus + 9;
              const defenceRoll  = def.rangedDefense + 9;
              const hit          = rollHit(attackRoll, defenceRoll);
              const maxHit       = gunnerMaxHit(gunnerLevel, bonuses.rangedStrengthBonus);
              const damage       = hit ? (Math.floor(Math.random() * maxHit) + 1) : 0;

              // Apply to NPC
              const newHp = target.hp - damage;
              if (newHp <= 0) {
                nextNPCs[idx] = { ...target, hp: 0, dying: true, dyingTick: tick, lastHitTick: tick, lastHitDamage: damage };
                nextPlayer = { ...nextPlayer, attackTargetId: null, lastAttackTick: tick };
                messages.push(`You have defeated the ${def.name}.`);
              } else {
                nextNPCs[idx] = { ...target, hp: newHp, lastHitTick: tick, lastHitDamage: damage };
                nextPlayer = { ...nextPlayer, lastAttackTick: tick };
              }

              // Consume one Kinetic Charge
              const newQty = ammoStack.quantity - 1;
              if (newQty <= 0) {
                const { ammo: _ammo, ...restEquipped } = nextPlayer.equipped;
                nextPlayer = { ...nextPlayer, equipped: restEquipped };
                messages.push('You have run out of Kinetic Charges.');
              } else {
                nextPlayer = {
                  ...nextPlayer,
                  equipped: { ...nextPlayer.equipped, ammo: { itemId: 'kinetic_charges', quantity: newQty } },
                };
              }

              // Award XP (only on damage)
              if (damage > 0) {
                const gunnerXp = addXP(nextPlayer.skills.gunner, damage * 4);
                const hpXp     = addXP(nextPlayer.skills.hitpoints, Math.floor(damage * 1.33));
                nextPlayer = {
                  ...nextPlayer,
                  skills: { ...nextPlayer.skills, gunner: gunnerXp.skill, hitpoints: hpXp.skill },
                };
                if (gunnerXp.levelsGained > 0) {
                  messages.push(`Level up! Cowboy is now level ${gunnerXp.skill.level}.`);
                }
                if (hpXp.levelsGained > 0) {
                  nextPlayer = {
                    ...nextPlayer,
                    maxHp: nextPlayer.maxHp + hpXp.levelsGained,
                    hp:    nextPlayer.hp    + hpXp.levelsGained,
                  };
                  messages.push(`Level up! Hitpoints is now level ${hpXp.skill.level}.`);
                }
              }
            }
          }
        }
      } else {
        // ---- Warrior (melee) auto-attack ----
        if (is4Adjacent(pos(nextPlayer), pos(target))) {
          if (nextPlayer.path.length > 0) nextPlayer = { ...nextPlayer, path: [] };
          nextPlayer = { ...nextPlayer, facing: directionTo(pos(nextPlayer), pos(target)) };

          if (tick - nextPlayer.lastAttackTick >= PLAYER_ATTACK_SPEED) {
            const def = getNPCDef(target.kind);
            const idx = nextNPCs.findIndex(n => n.id === target.id);
            const bonuses      = getEquipBonuses(nextPlayer.equipped);
            const warriorLevel = nextPlayer.skills.warrior?.level ?? 1;
            const attackRoll   = warriorLevel + bonuses.meleeAttackBonus + 9;
            const defenceRoll  = def.meleeDefense + 9;
            const hit          = rollHit(attackRoll, defenceRoll);
            const maxHit       = meleeMaxHit(warriorLevel, bonuses.meleeStrengthBonus);
            const damage       = hit ? (Math.floor(Math.random() * maxHit) + 1) : 0;

            const newHp = target.hp - damage;
            if (newHp <= 0) {
              nextNPCs[idx] = { ...target, hp: 0, dying: true, dyingTick: tick, lastHitTick: tick, lastHitDamage: damage };
              nextPlayer = { ...nextPlayer, attackTargetId: null, lastAttackTick: tick };
              messages.push(`You have defeated the ${def.name}.`);
            } else {
              nextNPCs[idx] = { ...target, hp: newHp, lastHitTick: tick, lastHitDamage: damage };
              nextPlayer = { ...nextPlayer, lastAttackTick: tick };
            }

            if (damage > 0) {
              const warriorXp = addXP(nextPlayer.skills.warrior, damage * 4);
              const hpXp      = addXP(nextPlayer.skills.hitpoints, Math.floor(damage * 1.33));
              nextPlayer = {
                ...nextPlayer,
                skills: { ...nextPlayer.skills, warrior: warriorXp.skill, hitpoints: hpXp.skill },
              };
              if (warriorXp.levelsGained > 0) messages.push(`Level up! Warrior is now level ${warriorXp.skill.level}.`);
              if (hpXp.levelsGained > 0) {
                nextPlayer = {
                  ...nextPlayer,
                  maxHp: nextPlayer.maxHp + hpXp.levelsGained,
                  hp:    nextPlayer.hp    + hpXp.levelsGained,
                };
                messages.push(`Level up! Hitpoints is now level ${hpXp.skill.level}.`);
              }
            }
          }
        } else if (nextPlayer.path.length === 0) {
          const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world, true);
          if (spot) {
            nextPlayer = { ...nextPlayer, path: spot.path, destinationX: spot.pos.x, destinationY: spot.pos.y };
          } else {
            nextPlayer = { ...nextPlayer, attackTargetId: null };
          }
        }
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

    // Always face the player
    const facingPlayer = directionTo(pos(npc), pos(nextPlayer));
    nextNPCs[i] = { ...npc, facing: facingPlayer };

    if (!is4Adjacent(pos(npc), pos(nextPlayer))) continue;
    if (tick - npc.lastAttackTick < def.attackSpeedTicks) continue;

    const playerBonuses = getEquipBonuses(nextPlayer.equipped);
    const defenceLevel  = nextPlayer.skills.defence?.level ?? 1;
    const npcAttackRoll  = def.attack + 9;
    const playerDefRoll  = defenceLevel + playerBonuses.meleeDefenseBonus + 9;
    const npcHit         = rollHit(npcAttackRoll, playerDefRoll);
    const maxDmg         = npcMaxHit(def.strength);
    const damage         = npcHit ? Math.floor(Math.random() * maxDmg) + 1 : 0;

    nextNPCs[i] = { ...nextNPCs[i], lastAttackTick: tick };

    const newHp = nextPlayer.hp - damage;
    if (newHp <= 0) {
      nextPlayer = {
        ...nextPlayer,
        hp: 0,
        dying: true,
        dyingTick: tick,
        lastHitTick: tick,
        lastHitDamage: damage,
        attackTargetId: null,
        talkTargetId: null,
        chopTargetX: null,
        chopTargetY: null,
        mineTargetX: null,
        mineTargetY: null,
        fishTargetX: null,
        fishTargetY: null,
        path: [],
      };
    } else {
      nextPlayer = { ...nextPlayer, hp: newHp, lastHitTick: tick, lastHitDamage: damage };
    }
  }

  // --- Passive HP regen ---
  if (
    !nextPlayer.dying &&
    nextPlayer.hp < nextPlayer.maxHp &&
    tick - nextPlayer.lastRegenTick >= PLAYER_REGEN_INTERVAL_TICKS
  ) {
    nextPlayer = {
      ...nextPlayer,
      hp: Math.min(nextPlayer.maxHp, nextPlayer.hp + 1),
      lastRegenTick: tick,
    };
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
