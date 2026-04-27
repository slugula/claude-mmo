import type { GameState, GameAction } from '../shared/types';
import { InventoryUI } from './InventoryUI';
import { SkillsUI } from './SkillsUI';
import { EquipmentUI } from './EquipmentUI';
import { ChatLog } from './ChatLog';
import type { ContextInfo } from './ContextInfo';
import { getCombatLevel, getTotalLevel } from '../systems/SkillSystem';

type TabId = 'inventory' | 'skills' | 'equipment';

export class GameUI {
  private tabBar: HTMLElement;
  private panelContent: HTMLElement;
  private statusTick: HTMLElement;
  private statusPos: HTMLElement;
  private activeTab: TabId = 'inventory';
  private tabElements = new Map<TabId, HTMLElement>();

  private inventoryUI: InventoryUI;
  private skillsUI: SkillsUI;
  private equipmentUI: EquipmentUI;

  constructor(dispatch: (action: GameAction) => void) {
    this.tabBar       = document.getElementById('tab-bar')!;
    this.panelContent = document.getElementById('panel-content')!;
    this.statusTick   = document.getElementById('status-tick')!;
    this.statusPos    = document.getElementById('status-pos')!;

    this.inventoryUI = new InventoryUI(dispatch);
    this.skillsUI    = new SkillsUI();
    this.equipmentUI = new EquipmentUI(dispatch);

    ChatLog.init(dispatch);

    this.buildTabs();
    this.showTab('inventory');
  }

  setContextInfo(ci: ContextInfo): void {
    this.inventoryUI.setContextInfo(ci);
    this.equipmentUI.setContextInfo(ci);
  }

  dismissInventoryMenu(): void {
    this.inventoryUI.dismissSlotMenu();
  }

  private buildTabs(): void {
    const tabs: { id: TabId; label: string; el: HTMLElement }[] = [
      { id: 'inventory', label: 'Inv',   el: this.inventoryUI.element },
      { id: 'skills',    label: 'Skills',el: this.skillsUI.element    },
      { id: 'equipment', label: 'Equip', el: this.equipmentUI.element },
    ];

    for (const tab of tabs) {
      this.tabElements.set(tab.id, tab.el);
      const btn = document.createElement('button');
      btn.className = 'tab-button';
      btn.dataset.tab = tab.id;
      btn.textContent = tab.label;
      btn.addEventListener('click', () => this.showTab(tab.id));
      this.tabBar.appendChild(btn);
    }

    this.equalizePanelHeight();
  }

  // Renders each tab temporarily to measure its natural height, then locks
  // #panel-content to the tallest one. Re-run whenever tabs are added.
  private equalizePanelHeight(): void {
    this.panelContent.style.height   = 'auto';
    this.panelContent.style.overflow = 'visible';

    let maxH = 0;
    for (const el of this.tabElements.values()) {
      this.panelContent.innerHTML = '';
      this.panelContent.appendChild(el);
      maxH = Math.max(maxH, this.panelContent.offsetHeight);
    }

    this.panelContent.style.overflow = '';
    this.panelContent.style.height   = `${maxH}px`;
    this.panelContent.innerHTML = '';
  }

  private showTab(id: TabId): void {
    this.activeTab = id;
    this.panelContent.innerHTML = '';
    const el = this.tabElements.get(id);
    if (el) this.panelContent.appendChild(el);

    this.tabBar.querySelectorAll('.tab-button').forEach((btn) => {
      btn.classList.toggle('active', (btn as HTMLElement).dataset.tab === id);
    });
  }

  update(state: GameState, localPlayerId: string): void {
    const player = state.players[localPlayerId];
    if (!player) return;

    this.inventoryUI.update(player.inventory);
    this.skillsUI.update(player.skills);
    this.equipmentUI.update(player);

    ChatLog.setPlayerName(player.playerName);

    this.statusTick.textContent = `Tick: ${state.tick}`;
    this.statusPos.textContent  = `(${player.tileX}, ${player.tileY})  Cb:${getCombatLevel(player.skills)}  TL:${getTotalLevel(player.skills)}`;
  }
}
