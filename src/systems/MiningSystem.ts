import type { PlayerState, WorldState, GameAction, Direction } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { addXP } from './SkillSystem';
import { findReachableAdjacent } from './CombatSystem';

// Mirrors WoodcuttingSystem: rocks deplete with active mining, then respawn.
const MINE_INTERVAL = 12;    // ticks between each success roll
const SUCCESS_CHANCE = 0.5;  // base success probability per roll
const REGEN_INTERVAL = 5;    // game ticks per 1 health point restored

interface RockDef {
  requiredLevel: number;
  xp: number;
  resourceId: string;
  despawnHealth: number; // ticks of active mining to deplete
  respawnTicks: number;
}

// One rock type for now — all world rocks yield copper ore.
const ROCK_DEFS: Record<string, RockDef> = {
  rock: { requiredLevel: 1, xp: 17.5, resourceId: 'copper_ore', despawnHealth: 50, respawnTicks: 30 },
};

export function rockKey(x: number, y: number): string {
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

function findPickaxe(player: PlayerState): boolean {
  const checkItem = (itemId: string) => {
    const def = getItem(itemId);
    if (def?.toolType !== 'pickaxe') return false;
    const req = def.requirements?.mining ?? 1;
    return player.skills.mining.level >= req;
  };

  if (player.equipped.rightHand && checkItem(player.equipped.rightHand.itemId)) return true;
  return player.inventory.some(slot => slot !== null && checkItem(slot!.itemId));
}

export interface MiningResult {
  players: Record<string, PlayerState>;
  depletedRocks: Record<string, number>;
  rockHealth: Record<string, number>;
  world: WorldState;
  messages: Record<string, string[]>;
}

export function processMining(
  players: Record<string, PlayerState>,
  playerActions: Map<string, GameAction[]>,
  world: WorldState,
  depletedRocks: Record<string, number>,
  rockHealth: Record<string, number>,
  tick: number,
): MiningResult {
  const nextPlayers: Record<string, PlayerState> = { ...players };
  const nextDepleted: Record<string, number> = { ...depletedRocks };
  let nextHealth: Record<string, number> = { ...rockHealth };
  let nextWorld = world;
  const messages: Record<string, string[]> = Object.fromEntries(
    Object.keys(players).map(id => [id, []]),
  );

  const activelyMined = new Set<string>();

  for (const [playerId, player] of Object.entries(players)) {
    if (player.dying) { nextPlayers[playerId] = player; continue; }
    const actions = playerActions.get(playerId) ?? [];
    let p = { ...player };

    // --- Process new MINE_ROCK actions ---
    for (const action of actions) {
      if (action.type !== 'MINE_ROCK') continue;
      p = {
        ...p,
        mineTargetX: action.tileX,
        mineTargetY: action.tileY,
        attackTargetId: null,
        talkTargetId: null,
        pickupItemId: null,
      };
    }

    const tx = p.mineTargetX;
    const ty = p.mineTargetY;

    if (tx === null || ty === null) {
      nextPlayers[playerId] = p;
      continue;
    }

    const key = rockKey(tx, ty);

    // Rock already depleted?
    if (nextDepleted[key] !== undefined) {
      messages[playerId].push('This rock has already been mined.');
      nextPlayers[playerId] = { ...p, mineTargetX: null, mineTargetY: null };
      continue;
    }

    // Verify tile is still a rock
    const tile = world.tiles[ty]?.[tx];
    if (!tile || tile.obstacle !== 'rock') {
      nextPlayers[playerId] = { ...p, mineTargetX: null, mineTargetY: null };
      continue;
    }

    const rockDef = ROCK_DEFS['rock'];

    if (p.skills.mining.level < rockDef.requiredLevel) {
      messages[playerId].push(`You need level ${rockDef.requiredLevel} Mining to mine this.`);
      nextPlayers[playerId] = { ...p, mineTargetX: null, mineTargetY: null };
      continue;
    }

    // Need to walk to the rock?
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
          messages[playerId].push("You can't reach that rock.");
          p = { ...p, mineTargetX: null, mineTargetY: null };
        }
      }
      nextPlayers[playerId] = p;
      continue;
    }

    // Adjacent — check for pickaxe
    if (!findPickaxe(p)) {
      messages[playerId].push('You need a pickaxe to mine this rock.');
      nextPlayers[playerId] = { ...p, mineTargetX: null, mineTargetY: null };
      continue;
    }

    // Check inventory space before starting.
    const freeSlot = p.inventory.findIndex(s => s === null);
    if (freeSlot === -1) {
      messages[playerId].push('Your inventory is too full to hold any more ore.');
      nextPlayers[playerId] = { ...p, mineTargetX: null, mineTargetY: null };
      continue;
    }

    // Face the rock
    p = { ...p, facing: directionTo(p.tileX, p.tileY, tx, ty) };

    // Decrement rock health
    activelyMined.add(key);
    const currentHealth = nextHealth[key] ?? rockDef.despawnHealth;
    const newHealth = Math.max(0, currentHealth - 1);
    nextHealth[key] = newHealth;

    // Not yet time to roll?
    if (tick - p.lastMineTick < MINE_INTERVAL) {
      nextPlayers[playerId] = p;
      continue;
    }

    // Success roll
    if (Math.random() >= SUCCESS_CHANCE) {
      nextPlayers[playerId] = { ...p, lastMineTick: tick };
      continue;
    }

    // Grant ore + XP
    const newInventory = [...p.inventory];
    newInventory[freeSlot] = { itemId: rockDef.resourceId, quantity: 1 };

    const { skill: newMiningSkill, levelsGained } = addXP(p.skills.mining, rockDef.xp);
    const newSkills = { ...p.skills, mining: newMiningSkill };

    messages[playerId].push('You manage to mine some copper ore.');
    if (levelsGained > 0) {
      messages[playerId].push(`Congratulations! Your Mining level is now ${newMiningSkill.level}.`);
    }

    p = { ...p, inventory: newInventory, skills: newSkills, lastMineTick: tick };

    // Deplete rock if health reached 0
    if (newHealth === 0) {
      nextDepleted[key] = tick + rockDef.respawnTicks;
      delete nextHealth[key];

      const newTiles = nextWorld.tiles.map(row => [...row]);
      newTiles[ty] = [...newTiles[ty]];
      newTiles[ty][tx] = { ...newTiles[ty][tx], walkable: true, obstacle: '', blocksRanged: false };
      nextWorld = { ...nextWorld, tiles: newTiles };

      p = { ...p, mineTargetX: null, mineTargetY: null };
      messages[playerId].push('The rock is depleted.');
    }

    nextPlayers[playerId] = p;
  }

  // Regen health for rocks not actively mined this tick
  if (tick % REGEN_INTERVAL === 0) {
    const nextHealthCopy: Record<string, number> = {};
    for (const [key, health] of Object.entries(nextHealth)) {
      if (activelyMined.has(key)) {
        nextHealthCopy[key] = health;
      } else {
        const [xs, ys] = key.split('-');
        const x = parseInt(xs, 10);
        const y = parseInt(ys, 10);
        const tile = world.tiles[y]?.[x];
        if (tile?.obstacle === 'rock') {
          const rockDef = ROCK_DEFS['rock'];
          const restored = Math.min(rockDef.despawnHealth, health + 1);
          if (restored < rockDef.despawnHealth) {
            nextHealthCopy[key] = restored;
          }
        } else {
          nextHealthCopy[key] = health;
        }
      }
    }
    nextHealth = nextHealthCopy;
  }

  // Respawn depleted rocks whose timer has expired
  for (const [key, respawnAtTick] of Object.entries(nextDepleted)) {
    if (tick < respawnAtTick) continue;
    const [xs, ys] = key.split('-');
    const x = parseInt(xs, 10);
    const y = parseInt(ys, 10);

    const newTiles = nextWorld.tiles.map(row => [...row]);
    newTiles[y] = [...newTiles[y]];
    newTiles[y][x] = { ...newTiles[y][x], walkable: false, obstacle: 'rock', blocksRanged: true };
    nextWorld = { ...nextWorld, tiles: newTiles };

    delete nextDepleted[key];
  }

  return {
    players: nextPlayers,
    depletedRocks: nextDepleted,
    rockHealth: nextHealth,
    world: nextWorld,
    messages,
  };
}
