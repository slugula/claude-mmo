import type { EditorState } from './EditorState';
import npcData from '../data/npcs.json';

interface BrowserEntry {
  id:      string;
  label:   string;
  section: string;
}

const STATIC_ENTRIES: BrowserEntry[] = [
  // Objects
  { id: 'tree',         label: 'Tree',         section: 'Objects' },
  { id: 'rock',         label: 'Rock',         section: 'Objects' },
  { id: 'chest',        label: 'Bank Chest',   section: 'Objects' },
  { id: 'fishing_spot', label: 'Fishing Spot', section: 'Objects' },
  // Terrain types
  { id: 'water',        label: 'Water Tile',   section: 'Terrain' },
  { id: 'wall',         label: 'Wall',         section: 'Terrain' },
];

export class ContentBrowser {
  private listEl:  HTMLElement;
  private searchEl: HTMLInputElement;
  private state:   EditorState;
  private entries: BrowserEntry[];

  onSelect: ((id: string, label: string) => void) | null = null;

  constructor(listEl: HTMLElement, searchEl: HTMLInputElement, state: EditorState) {
    this.listEl   = listEl;
    this.searchEl = searchEl;
    this.state    = state;

    // Build NPC entries from npcs.json
    const npcEntries: BrowserEntry[] = (npcData as Array<{ kind: string; name: string }>).map(n => ({
      id:      n.kind,
      label:   n.name,
      section: 'NPCs',
    }));

    this.entries = [...STATIC_ENTRIES, ...npcEntries];

    this.searchEl.addEventListener('input', () => this.render());
    this.render();
  }

  render(): void {
    const query = this.searchEl.value.toLowerCase();
    const filtered = query
      ? this.entries.filter(e => e.label.toLowerCase().includes(query) || e.section.toLowerCase().includes(query))
      : this.entries;

    // Group by section
    const sections = new Map<string, BrowserEntry[]>();
    for (const entry of filtered) {
      if (!sections.has(entry.section)) sections.set(entry.section, []);
      sections.get(entry.section)!.push(entry);
    }

    this.listEl.innerHTML = '';

    for (const [section, items] of sections) {
      const header = document.createElement('div');
      header.className = 'browser-section-header';
      header.textContent = section;
      this.listEl.appendChild(header);

      for (const item of items) {
        const row = document.createElement('div');
        row.className = 'browser-item';
        row.textContent = item.label;
        row.dataset['id'] = item.id;

        if (this.state.selectedObject === item.id) row.classList.add('selected');

        row.addEventListener('click', () => {
          this.state.selectedObject = item.id;
          this.onSelect?.(item.id, item.label);
          this.render(); // refresh selected highlight
        });

        this.listEl.appendChild(row);
      }
    }
  }
}
