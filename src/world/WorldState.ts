import type { WorldState, TileData, WallSeg } from '../shared/types';
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

export function createWorldFromTiles(
  tiles: TileData[][], vertexHeights?: number[], walls?: WallSeg[],
): WorldState {
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
  // A diagonal wall cuts through the middle of its tile — block the whole tile
  // (simpler and matches expectations) rather than an edge.
  if (walls) {
    for (const w of walls) {
      if (!w.pillar && (w.orient & 1) === 1) {
        const t = normalized[w.tileY]?.[w.tileX];
        if (t) t.walkable = false;
      }
    }
  }
  const wallClip = walls && walls.length > 0 ? buildWallClip(walls, W, H) : undefined;
  return { width: W, height: H, tiles: normalized, vertexHeights: vh, wallClip };
}

// ---- Wall clipping (edge-based collision) ----------------------------------
// Per-tile bitmask of blocked OUTGOING directions. Cardinal walls block an edge
// (set on both tiles sharing it); pillars block the diagonal through their
// corner. Diagonal walls (odd orient) are not yet clipped.
export const CLIP_XP = 1,  CLIP_XM = 2,  CLIP_YP = 4,  CLIP_YM = 8;     // +x,-x,+y,-y edges
export const CLIP_PP = 16, CLIP_PM = 32, CLIP_MP = 64, CLIP_MM = 128;   // +x+y,+x-y,-x+y,-x-y diagonals

export function buildWallClip(walls: WallSeg[], W: number, H: number): Uint8Array {
  const clip = new Uint8Array(W * H);
  const set = (x: number, y: number, bit: number) => {
    if (x >= 0 && x < W && y >= 0 && y < H) clip[y * W + x] |= bit;
  };
  for (const w of walls) {
    const x = w.tileX, y = w.tileY, o = w.orient & 7;
    if (w.pillar) {
      if      (o === 0) { set(x, y, CLIP_PP); set(x + 1, y + 1, CLIP_MM); }
      else if (o === 2) { set(x, y, CLIP_PM); set(x + 1, y - 1, CLIP_MP); }
      else if (o === 4) { set(x, y, CLIP_MM); set(x - 1, y - 1, CLIP_PP); }
      else if (o === 6) { set(x, y, CLIP_MP); set(x - 1, y + 1, CLIP_PM); }
    } else if ((o & 1) === 0) {   // cardinal edge wall
      if      (o === 0) { set(x, y, CLIP_YP); set(x, y + 1, CLIP_YM); }
      else if (o === 2) { set(x, y, CLIP_XP); set(x + 1, y, CLIP_XM); }
      else if (o === 4) { set(x, y, CLIP_YM); set(x, y - 1, CLIP_YP); }
      else if (o === 6) { set(x, y, CLIP_XM); set(x - 1, y, CLIP_XP); }
    }
  }
  return clip;
}

// True if moving from tile (x,y) by (dx,dy) is blocked by a wall/pillar.
export function clipBlocks(
  clip: Uint8Array, W: number, x: number, y: number, dx: number, dy: number,
): boolean {
  const cm = clip[y * W + x];
  if (dx !== 0 && dy !== 0) {
    const dbit = dx > 0 ? (dy > 0 ? CLIP_PP : CLIP_PM)
                        : (dy > 0 ? CLIP_MP : CLIP_MM);
    if (cm & dbit) return true;
    // Corner-cut: can't slip diagonally past a wall on either adjacent edge.
    if (clipBlocks(clip, W, x, y, dx, 0)) return true;
    if (clipBlocks(clip, W, x, y, 0, dy)) return true;
    return false;
  }
  if (dx > 0) return (cm & CLIP_XP) !== 0;
  if (dx < 0) return (cm & CLIP_XM) !== 0;
  if (dy > 0) return (cm & CLIP_YP) !== 0;
  if (dy < 0) return (cm & CLIP_YM) !== 0;
  return false;
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
