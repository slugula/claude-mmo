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
    const tabs: { id: TabId; label: string }[] = [
      { id: 'inventory',  label: 'Inv'   },
      { id: 'skills',     label: 'Skills'},
      { id: 'equipment',  label: 'Equip' },
    ];

    for (const tab of tabs) {
      const btn = document.createElement('button');
      btn.className = 'tab-button';
      btn.dataset.tab = tab.id;
      btn.textContent = tab.label;
      btn.addEventListener('click', () => this.showTab(tab.id));
      this.tabBar.appendChild(btn);
    }
  }

  private showTab(id: TabId): void {
    this.activeTab = id;
    this.panelContent.innerHTML = '';

    if (id === 'inventory')  this.panelContent.appendChild(this.inventoryUI.element);
    if (id === 'equipment')  this.panelContent.appendChild(this.equipmentUI.element);
    if (id === 'skills')     this.panelContent.appendChild(this.skillsUI.element);

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
