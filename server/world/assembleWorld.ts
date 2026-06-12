import { readFileSync, existsSync } from 'fs';
import { dirname, join } from 'path';
import type { TileData, NPCSpawn, PermanentItemSpawn, WallSeg, WorldManifest } from '../../src/shared/types';

// Assembles the chunk maps referenced by a world manifest (world.json) into one
// flat global world. Cell (cx,cy) owns global tiles [cx*S, cx*S+S). Everything
// downstream (tick systems, pathfinding, interest management) operates on the
// assembled width/height/tiles in global coordinates, so cross-chunk walking,
// pathing, and interaction need no further changes.

// On-disk chunk map shape — same fields GameLoop's single-map loader reads.
export interface ChunkMapJSON {
  version?: number;
  width:    number;
  height:   number;
  tiles:    TileData[][];
  npcSpawns?:      NPCSpawn[];
  permanentItems?: PermanentItemSpawn[];
  vertexHeights?:  number[];
  waterTiles?: { tileX: number; tileY: number }[];
  overlayTiles?: { tileX: number; tileY: number; shape: number; materialId: number; rotation?: number }[];
  walls?: WallSeg[];
}

export interface AssembledWorld {
  width:  number;
  height: number;
  tiles:  TileData[][];
  vertexHeights: number[];
  npcSpawns: NPCSpawn[];
  permanentItems: PermanentItemSpawn[];
  overlayTiles: { tileX: number; tileY: number; shape: number; materialId: number; rotation?: number }[];
  walls: WallSeg[];
  spawn: { x: number; y: number };
  /** Global positions of chest obstacles, collected during assembly so GameLoop
   *  doesn't need an O(w·h) scan over the full world. */
  chests: { x: number; y: number }[];
}

export function loadWorldManifest(manifestPath: string): WorldManifest | null {
  if (!existsSync(manifestPath)) return null;
  const manifest = JSON.parse(readFileSync(manifestPath, 'utf-8')) as WorldManifest;
  if (!Array.isArray(manifest.chunks) || manifest.chunks.length === 0) {
    throw new Error(`[assembleWorld] ${manifestPath}: manifest has no chunks`);
  }
  if (!Number.isInteger(manifest.chunkSize) || manifest.chunkSize <= 0) {
    throw new Error(`[assembleWorld] ${manifestPath}: invalid chunkSize`);
  }
  for (const c of manifest.chunks) {
    if (c.cx < 0 || c.cy < 0) {
      throw new Error(`[assembleWorld] chunk ${c.mapFile}: negative cell coords (${c.cx},${c.cy}) not supported in v1`);
    }
  }
  return manifest;
}

function voidTile(x: number, y: number): TileData {
  return {
    x, y,
    walkable: false,
    type: 'grass',
    obstacle: 'none',
    blocksRanged: false,
    groundColor: '#1c2026',   // dark neutral so void reads as "nothing" on minimaps
    height: 0,
  } as TileData;
}

export function assembleWorld(manifestPath: string, manifest: WorldManifest): AssembledWorld {
  const S = manifest.chunkSize;
  const baseDir = dirname(manifestPath);

  const maxCx = Math.max(...manifest.chunks.map(c => c.cx));
  const maxCy = Math.max(...manifest.chunks.map(c => c.cy));
  const gw = (maxCx + 1) * S;
  const gh = (maxCy + 1) * S;

  // Void fill: non-walkable flat ground everywhere a chunk isn't assigned.
  const tiles: TileData[][] = [];
  for (let y = 0; y < gh; y++) {
    tiles[y] = [];
    for (let x = 0; x < gw; x++) tiles[y][x] = voidTile(x, y);
  }
  const vertexHeights = new Array<number>((gw + 1) * (gh + 1)).fill(0);

  const npcSpawns: NPCSpawn[] = [];
  const permanentItems: PermanentItemSpawn[] = [];
  const overlayTiles: AssembledWorld['overlayTiles'] = [];
  const walls: WallSeg[] = [];
  const chests: { x: number; y: number }[] = [];

  // Tracks which chunk last wrote each shared edge vertex, for mismatch warnings.
  const vertexOwner = new Map<number, string>();
  const mismatches: string[] = [];

  for (const chunk of manifest.chunks) {
    const path = join(baseDir, chunk.mapFile);
    const data = JSON.parse(readFileSync(path, 'utf-8')) as ChunkMapJSON;
    if (data.width !== S || data.height !== S) {
      throw new Error(`[assembleWorld] ${chunk.mapFile} is ${data.width}×${data.height}; world chunkSize is ${S}`);
    }
    const ox = chunk.cx * S;
    const oy = chunk.cy * S;

    for (let y = 0; y < S; y++) {
      for (let x = 0; x < S; x++) {
        const src = data.tiles[y][x];
        const gx = ox + x, gy = oy + y;
        tiles[gy][gx] = {
          ...src,
          height: (src as TileData & { height?: number }).height ?? 0,
          x: gx,
          y: gy,
        };
        if (src.obstacle === 'chest') chests.push({ x: gx, y: gy });
      }
    }

    // Vertex heights: chunk grid is (S+1)×(S+1); edge rows/columns are shared
    // with neighbors. Last writer wins; warn when an already-written shared
    // vertex disagrees so authors know two chunk edges don't line up.
    const vh = data.vertexHeights;
    if (vh) {
      for (let vy = 0; vy <= S; vy++) {
        for (let vx = 0; vx <= S; vx++) {
          const gi = (oy + vy) * (gw + 1) + (ox + vx);
          const v = vh[vy * (S + 1) + vx] ?? 0;
          const prevOwner = vertexOwner.get(gi);
          if (prevOwner !== undefined && Math.abs(vertexHeights[gi] - v) > 1e-4) {
            mismatches.push(`(${ox + vx},${oy + vy}) ${prevOwner}=${vertexHeights[gi].toFixed(3)} vs ${chunk.mapFile}=${v.toFixed(3)}`);
          }
          vertexHeights[gi] = v;
          vertexOwner.set(gi, chunk.mapFile);
        }
      }
    }

    for (const n of data.npcSpawns ?? []) npcSpawns.push({ ...n, tileX: n.tileX + ox, tileY: n.tileY + oy });
    for (const p of data.permanentItems ?? []) permanentItems.push({ ...p, x: p.x + ox, y: p.y + oy });
    for (const w of data.walls ?? []) walls.push({ ...w, tileX: w.tileX + ox, tileY: w.tileY + oy });
    // Legacy waterTiles migrate to full-tile water overlays (materialId 3),
    // matching the single-map path in GameLoop.
    const overlays = data.overlayTiles ?? (data.waterTiles ?? []).map(
      w => ({ tileX: w.tileX, tileY: w.tileY, shape: 0, materialId: 3 }),
    );
    for (const o of overlays) overlayTiles.push({ ...o, tileX: o.tileX + ox, tileY: o.tileY + oy });
  }

  if (mismatches.length > 0) {
    console.warn(`[assembleWorld] ${mismatches.length} shared edge-vertex height mismatch(es) — ` +
                 `seams will be visible. First few:\n  ${mismatches.slice(0, 5).join('\n  ')}`);
  }

  console.log(`[assembleWorld] assembled ${manifest.chunks.length} chunk(s) into ${gw}×${gh} world ` +
              `(${npcSpawns.length} NPC spawns, ${walls.length} walls, ${overlayTiles.length} overlays)`);

  return {
    width: gw,
    height: gh,
    tiles,
    vertexHeights,
    npcSpawns,
    permanentItems,
    overlayTiles,
    walls,
    spawn: { x: manifest.spawn.x, y: manifest.spawn.y },
    chests,
  };
}
