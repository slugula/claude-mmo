import { VISIBLE_SKILLS } from '../shared/types';
import type { SkillsState, SkillId } from '../shared/types';
import { progressToNextLevel } from '../systems/SkillSystem';

const SKILL_COLORS: Record<SkillId, string> = {
  warrior:      '#d4882c',
  hitpoints:    '#e06060',
  mining:       '#8888cc',
  defence:      '#60a0e0',
  agility:      '#80c0d0',
  smithing:     '#c0a840',
  herblore:     '#50c050',
  fishing:      '#6080d0',
  cooking:      '#d08030',
  ranged:       '#70c060',
  thieving:     '#c060a0',
  firemaking:   '#e08020',
  prayer:       '#e0d060',
  crafting:     '#c08050',
  fletching:    '#60b060',
  magic:        '#8060e0',
  woodcutting:  '#509040',
  runecraft:    '#d0c060',
  slayer:       '#c03030',
  farming:      '#709050',
  construction: '#c0a060',
  hunter:       '#906030',
  gunner:       '#00cfff',
};

const SKILL_DISPLAY_NAMES: Record<SkillId, string> = {
  warrior:      'Warrior',
  hitpoints:    'Hitpoints',
  mining:       'Mining',
  defence:      'Defence',
  agility:      'Agility',
  smithing:     'Smithing',
  herblore:     'Herblore',
  fishing:      'Fishing',
  cooking:      'Cooking',
  ranged:       'Ranged',
  thieving:     'Thieving',
  firemaking:   'Firemaking',
  prayer:       'Prayer',
  crafting:     'Crafting',
  fletching:    'Fletching',
  magic:        'Magic',
  woodcutting:  'Woodcutting',
  runecraft:    'Runecraft',
  slayer:       'Slayer',
  farming:      'Farming',
  construction: 'Construction',
  hunter:       'Hunter',
  gunner:       'Gunner',
};

export class SkillsUI {
  private container: HTMLElement;

  constructor() {
    this.container = document.createElement('div');
    this.container.style.cssText = `
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 3px;
      padding: 6px;
      align-content: start;
    `;
    this.buildCells();
  }

  get element(): HTMLElement {
    return this.container;
  }

  private buildCells(): void {
    for (const id of VISIBLE_SKILLS) {
      // Outer card — horizontal flex
      const cell = document.createElement('div');
      cell.style.cssText = `
        background: #0d0600;
        border: 1px solid #3d2010;
        border-radius: 2px;
        padding: 4px;
        cursor: default;
        position: relative;
        overflow: hidden;
        display: flex;
        align-items: center;
        gap: 6px;
      `;

      // Icon — use image file if one exists, otherwise fall back to coloured square
      const ICON_BASE = '/icons/skills/';
      const ICON_IDS = new Set<SkillId>(['woodcutting']);

      let icon: HTMLElement;
      if (ICON_IDS.has(id)) {
        const img = document.createElement('img');
        img.src = `${ICON_BASE}${id}.png`;
        img.alt = SKILL_DISPLAY_NAMES[id];
        img.style.cssText = `
          width: 36px;
          height: 36px;
          border-radius: 2px;
          flex-shrink: 0;
          image-rendering: pixelated;
          object-fit: contain;
        `;
        icon = img;
      } else {
        const div = document.createElement('div');
        div.style.cssText = `
          width: 36px;
          height: 36px;
          background: ${SKILL_COLORS[id]};
          border-radius: 2px;
          flex-shrink: 0;
        `;
        icon = div;
      }

      // Right-hand column: name on top, level below
      const right = document.createElement('div');
      right.style.cssText = `
        flex: 1;
        min-width: 0;
        display: flex;
        flex-direction: column;
        gap: 1px;
      `;

      const label = document.createElement('div');
      label.style.cssText = `
        font-size: 9px;
        color: #c8a060;
        text-transform: uppercase;
        letter-spacing: 0.5px;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
      `;
      label.textContent = SKILL_DISPLAY_NAMES[id];

      const lvl = document.createElement('div');
      lvl.className = `skill-lvl-${id}`;
      lvl.style.cssText = `
        font-size: 14px;
        font-weight: 700;
        color: #ffcc44;
        line-height: 1.1;
        white-space: nowrap;
      `;
      lvl.textContent = '1';

      // Thin XP progress bar along the bottom edge of the card
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

      right.appendChild(label);
      right.appendChild(lvl);
      cell.appendChild(icon);
      cell.appendChild(right);
      cell.appendChild(bar);
      this.container.appendChild(cell);
    }
  }

  update(skills: SkillsState, hp?: number, maxHp?: number): void {
    for (const id of VISIBLE_SKILLS) {
      const skill = skills[id] ?? { level: 1, xp: 0 };  // safe against old server builds
      const lvlEl = this.container.querySelector(`.skill-lvl-${id}`) as HTMLElement | null;
      const barEl = this.container.querySelector(`.skill-bar-${id}`) as HTMLElement | null;
      if (lvlEl) {
        // Hitpoints shows current / max HP; all other skills show their level
        if (id === 'hitpoints' && hp !== undefined && maxHp !== undefined) {
          lvlEl.textContent = `${hp} / ${maxHp}`;
        } else {
          lvlEl.textContent = String(skill.level);
        }
      }
      if (barEl) barEl.style.width = `${Math.round(progressToNextLevel(skill.xp) * 100)}%`;
    }
  }
}
