import type { PlayerState, WorldState, GameAction, Direction, GridPosition } from '../shared/types';
import { findPath } from '../world/Pathfinder';

function directionBetween(from: GridPosition, to: GridPosition): Direction {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  if (Math.abs(dx) >= Math.abs(dy)) {
    return dx > 0 ? 'east' : 'west';
  }
  return dy > 0 ? 'south' : 'north';
}

export function processMovement(
  player: PlayerState,
  world: WorldState,
  actions: GameAction[],
): PlayerState {
  // Dying players cannot move or act
  if (player.dying) return player;

  let state = { ...player, path: [...player.path] };

  // MOVE_TO overrides any ongoing action, including combat
  for (const action of actions) {
    if (action.type === 'MOVE_TO') {
      const newPath = findPath(
        world,
        { x: state.tileX, y: state.tileY },
        { x: action.targetX, y: action.targetY },
      );
      state.path = newPath;
      state.destinationX = action.targetX;
      state.destinationY = action.targetY;
      state.attackTargetId = null; // walking cancels combat
      state.talkTargetId   = null;
      state.pickupItemId   = null;
      state.chopTargetX    = null;
      state.chopTargetY    = null;
    }
  }

  // Advance one step along the path (1 tile per tick)
  if (state.path.length > 0) {
    const next = state.path[0];
    state.facing = directionBetween(
      { x: state.tileX, y: state.tileY },
      next,
    );
    state.tileX = next.x;
    state.tileY = next.y;
    state.path = state.path.slice(1);
  }

  return state;
}
