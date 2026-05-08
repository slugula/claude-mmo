import type { EditorState } from './EditorState';
import type { WorldMapFile } from '../shared/types';

export class FileOps {
  private state:       EditorState;
  private filenameEl:  HTMLInputElement;
  private fileInputEl: HTMLInputElement;

  constructor(state: EditorState, filenameEl: HTMLInputElement, fileInputEl: HTMLInputElement) {
    this.state       = state;
    this.filenameEl  = filenameEl;
    this.fileInputEl = fileInputEl;

    this.fileInputEl.addEventListener('change', () => this.handleFileSelected());
  }

  newMap(): void {
    if (!confirm('Start a new blank 256×256 map? Unsaved changes will be lost.')) return;
    this.state.createBlank();
    this.filenameEl.value = 'worldMap.json';
  }

  save(): void {
    const data = this.state.toMapFile();
    const json = JSON.stringify(data, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url  = URL.createObjectURL(blob);

    const a = document.createElement('a');
    a.href     = url;
    a.download = this.filenameEl.value || 'worldMap.json';
    a.click();
    URL.revokeObjectURL(url);
  }

  openFilePicker(): void {
    this.fileInputEl.click();
  }

  private handleFileSelected(): void {
    const file = this.fileInputEl.files?.[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      try {
        const data = JSON.parse(e.target!.result as string) as WorldMapFile;
        this.state.loadFromFile(data);
        this.filenameEl.value = file.name;
      } catch {
        alert('Failed to parse map file. Make sure it is a valid worldMap.json.');
      }
    };
    reader.readAsText(file);

    // Reset so the same file can be re-loaded
    this.fileInputEl.value = '';
  }
}
