import type { HoverTarget, NPCState } from '../shared/types';
import { getPrimaryAction } from '../world/Interactables';

export class ContextInfo {
  private readonly el: HTMLElement;
  private readonly verbEl: HTMLSpanElement;
  private readonly subjectEl: HTMLSpanElement;
  private readonly suffixEl: HTMLSpanElement;

  constructor() {
    this.el = document.createElement('div');
    this.el.style.cssText = `
      position: fixed;
      top: 12px;
      left: 12px;
      z-index: 20;
      pointer-events: none;
      font-size: 15px;
      font-weight: 700;
      display: none;
      text-shadow:
        1px  1px 2px rgba(0,0,0,0.95),
       -1px -1px 2px rgba(0,0,0,0.95),
        1px -1px 2px rgba(0,0,0,0.95),
       -1px  1px 2px rgba(0,0,0,0.95);
    `;

    this.verbEl = document.createElement('span');
    this.verbEl.style.color = '#ffffff';

    this.subjectEl = document.createElement('span');
    this.subjectEl.style.color = '#ff981f';

    this.suffixEl = document.createElement('span');
    this.suffixEl.style.color = '#ffcc44';

    this.el.appendChild(this.verbEl);
    this.el.appendChild(this.subjectEl);
    this.el.appendChild(this.suffixEl);
    document.body.appendChild(this.el);
  }

  private override: { verb: string; subject: string } | null = null;

  setOverride(verb: string, subject: string): void {
    this.override = { verb, subject };
    this.verbEl.textContent = verb;
    this.subjectEl.textContent = subject ? ` ${subject}` : '';
    this.suffixEl.textContent = '';
    this.el.style.display = 'block';
  }

  clearOverride(): void {
    this.override = null;
  }

  update(hover: HoverTarget, npcs: NPCState[]): void {
    if (this.override) return;
    const primary = getPrimaryAction(hover, npcs);
    if (!primary) {
      this.el.style.display = 'none';
      return;
    }
    this.verbEl.textContent = primary.verb;
    this.subjectEl.textContent = primary.subject ? ` ${primary.subject}` : '';
    this.suffixEl.textContent = primary.subjectSuffix ? ` ${primary.subjectSuffix}` : '';
    this.el.style.display = 'block';
  }
}
