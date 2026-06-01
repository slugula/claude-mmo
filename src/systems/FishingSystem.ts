import type { PlayerState, WorldState, GameAction, Direction } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { addXP } from './SkillSystem';
import { findReachableAdjacent } from './CombatSystem';
import { clearActionIntents } from './ActionIntent';

// Fishing spots are inexhaustible (no depletion/respawn) — simpler than
// woodcutting/mining. Stand adjacent, roll on an interval, get a fish + XP.
const FISH_INTERVAL = 12;    // ticks between each success roll
const SUCCESS_CHANCE = 0.5;  // base success probability per roll

interface FishDef {
  requiredLevel: number;
  xp: number;
  resourceId: string;
}

// One fishing-spot type for now — yields raw shrimp.
const FISH_DEFS: Record<string, FishDef> = {
  fishing_spot: { requiredLevel: 1, xp: 10, resourceId: 'raw_shrimp' },
};

function directionTo(
  fx: number, fy: number, tx: number, ty: number,
): Direction {
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

function findFishingRod(player: PlayerState): boolean {
  const checkItem = (itemId: string) => {
    const def = getItem(itemId);
    if (def?.toolType !== 'fishing_rod') return false;
    const req = def.requirements?.fishing ?? 1;
    return player.skills.fishing.level >= req;
  };

  if (player.equipped.rightHand && checkItem(player.equipped.rightHand.itemId)) return true;
  return player.inventory.some(slot => slot !== null && checkItem(slot!.itemId));
}

export interface FishingResult {
  players: Record<string, PlayerState>;
  messages: Record<string, string[]>;
}

export function processFishing(
  players: Record<string, PlayerState>,
  playerActions: Map<string, GameAction[]>,
  world: WorldState,
  tick: number,
): FishingResult {
  const nextPlayers: Record<string, PlayerState> = { ...players };
  const messages: Record<string, string[]> = Object.fromEntries(
    Object.keys(players).map(id => [id, []]),
  );

  for (const [playerId, player] of Object.entries(players)) {
    if (player.dying) { nextPlayers[playerId] = player; continue; }
    const actions = playerActions.get(playerId) ?? [];
    let p = { ...player };

    // --- Process new FISH actions ---
    for (const action of actions) {
      if (action.type !== 'FISH') continue;
      // Starting to fish cancels every other action intent (attack, chop, mine,
      // talk, pickup) so they can't run concurrently, then sets the fish target.
      p = {
        ...clearActionIntents(p),
        fishTargetX: action.tileX,
        fishTargetY: action.tileY,
      };
    }

    const tx = p.fishTargetX;
    const ty = p.fishTargetY;

    if (tx === null || ty === null) {
      nextPlayers[playerId] = p;
      continue;
    }

    // Verify tile is still a fishing spot
    const tile = world.tiles[ty]?.[tx];
    if (!tile || tile.obstacle !== 'fishing_spot') {
      nextPlayers[playerId] = { ...p, fishTargetX: null, fishTargetY: null };
      continue;
    }

    const fishDef = FISH_DEFS['fishing_spot'];

    if (p.skills.fishing.level < fishDef.requiredLevel) {
      messages[playerId].push(`You need level ${fishDef.requiredLevel} Fishing to fish here.`);
      nextPlayers[playerId] = { ...p, fishTargetX: null, fishTargetY: null };
      continue;
    }

    // Need to walk to the spot?
    if (!is4Adjacent(p.tileX, p.tileY, tx, ty)) {
      if (p.path.length === 0) {
        const spot = findReachableAdjacent(
          { x: p.tileX, y: p.tileY },
          { x: tx, y: ty },
          world,
          true,
        );
        if (spot) {
          const firstStep = spot.path[0];
          const initialFacing = firstStep
            ? directionTo(p.tileX, p.tileY, firstStep.x, firstStep.y)
            : p.facing;
          p = {
            ...p,
            path: spot.path,
            destinationX: spot.pos.x,
            destinationY: spot.pos.y,
            facing: initialFacing,
          };
        } else {
          messages[playerId].push("You can't reach that fishing spot.");
          p = { ...p, fishTargetX: null, fishTargetY: null };
        }
      }
      nextPlayers[playerId] = p;
      continue;
    }

    // Adjacent — check for a fishing rod
    if (!findFishingRod(p)) {
      messages[playerId].push('You need a fishing rod to fish here.');
      nextPlayers[playerId] = { ...p, fishTargetX: null, fishTargetY: null };
      continue;
    }

    // Check inventory space before starting.
    const freeSlot = p.inventory.findIndex(s => s === null);
    if (freeSlot === -1) {
      messages[playerId].push('Your inventory is too full to hold any more fish.');
      nextPlayers[playerId] = { ...p, fishTargetX: null, fishTargetY: null };
      continue;
    }

    // Face the spot
    p = { ...p, facing: directionTo(p.tileX, p.tileY, tx, ty) };

    // Not yet time to roll?
    if (tick - p.lastFishTick < FISH_INTERVAL) {
      nextPlayers[playerId] = p;
      continue;
    }

    // Success roll
    if (Math.random() >= SUCCESS_CHANCE) {
      nextPlayers[playerId] = { ...p, lastFishTick: tick };
      continue;
    }

    // Grant fish + XP
    const newInventory = [...p.inventory];
    newInventory[freeSlot] = { itemId: fishDef.resourceId, quantity: 1 };

    const { skill: newFishingSkill, levelsGained } = addXP(p.skills.fishing, fishDef.xp);
    const newSkills = { ...p.skills, fishing: newFishingSkill };

    messages[playerId].push('You catch some shrimp.');
    if (levelsGained > 0) {
      messages[playerId].push(`Congratulations! Your Fishing level is now ${newFishingSkill.level}.`);
    }

    p = { ...p, inventory: newInventory, skills: newSkills, lastFishTick: tick };
    nextPlayers[playerId] = p;
  }

  return { players: nextPlayers, messages };
}
