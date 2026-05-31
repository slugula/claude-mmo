import type { GameState, GameAction, PlayerState, NPCState, DroppedItemState, RespawnEntry } from '../shared/types';
import { processMovement } from '../systems/MovementSystem';
import { processNPCs, processRespawns } from '../systems/NPCSystem';
import { processCombat } from '../systems/CombatSystem';
import { processInteractions } from '../systems/InteractSystem';
import { processItems } from '../systems/ItemSystem';
import { processWoodcutting } from '../systems/WoodcuttingSystem';
import { processMining } from '../systems/MiningSystem';
import { processFishing } from '../systems/FishingSystem';

export function processTick(
  prev: GameState,
  playerActions: Map<string, GameAction[]>,
): GameState {
  const tick = prev.tick + 1;

  let sharedNpcs: NPCState[] = prev.npcs;
  let sharedDropped: DroppedItemState[] = prev.droppedItems;
  let sharedRespawns: RespawnEntry[] = [...prev.pendingRespawns];

  const nextPlayers: Record<string, PlayerState> = {};
  const nextMessages: Record<string, string[]> = {};

  // Per-player systems — NPC/dropped state chains through each player
  for (const [playerId, player] of Object.entries(prev.players)) {
    const actions = playerActions.get(playerId) ?? [];
    const msgs: string[] = [];

    // 1. Combat
    const combat = processCombat(player, sharedNpcs, sharedDropped, actions, prev.world, tick);
    msgs.push(...combat.messages);
    sharedNpcs    = combat.npcs;
    sharedDropped = combat.droppedItems;
    sharedRespawns = [...sharedRespawns, ...combat.newRespawns];

    // 2. Interactions
    const interact = processInteractions(combat.player, sharedNpcs, actions, prev.world);
    msgs.push(...interact.messages);

    // 3. Movement
    const moved = processMovement(interact.player, prev.world, actions);

    // 4. Items
    const items = processItems(moved, sharedDropped, actions, prev.world, tick);
    msgs.push(...items.messages);
    sharedDropped = items.droppedItems;

    nextPlayers[playerId] = items.player;
    nextMessages[playerId] = msgs;
  }

  // 5. Woodcutting (global step — tree state shared across all players)
  const wc = processWoodcutting(
    nextPlayers,
    playerActions,
    prev.world,
    prev.depletedTrees,
    prev.treeHealth,
    tick,
  );
  for (const [pid, msgs] of Object.entries(wc.messages)) {
    if (nextMessages[pid]) nextMessages[pid].push(...msgs);
    else nextMessages[pid] = [...msgs];
  }

  // 6. Mining (global step — rock state shared across all players)
  const mine = processMining(
    wc.players,
    playerActions,
    wc.world,
    prev.depletedRocks,
    prev.rockHealth,
    tick,
  );
  for (const [pid, msgs] of Object.entries(mine.messages)) {
    if (nextMessages[pid]) nextMessages[pid].push(...msgs);
    else nextMessages[pid] = [...msgs];
  }

  // 7. Fishing (global step — inexhaustible spots, no shared depletion)
  const fish = processFishing(mine.players, playerActions, mine.world, tick);
  for (const [pid, msgs] of Object.entries(fish.messages)) {
    if (nextMessages[pid]) nextMessages[pid].push(...msgs);
    else nextMessages[pid] = [...msgs];
  }

  const finalPlayers = fish.players;
  const finalWorld = mine.world;

  // Expire dropped items older than 60 seconds (300 ticks at 200ms). Permanent items never despawn.
  const ITEM_DESPAWN_TICKS = 300;
  sharedDropped = sharedDropped.filter(item => item.permanent || tick - item.droppedAtTick < ITEM_DESPAWN_TICKS);

  // NPC AI runs once (shared world)
  const afterAI = processNPCs(sharedNpcs, finalWorld);
  const respawned = processRespawns(sharedRespawns, afterAI, finalWorld, tick);

  return {
    ...prev,
    tick,
    world: finalWorld,
    players: finalPlayers,
    npcs: respawned.npcs,
    droppedItems: sharedDropped,
    pendingRespawns: respawned.pending,
    messages: nextMessages,
    depletedTrees: wc.depletedTrees,
    treeHealth: wc.treeHealth,
    depletedRocks: mine.depletedRocks,
    rockHealth: mine.rockHealth,
  };
}
