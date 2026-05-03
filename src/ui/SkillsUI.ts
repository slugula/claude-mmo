import { VISIBLE_SKILLS } from '../shared/types';
import type { SkillsState, SkillId } from '../shared/types';
import { progressToNextLevel, xpForLevel, xpToNextLevel } from '../systems/SkillSystem';
import { setUITooltip, clearUITooltip } from './Tooltip';

const SKILL_COLORS: Record<SkillId, string> = {
  warrior:      '#d4882c',
  hitpoints:    '#e06060',
  defence:      '#60a0e0',
  woodcutting:  '#509040',
  mining:       '#8888aa',
  gunner:       '#00cfff',
};

const SKILL_DISPLAY_NAMES: Record<SkillId, string> = {
  warrior:      'Warrior',
  hitpoints:    'Hitpoints',
  mining:       'Mining',
  defence:      'Defence',
  woodcutting:  'Woodcutting',
  gunner:       'Gunner',
};

export class SkillsUI {
  private container: HTMLElement;

  // Stored on each update so hover tooltips can read live data
  private currentSkills: SkillsState | null = null;
  private currentHp: number                 = 0;
  private currentMaxHp: number              = 0;

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
        background: #0d060048;
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
      let icon: HTMLElement;

      const img = document.createElement('img');
      img.src = `${ICON_BASE}${id}.png`;
      img.style.cssText = `
        width: 36px;
        height: 36px;
        border-radius: 2px;
        flex-shrink: 0;
        image-rendering: pixelated;
        object-fit: none;
      `;

      img.onerror = () => {
        // If image fails to load, replace with the fallback div
        const div = document.createElement('div');
        div.style.cssText = `
          width: 32px;
          height: 32px;
          background: ${SKILL_COLORS[id]};
          border-radius: 2px;
          flex-shrink: 0;
      `;

        if (img.parentNode) {
          img.parentNode.replaceChild(div, img);
        }
      };

      icon = img

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

      // ---- Hover tooltip ----
      cell.addEventListener('mouseenter', () => {
        if (!this.currentSkills) return;
        const skill  = this.currentSkills[id] ?? { level: 1, xp: 0 };
        const lines  = this.buildSkillTooltip(id, skill.xp, skill.level);
        setUITooltip(lines);
      });
      cell.addEventListener('mouseleave', () => {
        clearUITooltip();
      });
    }
  }

  private buildSkillTooltip(
    id: SkillId,
    xp: number,
    level: number,
  ) {
    const displayName = SKILL_DISPLAY_NAMES[id];
    const color       = SKILL_COLORS[id];
    const remaining   = xpToNextLevel(xp);
    const nextAt      = level < 99 ? xpForLevel(level + 1) : null;

    const fmt = (n: number) => n.toLocaleString();

    const lines: import('./Tooltip').TooltipLine[] = [
      // Skill name row — coloured
      [{ text: displayName, color }],
    ];

    // Hitpoints also shows current / max HP
    if (id === 'hitpoints') {
      lines.push([
        { text: 'HP: ', color: '#888888' },
        { text: `${this.currentHp} / ${this.currentMaxHp}`, color: '#ffffff' },
      ]);
    }

    lines.push(
      [{ text: 'XP: ', color: '#888888' }, { text: fmt(xp), color: '#ffffff' }],
    );

    if (nextAt !== null) {
      lines.push(
        [{ text: 'Next level at: ', color: '#888888' }, { text: `${fmt(nextAt)} XP`, color: '#ffcc44' }],
        [{ text: 'Remaining: ',    color: '#888888' }, { text: `${fmt(remaining)} XP`, color: '#ffffff' }],
      );
    } else {
      lines.push([{ text: 'Maximum level reached', color: '#ffcc44' }]);
    }

    return lines;
  }

  update(skills: SkillsState, hp?: number, maxHp?: number): void {
    this.currentSkills  = skills;
    this.currentHp      = hp    ?? 0;
    this.currentMaxHp   = maxHp ?? 0;

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
