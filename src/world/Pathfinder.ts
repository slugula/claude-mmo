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

function heuristic(a: GridPosition, b: GridPosition): number {
  return Math.abs(a.x - b.x) + Math.abs(a.y - b.y);
}

function key(x: number, y: number): number {
  return x * 10000 + y;
}

const DIRS: [number, number][] = [
  [0, -1], [0, 1], [-1, 0], [1, 0],
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
      if (!isHeightPassable(world, current.x, current.y, nx, ny)) continue;

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

function isHeightPassable(world: WorldState, ax: number, ay: number, bx: number, by: number): boolean {
  const ha = world.tiles[ay]?.[ax]?.height ?? 0;
  const hb = world.tiles[by]?.[bx]?.height ?? 0;
  return Math.abs(ha - hb) <= HEIGHT_IMPASSABLE_DELTA;
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
