import type { EditorState, LayerType, BrushType } from './EditorState';

const PRESET_SWATCHES = [
  '#7ec850', // grass green
  '#4a8c1c', // dark green
  '#c8a850', // sandy yellow
  '#a07040', // dirt brown
  '#808080', // stone gray
  '#505050', // dark stone
  '#1878e5', // water blue
  '#8b4513', // rich earth
  '#d4c090', // sand
  '#5a3010', // dark earth
  '#ffffff', // snow
  '#333333', // ash/dark
];

export class PalettePanel {
  private state: EditorState;
  private swatchGrid:     HTMLElement;
  private colorPicker:    HTMLInputElement;
  private heightSlider:   HTMLInputElement;
  private heightValue:    HTMLElement;
  private terrainSection: HTMLElement;
  private heightSection:  HTMLElement;
  private brushButtons:   NodeListOf<Element>;

  onLayerChange: ((layer: LayerType) => void) | null = null;

  constructor(
    state:          EditorState,
    swatchGrid:     HTMLElement,
    colorPicker:    HTMLInputElement,
    heightSlider:   HTMLInputElement,
    heightValue:    HTMLElement,
    terrainSection: HTMLElement,
    heightSection:  HTMLElement,
    brushButtons:   NodeListOf<Element>,
  ) {
    this.state          = state;
    this.swatchGrid     = swatchGrid;
    this.colorPicker    = colorPicker;
    this.heightSlider   = heightSlider;
    this.heightValue    = heightValue;
    this.terrainSection = terrainSection;
    this.heightSection  = heightSection;
    this.brushButtons   = brushButtons;

    this.buildSwatches();
    this.bindEvents();
    this.syncVisibility();
  }

  setLayer(layer: LayerType): void {
    this.state.activeLayer = layer;
    this.syncVisibility();
    this.onLayerChange?.(layer);
  }

  private syncVisibility(): void {
    const isHeight = this.state.activeLayer === 'height';
    this.terrainSection.style.display = isHeight ? 'none'  : 'block';
    this.heightSection.style.display  = isHeight ? 'block' : 'none';
  }

  private buildSwatches(): void {
    this.swatchGrid.innerHTML = '';
    for (const color of PRESET_SWATCHES) {
      const swatch = document.createElement('div');
      swatch.className = 'swatch';
      swatch.style.backgroundColor = color;
      if (color === this.state.selectedColor) swatch.classList.add('active');

      swatch.addEventListener('click', () => {
        this.state.selectedColor = color;
        this.colorPicker.value   = color;
        this.updateSwatchActive(color);
      });

      this.swatchGrid.appendChild(swatch);
    }
  }

  private updateSwatchActive(color: string): void {
    this.swatchGrid.querySelectorAll('.swatch').forEach((el) => {
      const div = el as HTMLElement;
      div.classList.toggle('active', div.style.backgroundColor === hexToRgbStr(color));
    });
  }

  private bindEvents(): void {
    this.colorPicker.addEventListener('input', () => {
      this.state.selectedColor = this.colorPicker.value;
      this.updateSwatchActive(this.colorPicker.value);
    });

    this.heightSlider.addEventListener('input', () => {
      const val = parseInt(this.heightSlider.value, 10);
      this.state.selectedHeight = val / 100;
      this.heightValue.textContent = `${val}%`;
    });

    this.brushButtons.forEach((btn) => {
      btn.addEventListener('click', () => {
        const brushId = (btn as HTMLElement).dataset['brush'] as BrushType;
        this.state.brushType = brushId;
        this.brushButtons.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
      });
    });
  }
}

function hexToRgbStr(hex: string): string {
  const r = parseInt(hex.slice(1, 3), 16);
  const g = parseInt(hex.slice(3, 5), 16);
  const b = parseInt(hex.slice(5, 7), 16);
  return `rgb(${r}, ${g}, ${b})`;
}
