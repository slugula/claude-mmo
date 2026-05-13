import { EditorState } from './EditorState';
import { TilemapView }  from './TilemapView';
import { Preview3D }    from './Preview3D';
import { ModelViewer }  from './ModelViewer';
import { ContentBrowser } from './ContentBrowser';
import { PalettePanel }   from './PalettePanel';
import { MinimapView }    from './MinimapView';
import { FileOps }        from './FileOps';
import type { LayerType } from './EditorState';

export class Editor {
  private state:   EditorState;
  private tilemap: TilemapView;
  private preview: Preview3D;
  private model:   ModelViewer;
  private browser: ContentBrowser;
  private palette: PalettePanel;
  private minimap: MinimapView;
  private fileOps: FileOps;

  constructor() {
    this.state = new EditorState();

    // ---- Canvas elements -----------------------------------------------
    const tilemapCanvas  = document.getElementById('tilemap-canvas')  as HTMLCanvasElement;
    const previewCanvas  = document.getElementById('preview-canvas')  as HTMLCanvasElement;
    const modelCanvas    = document.getElementById('model-canvas')    as HTMLCanvasElement;
    const minimapCanvas  = document.getElementById('minimap-canvas')  as HTMLCanvasElement;

    // ---- Sub-components -----------------------------------------------
    this.tilemap = new TilemapView(tilemapCanvas, this.state);
    this.preview = new Preview3D(previewCanvas, this.state);
    this.model   = new ModelViewer(modelCanvas);
    this.minimap = new MinimapView(minimapCanvas, this.state);

    this.browser = new ContentBrowser(
      document.getElementById('browser-list')!,
      document.getElementById('browser-search') as HTMLInputElement,
      this.state,
    );

    this.palette = new PalettePanel(
      this.state,
      document.getElementById('swatches')!,
      document.getElementById('color-picker') as HTMLInputElement,
      document.getElementById('height-slider') as HTMLInputElement,
      document.getElementById('height-value')!,
      document.getElementById('terrain-controls')!,
      document.getElementById('height-controls')!,
      document.querySelectorAll('.brush-btn'),
    );

    this.fileOps = new FileOps(
      this.state,
      document.getElementById('filename-input') as HTMLInputElement,
      document.getElementById('file-input') as HTMLInputElement,
    );

    // ---- Wiring -------------------------------------------------------
    this.state.onChange = () => this.onStateChange();

    this.tilemap.onCoordChange = (x, y) => {
      const el = document.getElementById('coord-display');
      if (el) el.textContent = `${x}, ${y}`;
    };

    this.browser.onSelect = (id) => {
      this.model.load(id);
      // Auto-switch to objects layer when something is selected in browser
      if (this.state.activeLayer !== 'objects') {
        const sel = document.getElementById('layer-select') as HTMLSelectElement;
        sel.value = 'objects';
        this.handleLayerChange('objects');
      }
    };

    this.palette.onLayerChange = (layer) => this.handleLayerChange(layer);

    // ---- Toolbar buttons -----------------------------------------------
    document.getElementById('btn-new')  ?.addEventListener('click', () => { this.fileOps.newMap(); });
    document.getElementById('btn-save') ?.addEventListener('click', () => { this.fileOps.save(); });
    document.getElementById('btn-load') ?.addEventListener('click', () => { this.fileOps.openFilePicker(); });
    document.getElementById('btn-undo') ?.addEventListener('click', () => { this.state.undo(); });
    document.getElementById('btn-redo') ?.addEventListener('click', () => { this.state.redo(); });

    document.getElementById('layer-select')?.addEventListener('change', (e) => {
      this.handleLayerChange((e.target as HTMLSelectElement).value as LayerType);
    });

    // Keyboard shortcuts
    window.addEventListener('keydown', (e) => {
      if ((e.ctrlKey || e.metaKey) && e.key === 'z' && !e.shiftKey) { e.preventDefault(); this.state.undo(); }
      if ((e.ctrlKey || e.metaKey) && (e.key === 'y' || (e.key === 'z' && e.shiftKey))) { e.preventDefault(); this.state.redo(); }
      if ((e.ctrlKey || e.metaKey) && e.key === 's') { e.preventDefault(); this.fileOps.save(); }
    });

    // ---- Resize handling -----------------------------------------------
    const tilemapPane = document.getElementById('tilemap-pane')!;
    const ro = new ResizeObserver(() => {
      const w = tilemapPane.clientWidth;
      const h = tilemapPane.clientHeight;
      this.tilemap.resize(w, h);
    });
    ro.observe(tilemapPane);

    // Initial size
    this.tilemap.resize(tilemapPane.clientWidth, tilemapPane.clientHeight);

    // Initial undo button states
    this.updateUndoButtons();
  }

  private handleLayerChange(layer: LayerType): void {
    this.state.activeLayer = layer;
    this.palette.setLayer(layer);
    this.tilemap.markDirty();
  }

  private onStateChange(): void {
    this.tilemap.markDirty();
    this.minimap.render();
    this.preview.scheduleRefresh();
    this.updateUndoButtons();
  }

  private updateUndoButtons(): void {
    const undoBtn = document.getElementById('btn-undo') as HTMLButtonElement | null;
    const redoBtn = document.getElementById('btn-redo') as HTMLButtonElement | null;
    if (undoBtn) undoBtn.disabled = !this.state.canUndo;
    if (redoBtn) redoBtn.disabled = !this.state.canRedo;
  }
}
