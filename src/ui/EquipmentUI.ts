import type { EquipSlot, ItemStack, GameAction, PlayerState } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { drawItemSprite } from '../items/ItemSprites';
import { ChatLog } from './ChatLog';
import { showItemTooltip, hideItemTooltip } from './ItemTooltip';
import type { ContextInfo } from './ContextInfo';

const GRID: (EquipSlot | null)[][] = [
  [null,       'head',      null       ],
  [null,       'neck',      'ammo'     ],
  ['rightHand', 'body',     'leftHand' ],
  [null,       'legs',      null       ],
  ['hands',    'feet',      'ring'     ],
];

const SLOT_LABEL: Record<EquipSlot, string> = {
  head:      'Head',
  neck:      'Neck',
  body:      'Body',
  legs:      'Legs',
  feet:      'Feet',
  hands:     'Hands',
  ring:      'Ring',
  leftHand:  'Off-hand',
  rightHand: 'Main Hand',
  ammo:      'Ammo',
};

const SPRITE_SIZE = 32;

export class EquipmentUI {
  private readonly container: HTMLElement;
  private readonly slotEls: Map<EquipSlot, HTMLElement> = new Map();
  private readonly bonusesEl: HTMLElement;
  private readonly dispatch: (action: GameAction) => void;
  private contextInfo: ContextInfo | null = null;
  private equipped: Partial<Record<EquipSlot, ItemStack>> = {};
  private slotMenuEl: HTMLElement | null = null;
  private _dismissHandler: (() => void) | null = null;

  constructor(dispatch: (action: GameAction) => void) {
    this.dispatch = dispatch;

    this.container = document.createElement('div');
    this.container.style.cssText = 'padding: 8px;';

    const title = document.createElement('div');
    title.style.cssText = `
      color: #ff981f; font-size: 11px; font-weight: 700;
      text-transform: uppercase; letter-spacing: 1px;
      padding: 4px 0 6px; border-bottom: 1px solid #3d2010; margin-bottom: 6px;
    `;
    title.textContent = 'Equipment';
    this.container.appendChild(title);

    const grid = document.createElement('div');
    grid.style.cssText = `
      display: grid;
      grid-template-columns: repeat(3, calc((100% - 9px) / 4));
      justify-content: center;
      gap: 3px;
      margin-bottom: 8px;
    `;

    for (const row of GRID) {
      for (const slot of row) {
        if (slot === null) {
          const spacer = document.createElement('div');
          spacer.style.cssText = 'aspect-ratio: 1; background: transparent;';
          grid.appendChild(spacer);
          continue;
        }

        const el = document.createElement('div');
        el.style.cssText = `
          aspect-ratio: 1 / 1;
          background: rgba(10, 4, 0, 0.45);
          border: 1px solid rgba(61, 32, 16, 0.70);
          border-radius: 2px;
          display: flex; align-items: center; justify-content: center;
          cursor: default; position: relative;
          user-select: none;
          transition: border-color 0.1s;
        `;

        el.addEventListener('mouseenter', () => {
          const stack = this.equipped[slot];
          if (stack) {
            el.style.borderColor = '#ff981f';
            const name = getItem(stack.itemId)?.name ?? 'Item';
            this.contextInfo?.setOverride('Remove', name);
            showItemTooltip(name, el);
          } else {
            el.style.borderColor = '#5a3020';
            this.contextInfo?.setOverride(SLOT_LABEL[slot], '');
          }
        });

        el.addEventListener('mouseleave', () => {
          el.style.borderColor = '#3d2010';
          this.contextInfo?.clearOverride();
          hideItemTooltip();
        });

        el.addEventListener('click', () => {
          if (this.equipped[slot]) {
            this.dispatch({ type: 'UNEQUIP_ITEM', slot });
          }
        });

        el.addEventListener('contextmenu', (e) => {
          e.preventDefault();
          const stack = this.equipped[slot];
          if (stack) this.showSlotMenu(slot, e.clientX, e.clientY, stack);
        });

        this.slotEls.set(slot, el);
        grid.appendChild(el);
      }
    }

    this.container.appendChild(grid);

    this.bonusesEl = document.createElement('div');
    this.bonusesEl.style.cssText = `
      border-top: 1px solid rgba(61, 32, 16, 0.60);
      padding-top: 6px;
      font-size: 10px;
      color: #c8a060;
      line-height: 1.7;
    `;
    this.container.appendChild(this.bonusesEl);
  }

  setContextInfo(ci: ContextInfo): void { this.contextInfo = ci; }

  get element(): HTMLElement { return this.container; }

  update(state: PlayerState): void {
    this.equipped = state.equipped;

    for (const [slot, el] of this.slotEls) {
      el.innerHTML = '';
      const stack = state.equipped[slot];

      if (stack) {
        const canvas = document.createElement('canvas');
        canvas.width  = SPRITE_SIZE;
        canvas.height = SPRITE_SIZE;
        canvas.style.cssText = 'width: 70%; height: 70%; image-rendering: pixelated; pointer-events: none;';
        const ctx = canvas.getContext('2d')!;
        drawItemSprite(ctx, SPRITE_SIZE, SPRITE_SIZE, stack.itemId);
        el.appendChild(canvas);
      } else {
        const label = document.createElement('span');
        label.textContent = SLOT_LABEL[slot][0];
        label.style.cssText = 'color: #3d2010; font-size: 7px; pointer-events: none; font-weight: 700;';
        el.appendChild(label);
      }
    }

    let atk = 0, def = 0, str = 0;
    for (const stack of Object.values(state.equipped)) {
      if (!stack) continue;
      const s = getItem(stack.itemId)?.stats;
      if (!s) continue;
      atk += s.attackBonus   ?? 0;
      def += s.defenseBonus  ?? 0;
      str += s.strengthBonus ?? 0;
    }

    this.bonusesEl.innerHTML = `
      <div style="color:#ff981f;font-size:9px;font-weight:700;margin-bottom:2px;text-transform:uppercase;letter-spacing:1px;">Combat Bonuses</div>
      <div>Attack bonus: <span style="color:#ffffff">+${atk}</span></div>
      <div>Defence bonus: <span style="color:#ffffff">+${def}</span></div>
      <div>Strength bonus: <span style="color:#ffffff">+${str}</span></div>
    `;
  }

  private showSlotMenu(slot: EquipSlot, x: number, y: number, stack: ItemStack): void {
    this.hideSlotMenuNow();

    const def = getItem(stack.itemId);
    const name = def?.name ?? 'Item';

    const menu = document.createElement('div');
    menu.style.cssText = `
      position: fixed; z-index: 2000;
      background: #1a0d00; border: 1px solid #8b6c3e;
      border-radius: 3px; padding: 2px;
      min-width: 140px;
      box-shadow: 2px 2px 12px rgba(0,0,0,0.85);
      font-family: 'Segoe UI', system-ui, sans-serif;
      font-size: 12px; user-select: none;
    `;

    const header = document.createElement('div');
    header.style.cssText = `
      color: white; font-weight: 700; font-size: 11px;
      padding: 5px 10px 4px; border-bottom: 1px solid #3d2010; margin-bottom: 2px;
    `;
    header.textContent = 'Choose Option';
    menu.appendChild(header);

    const addOption = (verb: string, subject: string, isCancel: boolean, onClick: () => void) => {
      const item = document.createElement('div');
      item.style.cssText = `padding: 3px 10px; cursor: pointer; border-radius: 2px; line-height: 1.7; white-space: nowrap;`;
      item.addEventListener('mouseenter', () => { item.style.background = '#2d1b0e'; });
      item.addEventListener('mouseleave', () => { item.style.background = ''; });
      item.addEventListener('mousedown', (e) => { e.stopPropagation(); onClick(); this.hideSlotMenuNow(); });

      if (isCancel) {
        const s = document.createElement('span');
        s.style.color = '#ff4444';
        s.textContent = 'Cancel';
        item.appendChild(s);
      } else {
        const vs = document.createElement('span');
        vs.style.color = '#ffffff';
        vs.textContent = verb;
        item.appendChild(vs);
        if (subject) {
          const ss = document.createElement('span');
          ss.style.color = '#ff981f';
          ss.textContent = ` ${subject}`;
          item.appendChild(ss);
        }
      }
      menu.appendChild(item);
    };

    addOption('Remove', name, false, () => {
      this.dispatch({ type: 'UNEQUIP_ITEM', slot });
    });
    addOption('Examine', name, false, () => {
      ChatLog.log(def ? `${name} — value: ${def.value}gp` : name);
    });
    addOption('Cancel', '', true, () => {});

    document.body.appendChild(menu);

    const w = menu.offsetWidth;
    const h = menu.offsetHeight;
    menu.style.left = `${Math.min(x, window.innerWidth  - w - 4)}px`;
    menu.style.top  = `${Math.min(y, window.innerHeight - h - 4)}px`;

    this.slotMenuEl = menu;
    this._dismissHandler = () => this.hideSlotMenuNow();
    setTimeout(() => document.addEventListener('mousedown', this._dismissHandler!), 0);
  }

  private hideSlotMenuNow(): void {
    this.slotMenuEl?.remove();
    this.slotMenuEl = null;
    if (this._dismissHandler) {
      document.removeEventListener('mousedown', this._dismissHandler);
      this._dismissHandler = null;
    }
  }
}
