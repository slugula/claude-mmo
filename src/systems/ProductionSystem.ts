import type { PlayerState, WorldState, GameAction, Direction } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { addXP } from './SkillSystem';
import { findReachableAdjacent } from './CombatSystem';
import { clearActionIntents } from './ActionIntent';
import { getRecipesForFacility, successChance, type ProductionRecipe } from '../production/RecipeRegistry';
import { tunable } from '../config/Tunables';

// Production facilities (Preparation Table, Cooking Range, future furnace/anvil).
// The player stands adjacent and, every produce_interval ticks, converts one
// matching input item in their inventory into an output (or a fail item) and
// gains skill XP. Generic + data-driven via recipe_definitions — adding a new
// production skill is just new recipes + a facility object, no new system.

function directionTo(fx: number, fy: number, tx: number, ty: number): Direction {
  const dx = Math.sign(tx - fx);
  const dy = Math.sign(ty - fy);
  if (dx ===  1 && dy === -1) return 'north_east';
  if (dx ===  1 && dy ===  1) return 'south_east';
  if (dx === -1 && dy ===  1) return 'south_west';
  if (dx === -1 && dy === -1) return 'north_west';
  if (dx ===  1) return 'east';
  if (dx === -1) return 'west';
  if (dy === -1) return 'north';
  return 'south';
}

function is4Adjacent(ax: number, ay: number, bx: number, by: number): boolean {
  const dx = Math.abs(ax - bx);
  const dy = Math.abs(ay - by);
  return (dx === 1 && dy === 0) || (dx === 0 && dy === 1);
}

function countItem(player: PlayerState, itemId: string): number {
  let n = 0;
  for (const slot of player.inventory) if (slot && slot.itemId === itemId) n += slot.quantity;
  return n;
}

// Pick the first recipe at this facility the player can act on: has the input
// item and meets the level requirement. (One recipe per facility for now, but
// this keeps the door open for multi-input stations.)
function pickRecipe(facilityId: string, player: PlayerState): ProductionRecipe | null {
  const recipes = getRecipesForFacility(facilityId);
  for (const r of recipes) {
    if (player.skills[r.skill]?.level < r.requiredLevel) continue;
    if (countItem(player, r.inputItemId) < r.inputQty) continue;
    return r;
  }
  // Surface a level-gated recipe (has input but level too low) so we can warn.
  for (const r of recipes) {
    if (countItem(player, r.inputItemId) >= r.inputQty) return r;
  }
  return null;
}

// Remove `qty` of an item from the inventory (first matching slots). Returns a
// new inventory array; assumes the caller verified enough is present.
function removeItem(inv: (PlayerState['inventory'][number])[], itemId: string, qty: number): typeof inv {
  const out = [...inv];
  let remaining = qty;
  for (let i = 0; i < out.length && remaining > 0; i++) {
    const slot = out[i];
    if (!slot || slot.itemId !== itemId) continue;
    const take = Math.min(slot.quantity, remaining);
    remaining -= take;
    const left = slot.quantity - take;
    out[i] = left > 0 ? { ...slot, quantity: left } : null;
  }
  return out;
}

// Add `qty` of an item: stack onto an existing stackable slot, else first empty
// slot. Returns null if there's no room.
function addItem(inv: (PlayerState['inventory'][number])[], itemId: string, qty: number): (typeof inv) | null {
  const def = getItem(itemId);
  const out = [...inv];
  if (def?.stackable) {
    const idx = out.findIndex(s => s && s.itemId === itemId);
    if (idx !== -1) { out[idx] = { ...out[idx]!, quantity: out[idx]!.quantity + qty }; return out; }
  }
  const empty = out.findIndex(s => s === null);
  if (empty === -1) return null;
  out[empty] = { itemId, quantity: qty };
  return out;
}

export interface ProductionResult {
  players: Record<string, PlayerState>;
  messages: Record<string, string[]>;
}

export function processProduction(
  players: Record<string, PlayerState>,
  playerActions: Map<string, GameAction[]>,
  world: WorldState,
  tick: number,
): ProductionResult {
  const nextPlayers: Record<string, PlayerState> = { ...players };
  const messages: Record<string, string[]> = Object.fromEntries(
    Object.keys(players).map(id => [id, []]),
  );

  for (const [playerId, player] of Object.entries(players)) {
    if (player.dying) { nextPlayers[playerId] = player; continue; }
    const actions = playerActions.get(playerId) ?? [];
    let p = { ...player };

    // --- Process new USE_FACILITY actions ---
    for (const action of actions) {
      if (action.type !== 'USE_FACILITY') continue;
      p = {
        ...clearActionIntents(p),
        useTargetX: action.tileX,
        useTargetY: action.tileY,
      };
    }

    const tx = p.useTargetX;
    const ty = p.useTargetY;
    if (tx === null || ty === null) { nextPlayers[playerId] = p; continue; }

    // Verify the tile still holds a facility with at least one recipe.
    const tile = world.tiles[ty]?.[tx];
    const facilityId = tile?.obstacle ?? '';
    if (!facilityId || getRecipesForFacility(facilityId).length === 0) {
      nextPlayers[playerId] = { ...p, useTargetX: null, useTargetY: null };
      continue;
    }

    // Walk to the facility if not adjacent yet.
    if (!is4Adjacent(p.tileX, p.tileY, tx, ty)) {
      if (p.path.length === 0) {
        const spot = findReachableAdjacent({ x: p.tileX, y: p.tileY }, { x: tx, y: ty }, world, true);
        if (spot) {
          const firstStep = spot.path[0];
          const initialFacing = firstStep
            ? directionTo(p.tileX, p.tileY, firstStep.x, firstStep.y)
            : p.facing;
          p = { ...p, path: spot.path, destinationX: spot.pos.x, destinationY: spot.pos.y, facing: initialFacing };
        } else {
          messages[playerId].push("You can't reach that.");
          p = { ...p, useTargetX: null, useTargetY: null };
        }
      }
      nextPlayers[playerId] = p;
      continue;
    }

    // Face the facility.
    p = { ...p, facing: directionTo(p.tileX, p.tileY, tx, ty) };

    // Busy eating — keep the intent but don't act this tick.
    if (tick < p.eatUntilTick) { nextPlayers[playerId] = p; continue; }

    const recipe = pickRecipe(facilityId, p);
    if (!recipe) {
      messages[playerId].push('You have nothing to process here.');
      nextPlayers[playerId] = { ...p, useTargetX: null, useTargetY: null };
      continue;
    }

    // Level gate (recipe surfaced but level too low).
    if (p.skills[recipe.skill].level < recipe.requiredLevel) {
      const skillName = recipe.skill.charAt(0).toUpperCase() + recipe.skill.slice(1);
      messages[playerId].push(`You need level ${recipe.requiredLevel} ${skillName} to make that.`);
      nextPlayers[playerId] = { ...p, useTargetX: null, useTargetY: null };
      continue;
    }

    // Out of input entirely — stop.
    if (countItem(p, recipe.inputItemId) < recipe.inputQty) {
      nextPlayers[playerId] = { ...p, useTargetX: null, useTargetY: null };
      continue;
    }

    // Not yet time for the next attempt.
    if (tick - p.lastProduceTick < tunable('produce_interval')) { nextPlayers[playerId] = p; continue; }

    // Resolve success/fail and the resulting item.
    const success = Math.random() < successChance(recipe, p.skills[recipe.skill].level);
    const resultItemId = success ? recipe.outputItemId : (recipe.failItemId ?? recipe.outputItemId);
    const resultQty    = success ? recipe.outputQty : 1;

    // Consume input, then add the result (a freed input slot guarantees room).
    const afterConsume = removeItem(p.inventory, recipe.inputItemId, recipe.inputQty);
    const afterAdd = addItem(afterConsume, resultItemId, resultQty);
    if (afterAdd === null) {
      messages[playerId].push('Your inventory is too full.');
      nextPlayers[playerId] = { ...p, useTargetX: null, useTargetY: null };
      continue;
    }

    const { skill: newSkill, levelsGained } = addXP(p.skills[recipe.skill], recipe.xp);
    const newSkills = { ...p.skills, [recipe.skill]: newSkill };

    const inItem  = getItem(recipe.inputItemId);
    const outItem = getItem(resultItemId);
    const outName = (outItem?.name ?? resultItemId).toLowerCase();
    if (success) messages[playerId].push(`You make ${outName}.`);
    else         messages[playerId].push(`You accidentally ruin ${(inItem?.name ?? 'it').toLowerCase()}, making ${outName}.`);
    if (levelsGained > 0) {
      const skillName = recipe.skill.charAt(0).toUpperCase() + recipe.skill.slice(1);
      messages[playerId].push(`Congratulations! Your ${skillName} level is now ${newSkill.level}.`);
    }

    p = { ...p, inventory: afterAdd, skills: newSkills, lastProduceTick: tick };
    nextPlayers[playerId] = p;
  }

  return { players: nextPlayers, messages };
}
