import type { GridPosition, WorldState } from '../shared/types';
import { HEIGHT_IMPASSABLE_DELTA } from '../shared/constants';

interface Node {
  x: number;
  y: number;
  g: number;
  h: number;
  f: number;
  parent: Node | null;
}

// Chebyshev distance — correct admissible heuristic for 8-directional
// movement where diagonal steps cost the same as cardinal steps.
function heuristic(a: GridPosition, b: GridPosition): number {
  return Math.max(Math.abs(a.x - b.x), Math.abs(a.y - b.y));
}

function key(x: number, y: number): number {
  return x * 10000 + y;
}

// OSRS neighbour order: cardinals first (W, E, S, N), then diagonals (SW, SE, NW, NE).
// Cardinals are preferred so the path hugs walls rather than taking wide arcs.
const DIRS: [number, number][] = [
  [-1,  0], [ 1,  0], [ 0,  1], [ 0, -1],   // W, E, S, N
  [-1,  1], [ 1,  1], [-1, -1], [ 1, -1],   // SW, SE, NW, NE
];

export function findPath(
  world: WorldState,
  from: GridPosition,
  to: GridPosition,
): GridPosition[] {
  if (!isWalkable(world, to.x, to.y)) return [];
  if (from.x === to.x && from.y === to.y) return [];

  const open: Node[] = [];
  const closed = new Set<number>();
  const openMap = new Map<number, Node>();

  const startNode: Node = { x: from.x, y: from.y, g: 0, h: heuristic(from, to), f: 0, parent: null };
  startNode.f = startNode.h;
  open.push(startNode);
  openMap.set(key(from.x, from.y), startNode);

  while (open.length > 0) {
    open.sort((a, b) => a.f - b.f);
    const current = open.shift()!;
    const ck = key(current.x, current.y);
    openMap.delete(ck);
    closed.add(ck);

    if (current.x === to.x && current.y === to.y) {
      return buildPath(current);
    }

    for (const [dx, dy] of DIRS) {
      const nx = current.x + dx;
      const ny = current.y + dy;
      const nk = key(nx, ny);

      if (closed.has(nk)) continue;
      if (!isWalkable(world, nx, ny)) continue;
      // Block movement between tiles whose average vertex height differs too much
      if (Math.abs(tileAvgHeight(world, current.x, current.y) - tileAvgHeight(world, nx, ny)) > HEIGHT_IMPASSABLE_DELTA) continue;
      // Corner-clip prevention (OSRS rule): a diagonal step is blocked if either
      // of the two intermediate cardinal tiles is impassable.  This stops the
      // path from cutting through the corner of a wall or obstacle.
      if (dx !== 0 && dy !== 0) {
        if (!isWalkable(world, current.x + dx, current.y)) continue;
        if (!isWalkable(world, current.x, current.y + dy)) continue;
      }

      const g = current.g + 1;
      const existing = openMap.get(nk);
      if (existing && g >= existing.g) continue;

      const node: Node = {
        x: nx, y: ny,
        g,
        h: heuristic({ x: nx, y: ny }, to),
        f: 0,
        parent: current,
      };
      node.f = node.g + node.h;

      if (existing) {
        const idx = open.indexOf(existing);
        if (idx !== -1) open.splice(idx, 1);
      }
      open.push(node);
      openMap.set(nk, node);
    }

    if (closed.size > 2048) break;
  }

  return [];
}

function isWalkable(world: WorldState, x: number, y: number): boolean {
  if (x < 0 || y < 0 || x >= world.width || y >= world.height) return false;
  return world.tiles[y][x].walkable;
}

function tileAvgHeight(world: WorldState, tx: number, ty: number): number {
  const W = world.width;
  const vh = world.vertexHeights;
  return (
    (vh[ty       * (W + 1) + tx]     ?? 0) +
    (vh[ty       * (W + 1) + tx + 1] ?? 0) +
    (vh[(ty + 1) * (W + 1) + tx]     ?? 0) +
    (vh[(ty + 1) * (W + 1) + tx + 1] ?? 0)
  ) / 4;
}

function buildPath(node: Node): GridPosition[] {
  const path: GridPosition[] = [];
  let current: Node | null = node;
  while (current !== null) {
    path.unshift({ x: current.x, y: current.y });
    current = current.parent;
  }
  path.shift();
  return path;
}
