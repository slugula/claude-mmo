import type { ItemStack, GameAction } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { drawItemSprite } from '../items/ItemSprites';
import { INVENTORY_SLOTS } from '../shared/constants';
import { ChatLog } from './ChatLog';
import { showItemTooltip, hideItemTooltip } from './ItemTooltip';
import type { ContextInfo } from './ContextInfo';

const SPRITE_SIZE = 32;

export class InventoryUI {
  private container: HTMLElement;
  private slots: HTMLElement[] = [];
  private slotMenuEl: HTMLElement | null = null;
  private _dismissHandler: (() => void) | null = null;

  private dragSlot: number = -1;
  private ghostEl: HTMLElement | null = null;
  private currentInventory: (ItemStack | null)[] = [];

  private readonly dispatch: (action: GameAction) => void;
  private contextInfo: ContextInfo | null = null;

  setContextInfo(ci: ContextInfo): void { this.contextInfo = ci; }

  /** Called when a click happens outside the UI (e.g. on the game canvas). */
  dismissSlotMenu(): void { this.hideSlotMenuNow(); }

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
    title.textContent = 'Inventory';
    this.container.appendChild(title);

    const grid = document.createElement('div');
    grid.style.cssText = `
      display: grid;
      grid-template-columns: repeat(4, 1fr);
      gap: 3px;
    `;

    for (let i = 0; i < INVENTORY_SLOTS; i++) {
      const slot = document.createElement('div');
      slot.style.cssText = `
        width: 100%; aspect-ratio: 1;
        background: transparent;
        border: 1px solid rgba(61, 32, 16, 0.55);
        border-radius: 2px;
        display: flex; align-items: center; justify-content: center;
        cursor: default; position: relative;
        font-size: 9px; user-select: none;
        transition: border-color 0.1s;
      `;

      slot.addEventListener('mouseenter', () => {
        if (slot.dataset.itemId) slot.style.borderColor = '#ff981f';
        const stack = this.currentInventory[i];
        if (stack) {
          const def = getItem(stack.itemId);
          const name = def?.name ?? 'Item';
          const verb = def?.equipSlot
            ? (def.equipSlot === 'rightHand' ? 'Wield' : 'Wear')
            : 'Examine';
          this.contextInfo?.setOverride(verb, name);
          showItemTooltip(name, slot);
        }
      });
      slot.addEventListener('mouseleave', () => {
        if (this.dragSlot !== i) slot.style.borderColor = 'rgba(61, 32, 16, 0.55)';
        this.contextInfo?.clearOverride();
        hideItemTooltip();
      });

      slot.addEventListener('mousedown', (e) => {
        if (e.button !== 0) return;
        const stack = this.currentInventory[i];
        if (!stack) return;
        e.preventDefault();
        this.startDrag(i, e.clientX, e.clientY, stack);
      });

      slot.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        const stack = this.currentInventory[i];
        if (stack) this.showSlotMenu(i, e.clientX, e.clientY, stack);
      });

      this.slots.push(slot);
      grid.appendChild(slot);
    }

    this.container.appendChild(grid);

    document.addEventListener('mousemove', (e) => this.onMouseMove(e));
    document.addEventListener('mouseup',   (e) => this.onMouseUp(e));
  }

  get element(): HTMLElement { return this.container; }

  update(inventory: (ItemStack | null)[]): void {
    this.currentInventory = inventory;
    for (let i = 0; i < INVENTORY_SLOTS; i++) {
      this.renderSlot(i, inventory[i] ?? null);
    }
  }

  private renderSlot(i: number, stack: ItemStack | null): void {
    const slot = this.slots[i];
    slot.innerHTML = '';
    slot.dataset.itemId = '';

    if (!stack) {
      slot.style.borderColor = '#3d2010';
      slot.style.opacity = '1';
      return;
    }

    const def = getItem(stack.itemId);
    slot.dataset.itemId = stack.itemId;

    const canvas = document.createElement('canvas');
    canvas.width  = SPRITE_SIZE;
    canvas.height = SPRITE_SIZE;
    canvas.style.cssText = 'width: 70%; height: 70%; image-rendering: pixelated;';

    const ctx = canvas.getContext('2d')!;
    drawItemSprite(ctx, SPRITE_SIZE, SPRITE_SIZE, stack.itemId);
    slot.appendChild(canvas);

    if (stack.quantity > 1) {
      const qty = document.createElement('span');
      qty.style.cssText = `
        position: absolute; bottom: 1px; right: 2px;
        color: #ffdd44; font-size: 8px; font-weight: 700;
        text-shadow: 1px 1px 0 #000;
      `;
      qty.textContent = stack.quantity >= 1000
        ? `${Math.floor(stack.quantity / 1000)}k`
        : String(stack.quantity);
      slot.appendChild(qty);
    }
  }

  // ---- Drag & Drop ----

  private startDrag(slotIndex: number, clientX: number, clientY: number, stack: ItemStack): void {
    this.dragSlot = slotIndex;
    this.slots[slotIndex].style.opacity = '0.4';
    hideItemTooltip();

    const ghost = document.createElement('canvas');
    ghost.width  = SPRITE_SIZE;
    ghost.height = SPRITE_SIZE;
    ghost.style.cssText = `
      position: fixed; pointer-events: none; z-index: 9999;
      width: 36px; height: 36px; image-rendering: pixelated;
      transform: translate(-50%, -50%);
    `;
    const ctx = ghost.getContext('2d')!;
    drawItemSprite(ctx, SPRITE_SIZE, SPRITE_SIZE, stack.itemId);
    document.body.appendChild(ghost);
    this.ghostEl = ghost;
    this.moveGhost(clientX, clientY);
  }

  private onMouseMove(e: MouseEvent): void {
    if (this.dragSlot === -1 || !this.ghostEl) return;
    this.moveGhost(e.clientX, e.clientY);
  }

  private onMouseUp(e: MouseEvent): void {
    if (this.dragSlot === -1) return;

    const target = this.slotAt(e.clientX, e.clientY);
    if (target !== -1 && target !== this.dragSlot) {
      this.dispatch({ type: 'MOVE_SLOT', fromSlot: this.dragSlot, toSlot: target });
    } else if (target === this.dragSlot) {
      const stack = this.currentInventory[this.dragSlot];
      if (stack) {
        const def = getItem(stack.itemId);
        if (def?.equipSlot) {
          this.dispatch({ type: 'EQUIP_ITEM', slotIndex: this.dragSlot });
        } else {
          const name = def?.name ?? 'Item';
          ChatLog.log(def ? `${name}: value ${def.value}gp.` : `It\u2019s ${name.toLowerCase()}.`);
        }
      }
    }

    this.slots[this.dragSlot].style.opacity = '1';
    this.ghostEl?.remove();
    this.ghostEl = null;
    this.dragSlot = -1;
  }

  private moveGhost(x: number, y: number): void {
    if (!this.ghostEl) return;
    this.ghostEl.style.left = `${x}px`;
    this.ghostEl.style.top  = `${y}px`;
  }

  private slotAt(x: number, y: number): number {
    for (let i = 0; i < this.slots.length; i++) {
      const r = this.slots[i].getBoundingClientRect();
      if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
    }
    return -1;
  }

  // ---- Right-click slot menu ----

  private showSlotMenu(slotIndex: number, x: number, y: number, stack: ItemStack): void {
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
      font-size: 12px; user-select: none;
    `;

    // Header
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
      item.addEventListener('mousedown', (e) => { e.stopPropagation(); onClick(); });

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

    if (def?.equipSlot) {
      const equipVerb = def.equipSlot === 'rightHand' ? 'Wield' : 'Wear';
      addOption(equipVerb, name, false, () => {
        this.dispatch({ type: 'EQUIP_ITEM', slotIndex });
        this.hideSlotMenuNow();
      });
    }
    addOption('Drop', name, false, () => {
      this.dispatch({ type: 'DROP_ITEM', slotIndex });
      this.hideSlotMenuNow();
    });
    addOption('Examine', name, false, () => {
      ChatLog.log(def ? `${def.name} — value: ${def.value}gp` : name);
      this.hideSlotMenuNow();
    });
    addOption('Cancel', '', true, () => this.hideSlotMenuNow());

    document.body.appendChild(menu);

    const w = menu.offsetWidth;
    const h = menu.offsetHeight;
    menu.style.left = `${Math.min(x, window.innerWidth - w - 4)}px`;
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
