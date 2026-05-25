import type { PlayerState, WorldState, GameAction, Direction, GridPosition } from '../shared/types';
import { findPath } from '../world/Pathfinder';
import { findWalkableTileNear } from '../world/WorldState';

function directionBetween(from: GridPosition, to: GridPosition): Direction {
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
      // If the clicked tile is not walkable (obstacle, water, etc.), redirect to
      // the nearest walkable tile so minimap clicks on trees/rocks still navigate.
      const targetTile = world.tiles[action.targetY]?.[action.targetX];
      const dest = (targetTile && targetTile.walkable)
        ? { x: action.targetX, y: action.targetY }
        : findWalkableTileNear(world, action.targetX, action.targetY);
      const newPath = findPath(
        world,
        { x: state.tileX, y: state.tileY },
        dest,
      );
      state.path = newPath;
      state.destinationX = dest.x;
      state.destinationY = dest.y;
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
