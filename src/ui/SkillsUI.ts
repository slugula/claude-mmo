import { VISIBLE_SKILLS } from '../shared/types';
import type { SkillsState, SkillId } from '../shared/types';
import { progressToNextLevel } from '../systems/SkillSystem';

const SKILL_COLORS: Record<SkillId, string> = {
  attack:       '#e05050',
  hitpoints:    '#e06060',
  mining:       '#8888cc',
  strength:     '#e07040',
  agility:      '#80c0d0',
  smithing:     '#c0a840',
  defence:      '#60a0e0',
  herblore:     '#50c050',
  fishing:      '#6080d0',
  ranged:       '#70c060',
  thieving:     '#c060a0',
  cooking:      '#d08030',
  prayer:       '#e0d060',
  crafting:     '#c08050',
  firemaking:   '#e08020',
  magic:        '#8060e0',
  fletching:    '#60b060',
  woodcutting:  '#509040',
  runecraft:    '#d0c060',
  slayer:       '#c03030',
  farming:      '#709050',
  construction: '#c0a060',
  hunter:       '#906030',
};

const SKILL_DISPLAY_NAMES: Record<SkillId, string> = {
  attack:       'Attack',
  hitpoints:    'Hitpoints',
  mining:       'Mining',
  strength:     'Strength',
  agility:      'Agility',
  smithing:     'Smithing',
  defence:      'Defence',
  herblore:     'Herblore',
  fishing:      'Fishing',
  ranged:       'Ranged',
  thieving:     'Thieving',
  cooking:      'Cooking',
  prayer:       'Prayer',
  crafting:     'Crafting',
  firemaking:   'Firemaking',
  magic:        'Magic',
  fletching:    'Fletching',
  woodcutting:  'Woodcutting',
  runecraft:    'Runecraft',
  slayer:       'Slayer',
  farming:      'Farming',
  construction: 'Construction',
  hunter:       'Hunter',
};

export class SkillsUI {
  private container: HTMLElement;

  constructor() {
    this.container = document.createElement('div');
    this.container.style.cssText = `
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 2px;
      padding: 6px;
      align-content: start;
      min-height: 100%;
    `;
    this.buildCells();
  }

  get element(): HTMLElement {
    return this.container;
  }

  private buildCells(): void {
    for (const id of VISIBLE_SKILLS) {
      const cell = document.createElement('div');
      cell.style.cssText = `
        background: #0d0600;
        border: 1px solid #3d2010;
        border-radius: 2px;
        padding: 4px 5px;
        cursor: default;
        position: relative;
        overflow: hidden;
      `;

      const pip = document.createElement('div');
      pip.style.cssText = `
        width: 8px; height: 8px;
        border-radius: 50%;
        background: ${SKILL_COLORS[id]};
        display: inline-block;
        margin-right: 4px;
        vertical-align: middle;
        flex-shrink: 0;
      `;

      const label = document.createElement('span');
      label.style.cssText = `
        font-size: 9px;
        color: #c8a060;
        vertical-align: middle;
        display: block;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      `;
      label.textContent = SKILL_DISPLAY_NAMES[id];

      const lvl = document.createElement('span');
      lvl.className = `skill-lvl-${id}`;
      lvl.style.cssText = `
        font-size: 12px;
        font-weight: 700;
        color: #ffcc44;
        display: block;
      `;
      lvl.textContent = '1';

      const bar = document.createElement('div');
      bar.className = `skill-bar-${id}`;
      bar.style.cssText = `
        position: absolute;
        bottom: 0; left: 0;
        height: 2px;
        background: ${SKILL_COLORS[id]};
        width: 0%;
        transition: width 0.3s ease;
      `;

      const row = document.createElement('div');
      row.style.cssText = `display: flex; align-items: center; gap: 3px;`;
      row.appendChild(pip);
      row.appendChild(label);

      cell.appendChild(row);
      cell.appendChild(lvl);

      cell.appendChild(bar);
      this.container.appendChild(cell);
    }
  }

  update(skills: SkillsState): void {
    for (const id of VISIBLE_SKILLS) {
      const skill = skills[id];
      const lvlEl = this.container.querySelector(`.skill-lvl-${id}`) as HTMLElement | null;
      const barEl = this.container.querySelector(`.skill-bar-${id}`) as HTMLElement | null;
      if (lvlEl) lvlEl.textContent = String(skill.level);
      if (barEl) barEl.style.width = `${Math.round(progressToNextLevel(skill.xp) * 100)}%`;
    }
  }
}
