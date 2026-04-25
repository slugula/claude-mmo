let el: HTMLElement | null = null;

function getEl(): HTMLElement {
  if (el) return el;
  el = document.createElement('div');
  el.style.cssText = `
    position: fixed;
    display: none;
    z-index: 5000;
    pointer-events: none;
    background: rgba(0, 0, 0, 0.78);
    border: 1px solid rgba(255, 255, 255, 0.30);
    border-radius: 2px;
    padding: 3px 7px;
    font-family: 'Segoe UI', system-ui, sans-serif;
    font-size: 11px;
    font-weight: 600;
    color: #ffffff;
    white-space: nowrap;
    text-shadow: 1px 1px 0 rgba(0,0,0,0.8);
    transform: translate(-50%, calc(-100% - 4px));
  `;
  document.body.appendChild(el);
  return el;
}

export function showItemTooltip(text: string, anchor: HTMLElement): void {
  const tip = getEl();
  tip.textContent = text;
  tip.style.display = 'block';
  const r = anchor.getBoundingClientRect();
  tip.style.left = `${r.left + r.width / 2}px`;
  tip.style.top  = `${r.top}px`;
}

export function hideItemTooltip(): void {
  getEl().style.display = 'none';
}
