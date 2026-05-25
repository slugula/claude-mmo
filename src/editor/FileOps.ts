import type { EditorState } from './EditorState';
import type { WorldMapFile } from '../shared/types';

// ---- IndexedDB handle persistence -------------------------------------------
// FileSystemFileHandle can be stored in IDB (not JSON-serializable) so the
// next showSaveFilePicker call opens to the same directory automatically.

const IDB_NAME  = 'editor-prefs';
const IDB_STORE = 'handles';
const IDB_KEY   = 'lastSaveHandle';

function openIDB(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(IDB_NAME, 1);
    req.onupgradeneeded = () => req.result.createObjectStore(IDB_STORE);
    req.onsuccess = () => resolve(req.result);
    req.onerror   = () => reject(req.error);
  });
}

async function getLastHandle(): Promise<FileSystemFileHandle | undefined> {
  try {
    const db = await openIDB();
    return await new Promise((resolve) => {
      const req = db.transaction(IDB_STORE, 'readonly').objectStore(IDB_STORE).get(IDB_KEY);
      req.onsuccess = () => resolve(req.result as FileSystemFileHandle | undefined);
      req.onerror   = () => resolve(undefined);
    });
  } catch { return undefined; }
}

async function setLastHandle(handle: FileSystemFileHandle): Promise<void> {
  try {
    const db = await openIDB();
    await new Promise<void>((resolve) => {
      const tx = db.transaction(IDB_STORE, 'readwrite');
      tx.objectStore(IDB_STORE).put(handle, IDB_KEY);
      tx.oncomplete = () => resolve();
      tx.onerror    = () => resolve();
    });
  } catch { /* non-fatal */ }
}

// ---- FileOps ----------------------------------------------------------------

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

  async save(): Promise<void> {
    const json = JSON.stringify(this.state.toMapFile(), null, 2);

    // Native file dialog via File System Access API (Chromium-based browsers)
    const picker = (window as unknown as Record<string, unknown>)['showSaveFilePicker'];
    if (typeof picker === 'function') {
      const lastHandle = await getLastHandle();

      let handle: FileSystemFileHandle;
      try {
        handle = await (picker as (opts: unknown) => Promise<FileSystemFileHandle>)({
          suggestedName: this.filenameEl.value || 'worldMap.json',
          types: [{ description: 'JSON Map File', accept: { 'application/json': ['.json'] } }],
          startIn: lastHandle ?? 'documents',
        });
      } catch (e: unknown) {
        if ((e as Error).name === 'AbortError') return; // user cancelled
        throw e;
      }

      const writable = await handle.createWritable();
      await writable.write(json);
      await writable.close();

      this.filenameEl.value = handle.name;
      await setLastHandle(handle);
      return;
    }

    // Fallback: blob download (Firefox / Safari)
    const blob = new Blob([json], { type: 'application/json' });
    const url  = URL.createObjectURL(blob);
    const a    = document.createElement('a');
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
