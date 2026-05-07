import type { ItemStack, GameAction } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { drawItemSprite } from '../items/ItemSprites';
import { BANK_SLOTS } from '../shared/constants';
import { ChatLog } from './ChatLog';
import { showItemTooltip, hideItemTooltip } from './ItemTooltip';

const SPRITE_SIZE = 32;
const COLS = 8;

export class BankUI {
  private overlay: HTMLElement;
  private slotsContainer!: HTMLElement;
  private slotEls: HTMLElement[] = [];
  private currentBank: (ItemStack | null)[] = [];
  private visible = false;

  private slotMenuEl: HTMLElement | null = null;
  private _dismissHandler: (() => void) | null = null;

  private readonly dispatch: (action: GameAction) => void;
  private _onClose: (() => void) | null = null;
  private usageLabel!: HTMLElement;

  constructor(dispatch: (action: GameAction) => void) {
    this.dispatch = dispatch;
    this.overlay  = this.buildOverlay();
    document.body.appendChild(this.overlay);
  }

  onClose(cb: () => void): void { this._onClose = cb; }

  open(bank: (ItemStack | null)[]): void {
    this.currentBank = bank;
    this.renderAll();
    this.overlay.style.display = 'flex';
    this.visible = true;
  }

  update(bank: (ItemStack | null)[]): void {
    if (!this.visible) return;
    this.currentBank = bank;
    this.renderAll();
  }

  close(): void {
    this.hideMenuNow();
    this.overlay.style.display = 'none';
    this.visible = false;
    this._onClose?.();
  }

  get isOpen(): boolean { return this.visible; }

  // ---- Build DOM ----

  // Grid area is fixed at 460×464 px (not including title bar or bottom buttons).
  // Slots are 48×48 px (matching inventory). 8 cols × 48 px + 7 gaps × 3 px = 405 px grid,
  // centred inside the 460 px container. 100 slots → 13 rows → 660 px total → scrollable.
  // Panel is horizontally centred and sits above the 210 px chat log (bottom: 234 px).

  private static readonly SLOT_PX  = 48;
  private static readonly SLOT_GAP = 3;

  private buildOverlay(): HTMLElement {
    const panel = document.createElement('div');
    panel.style.cssText = `
      display: none; flex-direction: column;
      position: fixed;
      left: 50%; transform: translateX(-50%);
      bottom: 234px;
      z-index: 1500;
      width: 460px;
      background: #1a0d00; border: 2px solid #8b6c3e;
      border-radius: 4px; padding: 0;
      box-shadow: 3px 3px 16px rgba(0,0,0,0.9);
      font-family: inherit; user-select: none;
    `;

    // ---- Title bar — three columns: [usage | title | close] ----
    const titleBar = document.createElement('div');
    titleBar.style.cssText = `
      display: grid; grid-template-columns: 1fr auto 1fr;
      align-items: center;
      padding: 5px 8px 4px;
      border-bottom: 1px solid #3d2010;
      flex-shrink: 0;
    `;

    // Left: items / capacity fraction (orange)
    const usage = document.createElement('span');
    usage.style.cssText = 'color: #ff981f; font-size: 10px;';
    usage.textContent = `0 / ${BANK_SLOTS}`;
    this.usageLabel = usage;

    // Centre: "Bank Chest" label
    const title = document.createElement('span');
    title.style.cssText = 'color: #ffcc44; font-weight: 700; font-size: 12px; text-align: center;';
    title.textContent = 'Bank Chest';

    // Right: close button (white ✕)
    const closeWrap = document.createElement('div');
    closeWrap.style.cssText = 'display: flex; justify-content: flex-end;';
    const closeBtn = document.createElement('button');
    closeBtn.style.cssText = `
      background: none; border: 1px solid #8b6c3e; color: #ffffff;
      border-radius: 2px; cursor: pointer; font-size: 11px; padding: 1px 5px; line-height: 1;
    `;
    closeBtn.textContent = '✕';
    closeBtn.addEventListener('click', () => this.close());
    closeWrap.appendChild(closeBtn);

    titleBar.appendChild(usage);
    titleBar.appendChild(title);
    titleBar.appendChild(closeWrap);
    panel.appendChild(titleBar);

    // ---- Slot grid — fixed 460×464 viewport, scrollable ----
    const gridWrap = document.createElement('div');
    gridWrap.style.cssText = `
      width: 460px; height: 464px;
      overflow-y: scroll; overflow-x: hidden;
      flex-shrink: 0;
      box-sizing: border-box;
      display: flex; justify-content: center;
      padding: 6px 0;
    `;

    const grid = document.createElement('div');
    grid.style.cssText = `
      display: grid;
      grid-template-columns: repeat(${COLS}, ${BankUI.SLOT_PX}px);
      gap: ${BankUI.SLOT_GAP}px;
      align-content: start;
    `;

    for (let i = 0; i < BANK_SLOTS; i++) {
      const slot = this.buildSlot(i);
      this.slotEls.push(slot);
      grid.appendChild(slot);
    }

    this.slotsContainer = grid;
    gridWrap.appendChild(grid);
    panel.appendChild(gridWrap);

    // ---- Bottom buttons ----
    const btnRow = document.createElement('div');
    btnRow.style.cssText = `
      display: flex; gap: 4px; padding: 5px 6px 6px;
      border-top: 1px solid #3d2010;
      flex-shrink: 0;
    `;

    const mkBtn = (label: string, onClick: () => void) => {
      const b = document.createElement('button');
      b.style.cssText = `
        flex: 1; background: #2d1b0e; border: 1px solid #8b6c3e;
        color: #ffcc44; font-size: 10px; font-weight: 700;
        padding: 4px 2px; border-radius: 3px; cursor: pointer;
      `;
      b.textContent = label;
      b.addEventListener('mouseenter', () => { b.style.background = '#3d2010'; });
      b.addEventListener('mouseleave', () => { b.style.background = '#2d1b0e'; });
      b.addEventListener('click', onClick);
      return b;
    };

    btnRow.appendChild(mkBtn('Deposit Inventory', () => this.dispatch({ type: 'DEPOSIT_ALL' })));
    btnRow.appendChild(mkBtn('Deposit Worn Items', () => this.dispatch({ type: 'DEPOSIT_WORN' })));
    panel.appendChild(btnRow);

    return panel;
  }

  private buildSlot(i: number): HTMLElement {
    const S = BankUI.SLOT_PX;
    const slot = document.createElement('div');
    slot.style.cssText = `
      width: ${S}px; height: ${S}px;
      background: transparent;
      border: 1px solid rgba(61, 32, 16, 0.55);
      border-radius: 2px;
      display: flex; align-items: center; justify-content: center;
      cursor: default; position: relative;
      font-size: 9px;
      transition: border-color 0.1s;
      flex-shrink: 0;
    `;

    slot.addEventListener('mouseenter', () => {
      const stack = this.currentBank[i];
      if (!stack) return;
      slot.style.borderColor = '#ff981f';
      const def = getItem(stack.itemId);
      const name = def?.name ?? 'Item';
      showItemTooltip([
        [{ text: 'Withdraw' }, { text: ` ${name}`, color: '#ff981f' }],
      ]);
    });

    slot.addEventListener('mouseleave', () => {
      slot.style.borderColor = 'rgba(61, 32, 16, 0.55)';
      hideItemTooltip();
    });

    slot.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      const stack = this.currentBank[i];
      if (!stack) return;
      e.preventDefault();
      this.dispatch({ type: 'WITHDRAW_ITEM', bankSlot: i, quantity: 1 });
    });

    slot.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      const stack = this.currentBank[i];
      if (!stack) return;
      this.showWithdrawMenu(i, e.clientX, e.clientY, stack);
    });

    return slot;
  }

  // ---- Rendering ----

  private renderAll(): void {
    const used = this.currentBank.filter(s => s !== null).length;
    this.usageLabel.textContent = `${used} / ${BANK_SLOTS}`;
    for (let i = 0; i < BANK_SLOTS; i++) {
      this.renderSlot(i, this.currentBank[i] ?? null);
    }
  }

  private renderSlot(i: number, stack: ItemStack | null): void {
    const slot = this.slotEls[i];
    slot.innerHTML = '';
    slot.dataset.itemId = '';

    if (!stack) {
      slot.style.borderColor = 'rgba(61, 32, 16, 0.55)';
      return;
    }

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
      qty.textContent = stack.quantity >= 1_000_000
        ? `${(stack.quantity / 1_000_000).toFixed(1)}M`
        : stack.quantity >= 1000
          ? `${Math.floor(stack.quantity / 1000)}k`
          : String(stack.quantity);
      slot.appendChild(qty);
    }
  }

  // ---- Right-click withdraw menu ----

  private showWithdrawMenu(bankSlot: number, x: number, y: number, stack: ItemStack): void {
    this.hideMenuNow();

    const def  = getItem(stack.itemId);
    const name = def?.name ?? 'Item';

    const menu = document.createElement('div');
    menu.style.cssText = `
      position: fixed; z-index: 2100;
      background: #1a0d00; border: 1px solid #8b6c3e;
      border-radius: 3px; padding: 2px;
      min-width: 140px;
      box-shadow: 2px 2px 12px rgba(0,0,0,0.85);
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

    const withdraw = (qty: number) => {
      this.dispatch({ type: 'WITHDRAW_ITEM', bankSlot, quantity: qty });
      this.hideMenuNow();
    };

    addOption('Withdraw-1',   name, false, () => withdraw(1));
    addOption('Withdraw-5',   name, false, () => withdraw(5));
    addOption('Withdraw-10',  name, false, () => withdraw(10));
    addOption('Withdraw-All', name, false, () => withdraw(stack.quantity));
    addOption('Examine',      name, false, () => {
      ChatLog.log(def ? `${def.name} — value: ${def.value}gp` : name);
      this.hideMenuNow();
    });
    addOption('Cancel', '', true, () => this.hideMenuNow());

    document.body.appendChild(menu);

    const w = menu.offsetWidth;
    const h = menu.offsetHeight;
    menu.style.left = `${Math.min(x, window.innerWidth  - w - 4)}px`;
    menu.style.top  = `${Math.min(y, window.innerHeight - h - 4)}px`;

    this.slotMenuEl = menu;
    this._dismissHandler = () => this.hideMenuNow();
    setTimeout(() => document.addEventListener('mousedown', this._dismissHandler!), 0);
  }

  private hideMenuNow(): void {
    this.slotMenuEl?.remove();
    this.slotMenuEl = null;
    if (this._dismissHandler) {
      document.removeEventListener('mousedown', this._dismissHandler);
      this._dismissHandler = null;
    }
  }
}
