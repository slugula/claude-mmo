import type { PlayerState, WorldState, GameAction, Direction } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { addXP } from './SkillSystem';
import { findReachableAdjacent } from './CombatSystem';
import { clearActionIntents } from './ActionIntent';

const CHOP_INTERVAL = 12;   // ticks between each success roll
const SUCCESS_CHANCE = 0.5; // base success probability per roll
const REGEN_INTERVAL = 5;   // game ticks per 1 health point restored (= 1 real second)

interface TreeDef {
  requiredLevel: number;
  xp: number;
  resourceId: string;
  despawnHealth: number; // ticks of active chopping to deplete
  respawnTicks: number;
}

const TREE_DEFS: Record<string, TreeDef> = {
  tree:        { requiredLevel: 1,  xp: 25,   resourceId: 'logs',        despawnHealth: 50,  respawnTicks: 30  },
  oak_tree:    { requiredLevel: 15, xp: 37.5, resourceId: 'oak_logs',    despawnHealth: 135, respawnTicks: 42  },
  willow_tree: { requiredLevel: 30, xp: 67.5, resourceId: 'willow_logs', despawnHealth: 150, respawnTicks: 42  },
};

export function treeKey(x: number, y: number): string {
  return `${x}-${y}`;
}

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

function findAxe(player: PlayerState): boolean {
  const checkItem = (itemId: string) => {
    const def = getItem(itemId);
    if (def?.toolType !== 'axe') return false;
    const req = def.requirements?.woodcutting ?? 1;
    return player.skills.woodcutting.level >= req;
  };

  if (player.equipped.rightHand && checkItem(player.equipped.rightHand.itemId)) return true;
  return player.inventory.some(slot => slot !== null && checkItem(slot!.itemId));
}

export interface WoodcuttingResult {
  players: Record<string, PlayerState>;
  depletedTrees: Record<string, number>;
  treeHealth: Record<string, number>;
  world: WorldState;
  messages: Record<string, string[]>;
}

export function processWoodcutting(
  players: Record<string, PlayerState>,
  playerActions: Map<string, GameAction[]>,
  world: WorldState,
  depletedTrees: Record<string, number>,
  treeHealth: Record<string, number>,
  tick: number,
): WoodcuttingResult {
  const nextPlayers: Record<string, PlayerState> = { ...players };
  const nextDepleted: Record<string, number> = { ...depletedTrees };
  let nextHealth: Record<string, number> = { ...treeHealth };
  let nextWorld = world;
  const messages: Record<string, string[]> = Object.fromEntries(
    Object.keys(players).map(id => [id, []]),
  );

  const activelyChopped = new Set<string>();

  for (const [playerId, player] of Object.entries(players)) {
    if (player.dying) { nextPlayers[playerId] = player; continue; }
    const actions = playerActions.get(playerId) ?? [];
    let p = { ...player };

    // --- Process new CHOP_TREE actions ---
    for (const action of actions) {
      if (action.type !== 'CHOP_TREE') continue;
      // Starting to chop cancels every other action intent so they can't run
      // concurrently, then sets the chop target.
      p = {
        ...clearActionIntents(p),
        chopTargetX: action.tileX,
        chopTargetY: action.tileY,
      };
    }

    const tx = p.chopTargetX;
    const ty = p.chopTargetY;

    if (tx === null || ty === null) {
      nextPlayers[playerId] = p;
      continue;
    }

    const key = treeKey(tx, ty);

    // Tree already depleted?
    if (nextDepleted[key] !== undefined) {
      messages[playerId].push('That tree has already been cut down.');
      nextPlayers[playerId] = { ...p, chopTargetX: null, chopTargetY: null };
      continue;
    }

    // Verify tile is still a tree
    const tile = world.tiles[ty]?.[tx];
    if (!tile || tile.obstacle !== 'tree') {
      nextPlayers[playerId] = { ...p, chopTargetX: null, chopTargetY: null };
      continue;
    }

    const treeDef = TREE_DEFS['tree']; // all world trees are basic for now

    if (p.skills.woodcutting.level < treeDef.requiredLevel) {
      messages[playerId].push(`You need level ${treeDef.requiredLevel} Woodcutting to cut this.`);
      nextPlayers[playerId] = { ...p, chopTargetX: null, chopTargetY: null };
      continue;
    }

    // Need to walk to the tree?
    if (!is4Adjacent(p.tileX, p.tileY, tx, ty)) {
      if (p.path.length === 0) {
        const spot = findReachableAdjacent(
          { x: p.tileX, y: p.tileY },
          { x: tx, y: ty },
          world,
          true,
        );
        if (spot) {
          // Set facing toward the first path step immediately so the client's
          // turn-suppression fires on the same tick the path is assigned,
          // rather than waiting for MovementSystem to advance one step next tick.
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
          messages[playerId].push("You can't reach that tree.");
          p = { ...p, chopTargetX: null, chopTargetY: null };
        }
      }
      nextPlayers[playerId] = p;
      continue;
    }

    // Adjacent — check for axe
    if (!findAxe(p)) {
      messages[playerId].push('You need an axe to chop this tree.');
      nextPlayers[playerId] = { ...p, chopTargetX: null, chopTargetY: null };
      continue;
    }

    // Check inventory space before starting to chop — prevents the gathering
    // animation from playing and then showing "inventory full" after the fact.
    const freeSlot = p.inventory.findIndex(s => s === null);
    if (freeSlot === -1) {
      messages[playerId].push('Your inventory is too full to hold any more logs.');
      nextPlayers[playerId] = { ...p, chopTargetX: null, chopTargetY: null };
      continue;
    }

    // Face the tree
    p = { ...p, facing: directionTo(p.tileX, p.tileY, tx, ty) };

    // Decrement tree health
    activelyChopped.add(key);
    const currentHealth = nextHealth[key] ?? treeDef.despawnHealth;
    const newHealth = Math.max(0, currentHealth - 1);
    nextHealth[key] = newHealth;

    // Not yet time to roll?
    if (tick - p.lastChopTick < CHOP_INTERVAL) {
      nextPlayers[playerId] = p;
      continue;
    }

    // Success roll
    if (Math.random() >= SUCCESS_CHANCE) {
      nextPlayers[playerId] = { ...p, lastChopTick: tick };
      continue;
    }

    // Grant log + XP
    const newInventory = [...p.inventory];
    newInventory[freeSlot] = { itemId: treeDef.resourceId, quantity: 1 };

    const { skill: newWcSkill, levelsGained } = addXP(p.skills.woodcutting, treeDef.xp);
    const newSkills = { ...p.skills, woodcutting: newWcSkill };

    messages[playerId].push('You get some logs.');
    if (levelsGained > 0) {
      messages[playerId].push(`Congratulations! Your Woodcutting level is now ${newWcSkill.level}.`);
    }

    p = { ...p, inventory: newInventory, skills: newSkills, lastChopTick: tick };

    // Deplete tree if health reached 0
    if (newHealth === 0) {
      nextDepleted[key] = tick + treeDef.respawnTicks;
      delete nextHealth[key];

      // Mark tile as walkable
      const newTiles = nextWorld.tiles.map(row => [...row]);
      newTiles[ty] = [...newTiles[ty]];
      newTiles[ty][tx] = { ...newTiles[ty][tx], walkable: true, obstacle: '', blocksRanged: false };
      nextWorld = { ...nextWorld, tiles: newTiles };

      p = { ...p, chopTargetX: null, chopTargetY: null };
      messages[playerId].push('The tree falls to the ground.');
    }

    nextPlayers[playerId] = p;
  }

  // Regen health for trees not actively chopped this tick
  if (tick % REGEN_INTERVAL === 0) {
    const nextHealthCopy: Record<string, number> = {};
    for (const [key, health] of Object.entries(nextHealth)) {
      if (activelyChopped.has(key)) {
        nextHealthCopy[key] = health;
      } else {
        const [xs, ys] = key.split('-');
        const x = parseInt(xs, 10);
        const y = parseInt(ys, 10);
        const tile = world.tiles[y]?.[x];
        if (tile?.obstacle === 'tree') {
          const treeDef = TREE_DEFS['tree'];
          const restored = Math.min(treeDef.despawnHealth, health + 1);
          if (restored < treeDef.despawnHealth) {
            nextHealthCopy[key] = restored;
          }
          // If fully restored, drop from tracking (full health = key absent)
        } else {
          nextHealthCopy[key] = health;
        }
      }
    }
    nextHealth = nextHealthCopy;
  }

  // Respawn depleted trees whose timer has expired
  for (const [key, respawnAtTick] of Object.entries(nextDepleted)) {
    if (tick < respawnAtTick) continue;
    const [xs, ys] = key.split('-');
    const x = parseInt(xs, 10);
    const y = parseInt(ys, 10);

    const newTiles = nextWorld.tiles.map(row => [...row]);
    newTiles[y] = [...newTiles[y]];
    newTiles[y][x] = { ...newTiles[y][x], walkable: false, obstacle: 'tree', blocksRanged: true };
    nextWorld = { ...nextWorld, tiles: newTiles };

    delete nextDepleted[key];
  }

  return {
    players: nextPlayers,
    depletedTrees: nextDepleted,
    treeHealth: nextHealth,
    world: nextWorld,
    messages,
  };
}
