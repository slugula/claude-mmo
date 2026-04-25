import type { PlayerState, NPCState, GameAction, WorldState, GridPosition } from '../shared/types';
import { getNPCDef } from '../npcs/NPCRegistry';
import { findReachableAdjacent } from './CombatSystem';

export interface InteractResult {
  player: PlayerState;
  messages: string[];
}

function pos(e: { tileX: number; tileY: number }): GridPosition {
  return { x: e.tileX, y: e.tileY };
}

export function processInteractions(
  player: PlayerState,
  npcs: NPCState[],
  actions: GameAction[],
  world: WorldState,
): InteractResult {
  let nextPlayer = { ...player };
  const messages: string[] = [];

  for (const action of actions) {
    if (action.type !== 'TALK_TO') continue;
    const target = npcs.find(n => n.id === action.npcId && !n.dying);
    if (!target) continue;

    if (isAdjacent(pos(nextPlayer), pos(target))) {
      nextPlayer = { ...nextPlayer, talkTargetId: null, attackTargetId: null, path: [] };
      messages.push(greeting(target));
      continue;
    }

    const spot = findReachableAdjacent(pos(nextPlayer), pos(target), world);
    if (!spot) {
      messages.push(`I can\u2019t reach that.`);
      continue;
    }

    nextPlayer = {
      ...nextPlayer,
      talkTargetId: target.id,
      attackTargetId: null,
      path: spot.path,
      destinationX: spot.pos.x,
      destinationY: spot.pos.y,
    };
  }

  // Arrival — player finished walking to talk target
  if (nextPlayer.talkTargetId && nextPlayer.path.length === 0) {
    const target = npcs.find(n => n.id === nextPlayer.talkTargetId && !n.dying);
    if (!target) {
      nextPlayer = { ...nextPlayer, talkTargetId: null };
    } else if (isAdjacent(pos(nextPlayer), pos(target))) {
      messages.push(greeting(target));
      nextPlayer = { ...nextPlayer, talkTargetId: null };
    }
  }

  return { player: nextPlayer, messages };
}

function greeting(npc: NPCState): string {
  const def = getNPCDef(npc.kind);
  if (npc.kind === 'shopkeeper') {
    return `${def.name}: "Welcome, traveller! My wares will arrive soon."`;
  }
  return `${def.name} has nothing to say.`;
}

function isAdjacent(a: GridPosition, b: GridPosition): boolean {
  return Math.abs(a.x - b.x) <= 1 && Math.abs(a.y - b.y) <= 1 && !(a.x === b.x && a.y === b.y);
}
