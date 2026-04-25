import type { ContextEntry } from '../world/Interactables';

const UI_PANEL_WIDTH = 220;

export class ContextMenu {
  private readonly el: HTMLElement;
  private dismissHandler: ((e: MouseEvent) => void) | null = null;
  private readonly dispatch: (entry: ContextEntry, screenX: number, screenY: number) => void;

  constructor(dispatch: (entry: ContextEntry, screenX: number, screenY: number) => void) {
    this.dispatch = dispatch;

    this.el = document.createElement('div');
    this.el.style.cssText = `
      position: fixed;
      z-index: 1000;
      background: #1a0d00;
      border: 1px solid #8b6c3e;
      border-radius: 3px;
      padding: 2px;
      min-width: 155px;
      box-shadow: 2px 2px 12px rgba(0,0,0,0.85);
      display: none;
      user-select: none;
      font-size: 12px;
    `;
    document.body.appendChild(this.el);
  }

  show(entries: ContextEntry[], screenX: number, screenY: number): void {
    this.el.innerHTML = '';

    const header = document.createElement('div');
    header.style.cssText = `
      color: white;
      font-weight: 700;
      font-size: 11px;
      letter-spacing: 0.5px;
      padding: 5px 10px 4px;
      border-bottom: 1px solid #3d2010;
      margin-bottom: 2px;
    `;
    header.textContent = 'Choose Option';
    this.el.appendChild(header);

    for (const entry of entries) {
      this.el.appendChild(this.makeItem(entry.verb, entry.subject, entry.subjectSuffix, false, (e) => {
        this.dispatch(entry, e.clientX, e.clientY);
        this.hide();
      }));
    }

    this.el.appendChild(this.makeItem('Cancel', '', undefined, true, () => this.hide()));

    this.el.style.display = 'block';
    const w = this.el.offsetWidth;
    const h = this.el.offsetHeight;
    const maxRight = window.innerWidth - UI_PANEL_WIDTH;
    const left = screenX + w > maxRight ? screenX - w : screenX;
    const top  = screenY + h > window.innerHeight ? screenY - h : screenY;
    this.el.style.left = `${left}px`;
    this.el.style.top  = `${top}px`;

    if (this.dismissHandler) document.removeEventListener('mousedown', this.dismissHandler);
    this.dismissHandler = (e: MouseEvent) => {
      if (!this.el.contains(e.target as Node)) this.hide();
    };
    setTimeout(() => {
      document.addEventListener('mousedown', this.dismissHandler!);
    }, 0);
  }

  hide(): void {
    this.el.style.display = 'none';
    if (this.dismissHandler) {
      document.removeEventListener('mousedown', this.dismissHandler);
      this.dismissHandler = null;
    }
  }

  private makeItem(
    verb: string,
    subject: string,
    subjectSuffix: string | undefined,
    isCancel: boolean,
    onClick: (e: MouseEvent) => void,
  ): HTMLElement {
    const item = document.createElement('div');
    item.style.cssText = `
      padding: 3px 10px;
      cursor: pointer;
      border-radius: 2px;
      line-height: 1.7;
      white-space: nowrap;
    `;
    item.addEventListener('mouseenter', () => { item.style.background = '#2d1b0e'; });
    item.addEventListener('mouseleave', () => { item.style.background = '';        });
    item.addEventListener('mousedown',  (e) => { e.stopPropagation(); onClick(e);  });

    if (isCancel) {
      const span = document.createElement('span');
      span.style.color = '#ff4444';
      span.textContent = 'Cancel';
      item.appendChild(span);
    } else {
      const verbSpan = document.createElement('span');
      verbSpan.style.color = '#ffffff';
      verbSpan.textContent = verb;
      item.appendChild(verbSpan);

      if (subject) {
        const subjectSpan = document.createElement('span');
        subjectSpan.style.color = '#ff981f';
        subjectSpan.textContent = ` ${subject}`;
        item.appendChild(subjectSpan);
      }
      if (subjectSuffix) {
        const suffixSpan = document.createElement('span');
        suffixSpan.style.color = '#ffcc44';
        suffixSpan.textContent = ` ${subjectSuffix}`;
        item.appendChild(suffixSpan);
      }
    }

    return item;
  }
}
