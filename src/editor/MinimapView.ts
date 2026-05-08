import type { EditorState } from './EditorState';

// Fixed override colors
const WATER_COLOR  = '#1878e5';
const TREE_COLOR   = '#1a5c1a';
const ROCK_COLOR   = '#666666';
const CHEST_COLOR  = '#cc9900';
const NPC_COLOR    = '#ff44aa';
const SPAWN_COLOR  = '#ffffff';

const DISPLAY_SIZE = 192; // px on screen (CSS)

export class MinimapView {
  private canvas: HTMLCanvasElement;
  private ctx:    CanvasRenderingContext2D;
  private state:  EditorState;

  constructor(canvas: HTMLCanvasElement, state: EditorState) {
    this.canvas = canvas;
    this.ctx    = canvas.getContext('2d')!;
    this.state  = state;

    // The canvas pixel size = map size (1px per tile), displayed scaled
    this.canvas.width  = state.width;
    this.canvas.height = state.height;
    this.canvas.style.width  = `${DISPLAY_SIZE}px`;
    this.canvas.style.height = `${DISPLAY_SIZE}px`;

    this.render();
  }

  render(): void {
    const W = this.state.width;
    const H = this.state.height;

    // Use ImageData for fast per-pixel writes
    const img = this.ctx.createImageData(W, H);
    const data = img.data;

    for (let ty = 0; ty < H; ty++) {
      for (let tx = 0; tx < W; tx++) {
        const tile = this.state.tiles[ty]?.[tx];
        if (!tile) continue;

        let color: string;
        if (tile.type === 'water') {
          color = WATER_COLOR;
        } else if (tile.obstacle === 'tree') {
          color = TREE_COLOR;
        } else if (tile.obstacle === 'rock') {
          color = ROCK_COLOR;
        } else if (tile.obstacle === 'chest') {
          color = CHEST_COLOR;
        } else {
          color = tile.groundColor;
        }

        const [r, g, b] = hexToRgb(color);
        const idx = (ty * W + tx) * 4;
        data[idx]     = r;
        data[idx + 1] = g;
        data[idx + 2] = b;
        data[idx + 3] = 255;
      }
    }

    this.ctx.putImageData(img, 0, 0);

    // NPC spawn dots (2×2 pixels)
    this.ctx.fillStyle = NPC_COLOR;
    for (const spawn of this.state.npcSpawns) {
      this.ctx.fillRect(spawn.x, spawn.y, 2, 2);
    }

    // Player spawn cross at map center
    const cx = Math.floor(W / 2);
    const cy = Math.floor(H / 2);
    this.ctx.fillStyle = SPAWN_COLOR;
    this.ctx.fillRect(cx - 3, cy,     7, 1);
    this.ctx.fillRect(cx,     cy - 3, 1, 7);
  }
}

function hexToRgb(hex: string): [number, number, number] {
  // Handle shorthand and full hex
  const clean = hex.replace('#', '');
  if (clean.length === 3) {
    return [
      parseInt(clean[0]! + clean[0]!, 16),
      parseInt(clean[1]! + clean[1]!, 16),
      parseInt(clean[2]! + clean[2]!, 16),
    ];
  }
  return [
    parseInt(clean.slice(0, 2), 16),
    parseInt(clean.slice(2, 4), 16),
    parseInt(clean.slice(4, 6), 16),
  ];
}
