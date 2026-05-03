import type { GameState, PlayerState, ServerStatePatch } from '../src/shared/types';
import { TICK_DURATION_MS, PLAYER_START_X, PLAYER_START_Y } from '../src/shared/constants';
import { createWorldState, findWalkableTileNear } from '../src/world/WorldState';
import { spawnNPC } from '../src/systems/NPCSystem';
import { createDefaultSkills } from '../src/systems/SkillSystem';
import { createEmptyInventory } from '../src/systems/InventorySystem';
import { processTick } from '../src/engine/TickSystem';
import type { GameAction } from '../src/shared/types';

export const WORLD_SEED = 42;

type BroadcastFn = (patch: ServerStatePatch) => void;

export class GameLoop {
  private state: GameState;
  private pendingActions = new Map<string, GameAction[]>();
  private timer: ReturnType<typeof setInterval> | null = null;
  private broadcast: BroadcastFn;

  constructor(broadcast: BroadcastFn) {
    this.broadcast = broadcast;

    const world = createWorldState(WORLD_SEED);
    const chickenOffsets = [
      { dx:  8, dy:  4 }, { dx: 10, dy:  6 }, { dx:  6, dy:  7 },
      { dx:  9, dy:  2 }, { dx:  7, dy:  9 },
    ];
    const chickenSpawns = chickenOffsets.map(o =>
      findWalkableTileNear(world, PLAYER_START_X + o.dx, PLAYER_START_Y + o.dy),
    );
    const shopkeeperSpawn = findWalkableTileNear(world, PLAYER_START_X - 6, PLAYER_START_Y - 8);

    this.state = {
      tick: 0,
      world,
      players: {},
      npcs: [
        ...chickenSpawns.map((s, i) => spawnNPC(`chicken-${i + 1}`, 'chicken', s.x, s.y)),
        spawnNPC('shopkeeper-1', 'shopkeeper', shopkeeperSpawn.x, shopkeeperSpawn.y),
      ],
      // Permanent test rack — items north of spawn, always available, never despawn
      droppedItems: [
        { id: 'rack-pickaxe',         itemId: 'pickaxe',         quantity: 1,   tileX: PLAYER_START_X - 2, tileY: PLAYER_START_Y - 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-iron-axe',        itemId: 'iron_axe',        quantity: 1,   tileX: PLAYER_START_X - 1, tileY: PLAYER_START_Y - 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-basic-chaingun',  itemId: 'basic_chaingun',  quantity: 1,   tileX: PLAYER_START_X,     tileY: PLAYER_START_Y - 2, droppedAtTick: 0, permanent: true },
        { id: 'rack-kinetic-charges', itemId: 'kinetic_charges', quantity: 500, tileX: PLAYER_START_X + 1, tileY: PLAYER_START_Y - 2, droppedAtTick: 0, permanent: true },
      ],
      pendingRespawns: [],
      messages: {},
      depletedTrees: {},
      treeHealth: {},
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
        hp: Math.min(savedState.hp, restoredMaxHp),
        maxHp: restoredMaxHp,
        path: [],
        attackTargetId: null,
        talkTargetId: null,
        pickupItemId: null,
        chopTargetX: null,
        chopTargetY: null,
        chatMessage: '',
        chatMessageTick: -999,
        lastHitTick:    -999,
        lastAttackTick: -999,
        lastChopTick:   -999,
        dying:         false,
        dyingTick:     -999,
        lastRegenTick: -999,
      };
    } else {
      const spawn = findWalkableTileNear(this.state.world, PLAYER_START_X, PLAYER_START_Y);
      player = createInitialPlayer(spawn.x, spawn.y, name);
    }

    this.state = {
      ...this.state,
      players: { ...this.state.players, [playerId]: player },
    };
    this.pendingActions.set(playerId, []);
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

  getWorldSeed(): number {
    return WORLD_SEED;
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
    const playerActions = new Map<string, GameAction[]>();
    for (const [id, actions] of this.pendingActions) {
      playerActions.set(id, actions.splice(0));
    }

    this.state = processTick(this.state, playerActions);

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
    dying: false,
    dyingTick: -999,
    lastRegenTick: -999,
  };
}
