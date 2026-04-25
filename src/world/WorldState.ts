import type { WorldState, ObstacleType } from '../shared/types';
import {
  GRID_WIDTH, GRID_HEIGHT,
  PLAYER_START_X, PLAYER_START_Y,
  OBSTACLE_CLEAR_RADIUS, OBSTACLE_DENSITY,
} from '../shared/constants';

export function seededRandom(seed: number): () => number {
  let s = seed;
  return () => {
    s |= 0; s = s + 0x6d2b79f5 | 0;
    let t = Math.imul(s ^ s >>> 15, 1 | s);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

export function createWorldState(seed = 42): WorldState {
  const tiles: WorldState['tiles'] = [];

  for (let y = 0; y < GRID_HEIGHT; y++) {
    tiles[y] = [];
    for (let x = 0; x < GRID_WIDTH; x++) {
      tiles[y][x] = { x, y, walkable: true, type: 'grass', obstacle: 'none' };
    }
  }

  const rng = seededRandom(seed);
  for (let y = 0; y < GRID_HEIGHT; y++) {
    for (let x = 0; x < GRID_WIDTH; x++) {
      const dx = x - PLAYER_START_X;
      const dy = y - PLAYER_START_Y;
      if (Math.sqrt(dx * dx + dy * dy) < OBSTACLE_CLEAR_RADIUS) continue;

      if (rng() < OBSTACLE_DENSITY) {
        const obstacle: ObstacleType = rng() < 0.6 ? 'tree' : 'rock';
        tiles[y][x].walkable = false;
        tiles[y][x].obstacle = obstacle;
      }
    }
  }

  return { width: GRID_WIDTH, height: GRID_HEIGHT, tiles };
}

export function findWalkableTileNear(world: WorldState, cx: number, cy: number): { x: number; y: number } {
  for (let r = 0; r <= 10; r++) {
    for (let dy = -r; dy <= r; dy++) {
      for (let dx = -r; dx <= r; dx++) {
        if (Math.abs(dx) !== r && Math.abs(dy) !== r) continue;
        const x = Math.round(cx + dx);
        const y = Math.round(cy + dy);
        if (x >= 0 && y >= 0 && x < world.width && y < world.height && world.tiles[y][x].walkable) {
          return { x, y };
        }
      }
    }
  }
  return { x: Math.round(cx), y: Math.round(cy) };
}
