import type { TileData, NPCSpawn, PermanentItemSpawn, WorldMapFile } from '../shared/types';
import { GRID_WIDTH, GRID_HEIGHT } from '../shared/constants';

export type LayerType = 'terrain' | 'height' | 'objects';
export type BrushType = '1' | '3' | '5' | 'c3' | 'c5';

export interface TileChange {
  x: number;
  y: number;
  before: TileData;
  after: TileData;
}

interface VertexChange {
  idx: number;
  before: number;
  after: number;
}

interface EditCommand {
  tileChanges:    TileChange[];
  vertexChanges?: VertexChange[];
  npcsBefore?:    NPCSpawn[];
  npcsAfter?:     NPCSpawn[];
}

function blankTile(x: number, y: number): TileData {
  return { x, y, walkable: true, type: 'grass', obstacle: '', blocksRanged: false, groundColor: '#7ec850', height: 0 };
}

function createBlankTiles(w: number, h: number): TileData[][] {
  return Array.from({ length: h }, (_, y) =>
    Array.from({ length: w }, (_, x) => blankTile(x, y)),
  );
}

// Derive per-vertex heights from legacy per-tile heights (backward compat migration).
function migrateVertexHeights(tiles: TileData[][], W: number, H: number): Float32Array {
  const vh = new Float32Array((W + 1) * (H + 1));
  for (let row = 0; row <= H; row++) {
    for (let col = 0; col <= W; col++) {
      let sum = 0, count = 0;
      for (const tx of [col - 1, col]) {
        for (const ty of [H - row - 1, H - row]) {
          if (tx < 0 || tx >= W || ty < 0 || ty >= H) continue;
          const tile = tiles[ty]?.[tx];
          if (!tile || tile.type === 'water') continue;
          sum += (tile.height ?? 0);
          count++;
        }
      }
      vh[row * (W + 1) + col] = count > 0 ? sum / count : 0;
    }
  }
  return vh;
}

const OBSTACLE_TYPES = new Set(['tree', 'rock', 'chest', 'fishing_spot', 'water', 'wall']);

export class EditorState {
  width  = GRID_WIDTH;
  height = GRID_HEIGHT;
  tiles: TileData[][];
  // Per-vertex heights — (width+1)*(height+1) flat Float32Array, row-major.
  vertexHeights: Float32Array;
  npcSpawns: NPCSpawn[] = [];
  permanentItems: PermanentItemSpawn[] = [];

  activeLayer:    LayerType = 'terrain';
  selectedColor   = '#7ec850';
  selectedHeight  = 0.5;
  selectedObject: string | null = null;
  brushType: BrushType = '1';

  private undoStack: EditCommand[] = [];
  private redoStack: EditCommand[] = [];
  private strokeTiles    = new Map<string, TileChange>();
  private strokeVertices = new Map<number, VertexChange>();
  private strokeNPCsBefore: NPCSpawn[] | null = null;
  private inStroke = false;

  onChange: (() => void) | null = null;

  constructor() {
    this.tiles         = createBlankTiles(this.width, this.height);
    this.vertexHeights = new Float32Array((this.width + 1) * (this.height + 1));
  }

  // ---- Map lifecycle --------------------------------------------------------

  createBlank(w = GRID_WIDTH, h = GRID_HEIGHT): void {
    this.width         = w;
    this.height        = h;
    this.tiles         = createBlankTiles(w, h);
    this.vertexHeights = new Float32Array((w + 1) * (h + 1));
    this.npcSpawns      = [];
    this.permanentItems = [];
    this.undoStack = [];
    this.redoStack = [];
    this.onChange?.();
  }

  loadFromFile(data: WorldMapFile): void {
    this.width  = data.width;
    this.height = data.height;
    this.tiles  = data.tiles.map(row => row.map(t => ({ ...t, height: (t as TileData & { height?: number }).height ?? 0 })));
    this.npcSpawns      = data.npcSpawns      ?? [];
    this.permanentItems = data.permanentItems  ?? [];
    this.vertexHeights  = data.vertexHeights
      ? Float32Array.from(data.vertexHeights)
      : migrateVertexHeights(this.tiles, this.width, this.height);
    this.undoStack = [];
    this.redoStack = [];
    this.onChange?.();
  }

  toMapFile(): WorldMapFile {
    // Back-fill TileData.height with averaged vertex corners for backward compat.
    const W = this.width, H = this.height;
    const tiles = this.tiles.map(row => row.map(t => {
      const tx = t.x, ty = t.y;
      const avgH = (
        (this.vertexHeights[(H - ty)     * (W + 1) + tx]     ?? 0) +
        (this.vertexHeights[(H - ty)     * (W + 1) + tx + 1] ?? 0) +
        (this.vertexHeights[(H - ty - 1) * (W + 1) + tx]     ?? 0) +
        (this.vertexHeights[(H - ty - 1) * (W + 1) + tx + 1] ?? 0)
      ) / 4;
      return { ...t, height: avgH };
    }));
    return {
      version: 2,
      width:   this.width,
      height:  this.height,
      tiles,
      npcSpawns:      this.npcSpawns,
      permanentItems: this.permanentItems,
      vertexHeights:  Array.from(this.vertexHeights),
    };
  }

  // ---- Stroke API (one undo entry per mousedown→mouseup) --------------------

  beginStroke(): void {
    this.inStroke = true;
    this.strokeTiles.clear();
    this.strokeVertices.clear();
    this.strokeNPCsBefore = this.activeLayer === 'objects'
      ? [...this.npcSpawns]
      : null;
  }

  endStroke(): void {
    if (!this.inStroke) return;
    this.inStroke = false;

    const tileChanges   = [...this.strokeTiles.values()];
    const vertexChanges = [...this.strokeVertices.values()];
    const npcsChanged   = this.strokeNPCsBefore !== null &&
      JSON.stringify(this.strokeNPCsBefore) !== JSON.stringify(this.npcSpawns);

    if (tileChanges.length > 0 || vertexChanges.length > 0 || npcsChanged) {
      const cmd: EditCommand = { tileChanges };
      if (vertexChanges.length > 0) cmd.vertexChanges = vertexChanges;
      if (npcsChanged) {
        cmd.npcsBefore = this.strokeNPCsBefore!;
        cmd.npcsAfter  = [...this.npcSpawns];
      }
      this.undoStack.push(cmd);
      if (this.undoStack.length > 50) this.undoStack.shift();
      this.redoStack = [];
    }

    this.strokeTiles.clear();
    this.strokeVertices.clear();
    this.strokeNPCsBefore = null;
  }

  // ---- Painting -------------------------------------------------------------

  paintAt(tx: number, ty: number): void {
    for (const [dx, dy] of this.brushOffsets()) {
      const x = tx + dx, y = ty + dy;
      if (x < 0 || y < 0 || x >= this.width || y >= this.height) continue;
      this.applyPaint(x, y);
    }
    this.onChange?.();
  }

  eraseAt(tx: number, ty: number): void {
    for (const [dx, dy] of this.brushOffsets()) {
      const x = tx + dx, y = ty + dy;
      if (x < 0 || y < 0 || x >= this.width || y >= this.height) continue;
      this.applyErase(x, y);
    }
    this.onChange?.();
  }

  private recordTile(x: number, y: number, before: TileData): void {
    const key = `${x},${y}`;
    if (!this.strokeTiles.has(key)) {
      this.strokeTiles.set(key, { x, y, before, after: { ...this.tiles[y][x] } });
    } else {
      this.strokeTiles.get(key)!.after = { ...this.tiles[y][x] };
    }
  }

  private recordVertex(idx: number, before: number): void {
    if (!this.strokeVertices.has(idx)) {
      this.strokeVertices.set(idx, { idx, before, after: this.vertexHeights[idx] });
    } else {
      this.strokeVertices.get(idx)!.after = this.vertexHeights[idx];
    }
  }

  private applyPaint(x: number, y: number): void {
    const before = { ...this.tiles[y][x] };

    if (this.activeLayer === 'terrain') {
      this.tiles[y][x] = { ...this.tiles[y][x], groundColor: this.selectedColor };
      this.recordTile(x, y, before);

    } else if (this.activeLayer === 'height') {
      this.paintVertexHeight(x, y, this.selectedHeight);

    } else if (this.activeLayer === 'objects' && this.selectedObject) {
      this.applyObject(x, y, this.selectedObject);
      this.recordTile(x, y, before);
    }
  }

  // Set all 4 corner vertices of tile (tx, ty) to the given 0-1 height value.
  // Vertex row for tile ty: Babylon ground mesh row 0 sits at world z = H-0.5 (far end),
  // so tile ty's lower-z edge = vertex row H-ty, upper-z edge = vertex row H-ty-1.
  private paintVertexHeight(tx: number, ty: number, h: number): void {
    const W = this.width;
    const H = this.height;
    const corners = [
      (H - ty)     * (W + 1) + tx,
      (H - ty)     * (W + 1) + tx + 1,
      (H - ty - 1) * (W + 1) + tx,
      (H - ty - 1) * (W + 1) + tx + 1,
    ];
    for (const idx of corners) {
      if (idx < 0 || idx >= this.vertexHeights.length) continue;
      const prev = this.vertexHeights[idx];
      this.recordVertex(idx, prev);
      this.vertexHeights[idx] = h;
    }
  }

  private applyErase(x: number, y: number): void {
    const before = { ...this.tiles[y][x] };

    if (this.activeLayer === 'terrain') {
      this.tiles[y][x] = { ...this.tiles[y][x], groundColor: '#7ec850' };
      this.recordTile(x, y, before);
    } else if (this.activeLayer === 'height') {
      this.paintVertexHeight(x, y, 0);
    } else if (this.activeLayer === 'objects') {
      this.tiles[y][x] = {
        ...this.tiles[y][x],
        obstacle: '', walkable: true, blocksRanged: false,
        type: 'grass',
      };
      this.npcSpawns = this.npcSpawns.filter(s => !(s.x === x && s.y === y));
      this.recordTile(x, y, before);
    }
  }

  private applyObject(x: number, y: number, obj: string): void {
    if (!OBSTACLE_TYPES.has(obj)) {
      if (!this.npcSpawns.some(s => s.x === x && s.y === y)) {
        this.npcSpawns = [...this.npcSpawns, { kind: obj, x, y }];
      }
      return;
    }

    if (obj === 'water') {
      this.tiles[y][x] = { ...this.tiles[y][x], type: 'water', walkable: false, obstacle: '', blocksRanged: false, groundColor: '#1878e5' };
    } else if (obj === 'wall') {
      this.tiles[y][x] = { ...this.tiles[y][x], type: 'wall', walkable: false, obstacle: '', blocksRanged: true };
    } else {
      this.tiles[y][x] = {
        ...this.tiles[y][x],
        obstacle: obj as TileData['obstacle'],
        walkable: false,
        blocksRanged: obj === 'tree',
        type: 'grass',
      };
    }
  }

  // ---- Undo / Redo ----------------------------------------------------------

  undo(): void {
    const cmd = this.undoStack.pop();
    if (!cmd) return;
    for (const { x, y, before } of cmd.tileChanges) {
      this.tiles[y][x] = { ...before };
    }
    if (cmd.vertexChanges) {
      for (const { idx, before } of cmd.vertexChanges) {
        this.vertexHeights[idx] = before;
      }
    }
    if (cmd.npcsBefore) this.npcSpawns = [...cmd.npcsBefore];
    this.redoStack.push(cmd);
    this.onChange?.();
  }

  redo(): void {
    const cmd = this.redoStack.pop();
    if (!cmd) return;
    for (const { x, y, after } of cmd.tileChanges) {
      this.tiles[y][x] = { ...after };
    }
    if (cmd.vertexChanges) {
      for (const { idx, after } of cmd.vertexChanges) {
        this.vertexHeights[idx] = after;
      }
    }
    if (cmd.npcsAfter) this.npcSpawns = [...cmd.npcsAfter];
    this.undoStack.push(cmd);
    this.onChange?.();
  }

  get canUndo(): boolean { return this.undoStack.length > 0; }
  get canRedo(): boolean { return this.redoStack.length > 0; }

  // ---- Helpers --------------------------------------------------------------

  private brushOffsets(): Array<[number, number]> {
    switch (this.brushType) {
      case '3':  return this.square(1);
      case '5':  return this.square(2);
      case 'c3': return this.circle(1.5);
      case 'c5': return this.circle(2.5);
      default:   return [[0, 0]];
    }
  }

  private square(r: number): Array<[number, number]> {
    const out: Array<[number, number]> = [];
    for (let dy = -r; dy <= r; dy++)
      for (let dx = -r; dx <= r; dx++)
        out.push([dx, dy]);
    return out;
  }

  private circle(r: number): Array<[number, number]> {
    const out: Array<[number, number]> = [];
    const ri = Math.ceil(r);
    for (let dy = -ri; dy <= ri; dy++)
      for (let dx = -ri; dx <= ri; dx++)
        if (Math.sqrt(dx * dx + dy * dy) <= r) out.push([dx, dy]);
    return out;
  }
}
