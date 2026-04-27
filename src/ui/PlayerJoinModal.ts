import type { GameAction, ShirtColor, SkinColor } from '../shared/types';
import { ChatLog } from './ChatLog';

const SHIRT_SWATCHES: { id: ShirtColor; hex: string; label: string }[] = [
  { id: 'red',    hex: '#cc2222', label: 'Red'    },
  { id: 'blue',   hex: '#2244cc', label: 'Blue'   },
  { id: 'yellow', hex: '#ccaa22', label: 'Yellow' },
  { id: 'green',  hex: '#228822', label: 'Green'  },
];

const SKIN_SWATCHES: { id: SkinColor; hex: string; label: string }[] = [
  { id: 'fair',  hex: '#f5d5b0', label: 'Fair'  },
  { id: 'tan',   hex: '#d4956a', label: 'Tan'   },
  { id: 'olive', hex: '#9c7246', label: 'Olive' },
  { id: 'brown', hex: '#6b4226', label: 'Brown' },
];

export class PlayerJoinModal {
  private readonly overlay: HTMLElement;
  private readonly nameInput: HTMLInputElement;
  private readonly joinBtn: HTMLButtonElement;

  private _shirtColor: ShirtColor = 'blue';
  private _skinColor:  SkinColor  = 'fair';

  private readonly dispatch: (action: GameAction) => void;
  private joinCallback: ((name: string, shirtColor: ShirtColor, skinColor: SkinColor) => void) | null = null;

  // ---- Getters (single source of truth for in-flight appearance) ----

  get currentName(): string {
    return this.nameInput.value.trim().slice(0, 20) || 'Player';
  }
  get currentShirtColor(): ShirtColor { return this._shirtColor; }
  get currentSkinColor():  SkinColor  { return this._skinColor; }

  constructor(dispatch: (action: GameAction) => void) {
    this.dispatch = dispatch;
    this.overlay  = this.buildOverlay();
    this.nameInput = this.overlay.querySelector<HTMLInputElement>('#join-name-input')!;
    this.joinBtn   = this.overlay.querySelector<HTMLButtonElement>('#join-btn')!;

    this.joinBtn.addEventListener('click', () => this.confirm());
    this.nameInput.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); this.confirm(); }
    });

    document.body.appendChild(this.overlay);
  }

  // ---- Public API ----

  show(): void {
    this.overlay.style.display = 'flex';
    setTimeout(() => this.nameInput.focus(), 50);
  }

  hide(): void {
    this.overlay.style.display = 'none';
  }

  onJoin(cb: (name: string, shirtColor: ShirtColor, skinColor: SkinColor) => void): void {
    this.joinCallback = cb;
  }

  /** Called once init fires — sets the server-assigned default name if the field is empty. */
  setDefaultName(name: string): void {
    if (!this.nameInput.value) {
      this.nameInput.value = name;
    }
    this.sendAppearance();
  }

  // ---- Internal ----

  private confirm(): void {
    this.sendAppearance();
    const name  = this.currentName;
    const shirt = this._shirtColor;
    const skin  = this._skinColor;
    this.hide();
    this.joinCallback?.(name, shirt, skin);
  }

  private sendAppearance(): void {
    const name = this.currentName;
    // Keep the chat input line in sync with what the player is typing — no server roundtrip needed
    ChatLog.setPlayerName(name);
    this.dispatch({
      type:       'SET_APPEARANCE',
      playerName: name,
      shirtColor: this._shirtColor,
      skinColor:  this._skinColor,
    });
  }

  private selectShirt(id: ShirtColor): void {
    this._shirtColor = id;
    this.refreshSwatches('shirt', id);
    this.sendAppearance();
  }

  private selectSkin(id: SkinColor): void {
    this._skinColor = id;
    this.refreshSwatches('skin', id);
    this.sendAppearance();
  }

  private refreshSwatches(group: 'shirt' | 'skin', selected: string): void {
    const swatches = this.overlay.querySelectorAll<HTMLElement>(`.swatch-${group}`);
    swatches.forEach((sw) => {
      const isActive = sw.dataset.id === selected;
      sw.style.outline  = isActive ? '2px solid #ff981f' : '2px solid transparent';
      sw.style.transform = isActive ? 'scale(1.15)' : 'scale(1)';
    });
  }

  // ---- DOM construction ----

  private buildOverlay(): HTMLElement {
    const overlay = document.createElement('div');
    overlay.style.cssText = `
      position: fixed; inset: 0; z-index: 8000;
      background: rgba(0,0,0,0.72);
      display: flex; align-items: center; justify-content: center;
    `;

    const dialog = document.createElement('div');
    dialog.style.cssText = `
      background: #1a0d00;
      border: 1px solid #8b6c3e;
      border-radius: 4px;
      padding: 20px 24px 18px;
      width: 280px;
      font-family: 'Segoe UI', system-ui, sans-serif;
      color: #c8a060;
      box-shadow: 0 6px 32px rgba(0,0,0,0.9);
      user-select: none;
    `;

    // Title
    const title = document.createElement('div');
    title.style.cssText = `
      color: #ff981f; font-size: 14px; font-weight: 700;
      text-transform: uppercase; letter-spacing: 1.5px;
      margin-bottom: 16px; text-align: center;
    `;
    title.textContent = 'Join Server';
    dialog.appendChild(title);

    // Name field
    dialog.appendChild(this.buildLabel('Player Name'));

    const nameInput = document.createElement('input');
    nameInput.id = 'join-name-input';
    nameInput.type = 'text';
    nameInput.maxLength = 20;
    nameInput.placeholder = 'Player';
    nameInput.style.cssText = `
      width: 100%; box-sizing: border-box;
      background: #0d0600; color: #ffffff;
      border: 1px solid rgba(61,32,16,0.70);
      border-radius: 2px; padding: 5px 8px;
      font-size: 12px; font-family: inherit;
      outline: none; margin-bottom: 14px;
    `;
    nameInput.addEventListener('input', () => this.sendAppearance());
    nameInput.addEventListener('focus', () => { nameInput.style.borderColor = '#ff981f'; });
    nameInput.addEventListener('blur',  () => { nameInput.style.borderColor = 'rgba(61,32,16,0.70)'; });
    dialog.appendChild(nameInput);

    // Shirt colour
    dialog.appendChild(this.buildLabel('Shirt Color'));
    dialog.appendChild(this.buildSwatchRow('shirt', SHIRT_SWATCHES, 'blue', (id) => this.selectShirt(id as ShirtColor)));

    // Skin tone
    dialog.appendChild(this.buildLabel('Skin Tone'));
    dialog.appendChild(this.buildSwatchRow('skin', SKIN_SWATCHES, 'fair', (id) => this.selectSkin(id as SkinColor)));

    // Join button
    const joinBtn = document.createElement('button');
    joinBtn.id = 'join-btn';
    joinBtn.textContent = 'Join';
    joinBtn.style.cssText = `
      margin-top: 16px; width: 100%;
      background: #3d2010; color: #ff981f;
      border: 1px solid #8b6c3e; border-radius: 3px;
      padding: 7px 0; font-size: 13px; font-weight: 700;
      font-family: inherit; cursor: pointer;
      text-transform: uppercase; letter-spacing: 1px;
      transition: background 0.1s;
    `;
    joinBtn.addEventListener('mouseenter', () => { joinBtn.style.background = '#5a3020'; });
    joinBtn.addEventListener('mouseleave', () => { joinBtn.style.background = '#3d2010'; });
    dialog.appendChild(joinBtn);

    overlay.appendChild(dialog);
    return overlay;
  }

  private buildLabel(text: string): HTMLElement {
    const el = document.createElement('div');
    el.style.cssText = `
      font-size: 10px; color: #c8a060; font-weight: 700;
      text-transform: uppercase; letter-spacing: 1px;
      margin-bottom: 6px;
    `;
    el.textContent = text;
    return el;
  }

  private buildSwatchRow(
    group: string,
    swatches: { id: string; hex: string; label: string }[],
    defaultId: string,
    onClick: (id: string) => void,
  ): HTMLElement {
    const row = document.createElement('div');
    row.style.cssText = 'display: flex; gap: 8px; margin-bottom: 14px;';

    for (const swatch of swatches) {
      const el = document.createElement('div');
      el.className = `swatch-${group}`;
      el.dataset.id = swatch.id;
      const isActive = swatch.id === defaultId;
      el.style.cssText = `
        width: 36px; height: 36px;
        background: ${swatch.hex};
        border-radius: 3px;
        cursor: pointer;
        outline: ${isActive ? '2px solid #ff981f' : '2px solid transparent'};
        outline-offset: 2px;
        transform: ${isActive ? 'scale(1.15)' : 'scale(1)'};
        transition: outline 0.08s, transform 0.08s;
        flex-shrink: 0;
      `;
      el.title = swatch.label;
      el.addEventListener('click', () => onClick(swatch.id));
      row.appendChild(el);
    }

    return row;
  }
}
