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
      droppedItems: [],
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
      // Reset transient combat state so they don't resume mid-fight
      player = {
        ...savedState,
        path: [],
        attackTargetId: null,
        talkTargetId: null,
        pickupItemId: null,
        chopTargetX: null,
        chopTargetY: null,
        chatMessage: '',
        chatMessageTick: -999,
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
  inventory[0] = { itemId: 'pickaxe', quantity: 1 };
  inventory[1] = { itemId: 'iron_axe', quantity: 1 };

  return {
    tileX,
    tileY,
    facing: 'south',
    path: [],
    destinationX: tileX,
    destinationY: tileY,
    skills: createDefaultSkills(),
    inventory,
    equipped: {},
    hp: 100,
    maxHp: 100,
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
  };
}
