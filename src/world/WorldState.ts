import type { WorldState, TileData } from '../shared/types';
import {
  GRID_WIDTH, GRID_HEIGHT,
  PLAYER_START_X, PLAYER_START_Y,
  OBSTACLE_CLEAR_RADIUS, OBSTACLE_DENSITY,
  BANK_CHEST_X, BANK_CHEST_Y,
} from '../shared/constants';

// Build a (W+1)×(H+1) vertex-height Float32Array from legacy per-tile heights.
// Uses the same corner-averaging logic the old computeVertexHeight used, so old
// maps look identical after migration.
function migrateVertexHeights(tiles: TileData[][], W: number, H: number): Float32Array {
  const vh = new Float32Array((W + 1) * (H + 1));
  for (let row = 0; row <= H; row++) {
    for (let col = 0; col <= W; col++) {
      let sum = 0, count = 0;
      for (const tx of [col - 1, col]) {
        for (const ty of [H - row - 1, H - row]) {
          if (tx < 0 || tx >= W || ty < 0 || ty >= H) continue;
          const tile = tiles[ty]?.[tx];
          if (!tile) continue;
          if (tile.type === 'water') continue;
          sum += (tile.height ?? 0);
          count++;
        }
      }
      vh[row * (W + 1) + col] = count > 0 ? sum / count : 0;
    }
  }
  return vh;
}

export function createWorldFromTiles(tiles: TileData[][], vertexHeights?: number[]): WorldState {
  if (tiles.length === 0) {
    const vh = new Float32Array((GRID_WIDTH + 1) * (GRID_HEIGHT + 1));
    return { width: GRID_WIDTH, height: GRID_HEIGHT, tiles: [], vertexHeights: vh };
  }
  // Normalize: ensure height field exists for tiles loaded from old map files
  const normalized = tiles.map(row =>
    row.map(tile => ({ ...tile, height: (tile as TileData & { height?: number }).height ?? 0 })),
  );
  const W = normalized[0].length;
  const H = normalized.length;
  const vh = vertexHeights
    ? Float32Array.from(vertexHeights)
    : migrateVertexHeights(normalized, W, H);
  return { width: W, height: H, tiles: normalized, vertexHeights: vh };
}

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
      tiles[y][x] = { x, y, walkable: true, type: 'grass', obstacle: '', blocksRanged: false, groundColor: '#7ec850', height: 0 };
    }
  }

  const rng = seededRandom(seed);
  for (let y = 0; y < GRID_HEIGHT; y++) {
    for (let x = 0; x < GRID_WIDTH; x++) {
      const dx = x - PLAYER_START_X;
      const dy = y - PLAYER_START_Y;
      if (Math.sqrt(dx * dx + dy * dy) < OBSTACLE_CLEAR_RADIUS) continue;

      if (rng() < OBSTACLE_DENSITY) {
        const obstacle = rng() < 0.6 ? 'tree' : 'rock';
        tiles[y][x].walkable     = false;
        tiles[y][x].obstacle     = obstacle;
        tiles[y][x].blocksRanged = obstacle === 'tree';
      }
    }
  }

  // ---- Fishing pool (NE of spawn) --------------------------------------------
  const POOL_CX = PLAYER_START_X + 10;
  const POOL_CY = PLAYER_START_Y - 8;
  const POOL_RX = 5;
  const POOL_RY = 4;

  for (let y = 0; y < GRID_HEIGHT; y++) {
    for (let x = 0; x < GRID_WIDTH; x++) {
      const nx = (x - POOL_CX) / POOL_RX;
      const ny = (y - POOL_CY) / POOL_RY;
      if (nx * nx + ny * ny <= 1) {
        tiles[y][x] = { x, y, walkable: false, type: 'water', obstacle: '', blocksRanged: false, groundColor: '#1878e5', height: 0 };
      } else if (nx * nx + ny * ny <= 1.6) {
        tiles[y][x].obstacle = '';
        tiles[y][x].walkable = true;
      }
    }
  }

  tiles[BANK_CHEST_Y][BANK_CHEST_X] = {
    x: BANK_CHEST_X, y: BANK_CHEST_Y,
    walkable: false, type: 'grass', obstacle: 'chest', blocksRanged: true, groundColor: '#7ec850', height: 0,
  };

  const vh = new Float32Array((GRID_WIDTH + 1) * (GRID_HEIGHT + 1)); // all zeros
  return { width: GRID_WIDTH, height: GRID_HEIGHT, tiles, vertexHeights: vh };
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
