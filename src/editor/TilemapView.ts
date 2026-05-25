import type { EditorState } from './EditorState';

// Colors for overlay symbols
const SPAWN_CROSS_COLOR  = '#ffffff';
const GRID_LINE_COLOR    = 'rgba(255,255,255,0.08)';
const GRID_MAJOR_COLOR   = 'rgba(255,255,255,0.20)';
const CURSOR_COLOR       = 'rgba(255,255,255,0.45)';
const ERASE_CURSOR_COLOR = 'rgba(255,80,80,0.65)';

// Icon colors for placed objects / NPC spawns
const ICON_COLORS: Record<string, string> = {
  tree:         '#22cc33',
  rock:         '#8b5c2a',
  chest:        '#ff8800',
  fishing_spot: '#ffffff',
  npc:          '#ffff00',  // NPC spawns
};

export class TilemapView {
  private canvas: HTMLCanvasElement;
  private ctx:    CanvasRenderingContext2D;
  private state:  EditorState;

  // Pan / zoom
  private offsetX = 0;
  private offsetY = 0;
  private zoom    = 2;      // pixels per tile

  // Interaction
  private isPainting  = false;
  private isErasing   = false;
  private isPanning   = false;
  private panStartX   = 0;
  private panStartY   = 0;
  private panOffsetX  = 0;
  private panOffsetY  = 0;
  private cursorTileX = -1;
  private cursorTileY = -1;

  // Erase mode: toggled by pressing E; when active left-click erases instead of paints
  eraseMode = false;

  // Off-screen tile cache
  private tileCache:     OffscreenCanvas | null = null;
  private tileCacheDirty = true;

  onCoordChange: ((x: number, y: number) => void) | null = null;

  constructor(canvas: HTMLCanvasElement, state: EditorState) {
    this.canvas = canvas;
    this.ctx    = canvas.getContext('2d')!;
    this.state  = state;

    this.bindEvents();
    this.fitToContent();
  }

  private fitToContent(): void {
    const pane = this.canvas.parentElement!;
    const w = pane.clientWidth;
    const h = pane.clientHeight;
    // Choose zoom so the whole map fits
    const zoomFit = Math.min(w / this.state.width, h / this.state.height);
    this.zoom    = Math.max(1, Math.floor(zoomFit));
    this.offsetX = Math.floor((w - this.state.width  * this.zoom) / 2);
    this.offsetY = Math.floor((h - this.state.height * this.zoom) / 2);
  }

  resize(w: number, h: number): void {
    const dpr = window.devicePixelRatio || 1;
    this.canvas.width  = w * dpr;
    this.canvas.height = h * dpr;
    this.canvas.style.width  = `${w}px`;
    this.canvas.style.height = `${h}px`;
    this.ctx.scale(dpr, dpr);
    this.tileCacheDirty = true;
    this.render();
  }

  markDirty(): void {
    this.tileCacheDirty = true;
    this.render();
  }

  render(): void {
    const W = this.canvas.clientWidth;
    const H = this.canvas.clientHeight;
    this.ctx.clearRect(0, 0, W, H);
    this.ctx.fillStyle = '#111';
    this.ctx.fillRect(0, 0, W, H);

    this.rebuildCacheIfNeeded();
    if (this.tileCache) {
      this.ctx.imageSmoothingEnabled = false;
      this.ctx.drawImage(
        this.tileCache as unknown as HTMLCanvasElement,
        0, 0, this.tileCache.width, this.tileCache.height,
        this.offsetX, this.offsetY,
        this.state.width * this.zoom,
        this.state.height * this.zoom,
      );
    }

    this.drawGridLines(W, H);
    this.drawObjectIcons();
    this.drawOverlays();
    this.drawCursor();
  }

  private rebuildCacheIfNeeded(): void {
    if (!this.tileCacheDirty && this.tileCache) return;

    const W = this.state.width;
    const H = this.state.height;
    this.tileCache = new OffscreenCanvas(W, H);
    const ctx = this.tileCache.getContext('2d')!;

    for (let ty = 0; ty < H; ty++) {
      for (let tx = 0; tx < W; tx++) {
        const tile = this.state.tiles[ty]?.[tx];
        if (!tile) continue;

        let color = tile.groundColor;

        // Height overlay: shade tile by average of its 4 corner vertex heights.
        // Vertex row for tile ty: row H-ty (lower-z edge) and H-ty-1 (upper-z edge),
        // matching computeVertexHeight's convention in the game engine.
        if (this.state.activeLayer === 'height') {
          const W = this.state.width;
          const H = this.state.height;
          const h = (
            (this.state.vertexHeights[(H - ty)     * (W + 1) + tx]     ?? 0) +
            (this.state.vertexHeights[(H - ty)     * (W + 1) + tx + 1] ?? 0) +
            (this.state.vertexHeights[(H - ty - 1) * (W + 1) + tx]     ?? 0) +
            (this.state.vertexHeights[(H - ty - 1) * (W + 1) + tx + 1] ?? 0)
          ) / 4;
          color = blendColor(tile.groundColor, h);
        }

        ctx.fillStyle = color;
        ctx.fillRect(tx, ty, 1, 1);
      }
    }

    this.tileCacheDirty = false;
  }

  private drawGridLines(W: number, H: number): void {
    if (this.zoom < 3) return; // too zoomed out to show grid

    const ctx = this.ctx;
    const step = this.zoom;

    ctx.beginPath();
    // Minor lines every tile
    ctx.strokeStyle = GRID_LINE_COLOR;
    ctx.lineWidth   = 0.5;
    const startTX = Math.max(0, Math.floor(-this.offsetX / step));
    const endTX   = Math.min(this.state.width,  Math.ceil((W - this.offsetX) / step));
    const startTY = Math.max(0, Math.floor(-this.offsetY / step));
    const endTY   = Math.min(this.state.height, Math.ceil((H - this.offsetY) / step));

    for (let tx = startTX; tx <= endTX; tx++) {
      const sx = this.offsetX + tx * step;
      ctx.moveTo(sx, this.offsetY + startTY * step);
      ctx.lineTo(sx, this.offsetY + endTY   * step);
    }
    for (let ty = startTY; ty <= endTY; ty++) {
      const sy = this.offsetY + ty * step;
      ctx.moveTo(this.offsetX + startTX * step, sy);
      ctx.lineTo(this.offsetX + endTX   * step, sy);
    }
    ctx.stroke();

    // Major lines every 16 tiles
    if (this.zoom >= 2) {
      ctx.beginPath();
      ctx.strokeStyle = GRID_MAJOR_COLOR;
      ctx.lineWidth   = 1;
      for (let tx = 0; tx <= this.state.width; tx += 16) {
        const sx = this.offsetX + tx * step;
        if (sx < -1 || sx > W + 1) continue;
        ctx.moveTo(sx, 0); ctx.lineTo(sx, H);
      }
      for (let ty = 0; ty <= this.state.height; ty += 16) {
        const sy = this.offsetY + ty * step;
        if (sy < -1 || sy > H + 1) continue;
        ctx.moveTo(0, sy); ctx.lineTo(W, sy);
      }
      ctx.stroke();
    }
  }

  private visibleTileRange(): { startTX: number; endTX: number; startTY: number; endTY: number } {
    const W = this.canvas.clientWidth;
    const H = this.canvas.clientHeight;
    const step = this.zoom;
    return {
      startTX: Math.max(0, Math.floor(-this.offsetX / step)),
      endTX:   Math.min(this.state.width  - 1, Math.ceil((W - this.offsetX) / step)),
      startTY: Math.max(0, Math.floor(-this.offsetY / step)),
      endTY:   Math.min(this.state.height - 1, Math.ceil((H - this.offsetY) / step)),
    };
  }

  private drawObjectIcons(): void {
    if (this.zoom < 2) return;

    const ctx  = this.ctx;
    const step = this.zoom;
    const { startTX, endTX, startTY, endTY } = this.visibleTileRange();

    // Obstacle circles (tree / rock / chest / fishing_spot)
    for (let ty = startTY; ty <= endTY; ty++) {
      for (let tx = startTX; tx <= endTX; tx++) {
        const tile = this.state.tiles[ty]?.[tx];
        if (!tile || tile.obstacle === 'none') continue;
        const color = ICON_COLORS[tile.obstacle];
        if (!color) continue;
        const sx = this.offsetX + tx * step + step / 2;
        const sy = this.offsetY + ty * step + step / 2;
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(sx, sy, Math.max(1.5, step / 4), 0, Math.PI * 2);
        ctx.fill();
      }
    }

    // NPC spawns — yellow circles
    ctx.fillStyle = ICON_COLORS.npc;
    for (const spawn of this.state.npcSpawns) {
      if (spawn.x < startTX || spawn.x > endTX || spawn.y < startTY || spawn.y > endTY) continue;
      const sx = this.offsetX + spawn.x * step + step / 2;
      const sy = this.offsetY + spawn.y * step + step / 2;
      ctx.beginPath();
      ctx.arc(sx, sy, Math.max(1.5, step / 5), 0, Math.PI * 2);
      ctx.fill();
    }
  }

  private drawOverlays(): void {
    if (this.zoom < 3) return;

    const ctx  = this.ctx;
    const step = this.zoom;

    // Player spawn cross at center
    const spawnX = Math.floor(this.state.width  / 2);
    const spawnY = Math.floor(this.state.height / 2);
    const cx = this.offsetX + spawnX * step + step / 2;
    const cy = this.offsetY + spawnY * step + step / 2;
    const arm = Math.max(2, step / 3);
    ctx.strokeStyle = SPAWN_CROSS_COLOR;
    ctx.lineWidth   = Math.max(1, step / 8);
    ctx.beginPath();
    ctx.moveTo(cx - arm, cy); ctx.lineTo(cx + arm, cy);
    ctx.moveTo(cx, cy - arm); ctx.lineTo(cx, cy + arm);
    ctx.stroke();
  }

  private drawCursor(): void {
    const tx = this.cursorTileX;
    const ty = this.cursorTileY;
    if (tx < 0 || ty < 0 || tx >= this.state.width || ty >= this.state.height) return;

    const step = this.zoom;
    const offsets = this.brushPixelOffsets();
    this.ctx.strokeStyle = this.eraseMode ? ERASE_CURSOR_COLOR : CURSOR_COLOR;
    this.ctx.lineWidth   = 1;

    for (const [dx, dy] of offsets) {
      const bx = tx + dx, by = ty + dy;
      if (bx < 0 || by < 0 || bx >= this.state.width || by >= this.state.height) continue;
      this.ctx.strokeRect(
        this.offsetX + bx * step + 0.5,
        this.offsetY + by * step + 0.5,
        step - 1, step - 1,
      );
    }
  }

  private brushPixelOffsets(): Array<[number, number]> {
    switch (this.state.brushType) {
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

  private screenToTile(sx: number, sy: number): [number, number] {
    const tx = Math.floor((sx - this.offsetX) / this.zoom);
    const ty = Math.floor((sy - this.offsetY) / this.zoom);
    return [tx, ty];
  }

  private bindEvents(): void {
    const c = this.canvas;

    c.addEventListener('contextmenu', e => e.preventDefault());

    // E key toggles erase mode
    window.addEventListener('keydown', (e) => {
      if ((e.target as HTMLElement).tagName === 'INPUT') return;
      if (e.key === 'e' || e.key === 'E') {
        this.eraseMode = !this.eraseMode;
        this.render();
      }
    });

    c.addEventListener('wheel', (e) => {
      e.preventDefault();
      const rect = c.getBoundingClientRect();
      const mx = e.clientX - rect.left;
      const my = e.clientY - rect.top;

      const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
      const newZoom = Math.max(1, Math.min(64, this.zoom * factor));

      // Zoom toward mouse position
      this.offsetX = mx - (mx - this.offsetX) * (newZoom / this.zoom);
      this.offsetY = my - (my - this.offsetY) * (newZoom / this.zoom);
      this.zoom = newZoom;
      this.render();
    }, { passive: false });

    c.addEventListener('mousedown', (e) => {
      const rect = c.getBoundingClientRect();
      const sx = e.clientX - rect.left;
      const sy = e.clientY - rect.top;

      // Right-click or middle-click or alt+left: pan
      if (e.button === 2 || e.button === 1 || (e.button === 0 && e.altKey)) {
        this.isPanning  = true;
        this.panStartX  = e.clientX;
        this.panStartY  = e.clientY;
        this.panOffsetX = this.offsetX;
        this.panOffsetY = this.offsetY;
        e.preventDefault();
        return;
      }

      if (e.button === 0) {
        const [tx, ty] = this.screenToTile(sx, sy);
        this.state.beginStroke();
        if (this.eraseMode) {
          this.isErasing = true;
          this.state.eraseAt(tx, ty);
        } else {
          this.isPainting = true;
          this.state.paintAt(tx, ty);
        }
        this.render();
      }
    });

    window.addEventListener('mousemove', (e) => {
      const rect = c.getBoundingClientRect();
      const sx = e.clientX - rect.left;
      const sy = e.clientY - rect.top;

      if (this.isPanning) {
        this.offsetX = this.panOffsetX + (e.clientX - this.panStartX);
        this.offsetY = this.panOffsetY + (e.clientY - this.panStartY);
        this.render();
        return;
      }

      const [tx, ty] = this.screenToTile(sx, sy);
      this.cursorTileX = tx;
      this.cursorTileY = ty;
      this.onCoordChange?.(tx, ty);

      if (this.isPainting) {
        this.state.paintAt(tx, ty);
      } else if (this.isErasing) {
        this.state.eraseAt(tx, ty);
      } else {
        this.render(); // just redraw cursor
        return;
      }
      this.render();
    });

    window.addEventListener('mouseup', (e) => {
      if (e.button === 2 || e.button === 1 || (e.button === 0 && this.isPanning)) {
        this.isPanning = false;
        return;
      }
      if (this.isPainting || this.isErasing) {
        this.state.endStroke();
        this.isPainting = false;
        this.isErasing  = false;
      }
    });

    c.addEventListener('mouseleave', () => {
      this.cursorTileX = -1;
      this.cursorTileY = -1;
      this.render();
    });
  }
}

function blendColor(hex: string, amount: number): string {
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  if (amount > 0.5) {
    // Brighten toward white
    const t = (amount - 0.5) * 2;
    return `rgb(${Math.round(r + (255 - r) * t * 0.5)},${Math.round(g + (255 - g) * t * 0.5)},${Math.round(b + (255 - b) * t * 0.5)})`;
  } else {
    // Darken toward black
    const t = (0.5 - amount) * 2;
    return `rgb(${Math.round(r * (1 - t * 0.6))},${Math.round(g * (1 - t * 0.6))},${Math.round(b * (1 - t * 0.6))})`;
  }
}
