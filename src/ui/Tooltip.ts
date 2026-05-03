/**
 * Unified cursor-following tooltip singleton.
 *
 * Priority:  UI tooltips (inventory, equipment, skills, tabs)
 *         >  World hover tooltips (trees, rocks, NPCs, items)
 *
 * When a UI tooltip is active, world tooltips are suppressed.
 * This prevents the 60fps render loop from overwriting DOM-triggered tooltips.
 */

export interface TooltipSegment {
  text:   string;
  color?: string;   // defaults to '#ffffff'
}

// A single row in the tooltip: one segment or multiple same-line segments
export type TooltipLine = TooltipSegment | TooltipSegment[];

// ---- Private state ----------------------------------------------------------

let el: HTMLElement | null       = null;
let mouseX                       = 0;
let mouseY                       = 0;
let uiActive                     = false;
let trackingInit                 = false;

// ---- DOM setup --------------------------------------------------------------

function ensureTracking(): void {
  if (trackingInit) return;
  trackingInit = true;
  document.addEventListener('mousemove', (e) => {
    mouseX = e.clientX;
    mouseY = e.clientY;
    if (el && el.style.display !== 'none') positionEl();
  });
}

function getEl(): HTMLElement {
  if (!el) {
    el = document.createElement('div');
    el.style.cssText = `
      position: fixed;
      display: none;
      z-index: 5000;
      pointer-events: none;
      background: rgba(10, 5, 0, 0.90);
      border: 1px solid rgba(200, 160, 80, 0.38);
      border-radius: 2px;
      padding: 4px 9px;
      font-family: 'Segoe UI', system-ui, sans-serif;
      font-size: 11px;
      font-weight: 600;
      white-space: nowrap;
      text-shadow: 1px 1px 0 rgba(0,0,0,0.9);
      line-height: 1.6;
    `;
    document.body.appendChild(el);
    ensureTracking();
  }
  return el;
}

function positionEl(): void {
  if (!el) return;
  const OFFSET = 16;
  let left = mouseX + OFFSET;
  let top  = mouseY + OFFSET;
  const w  = el.offsetWidth;
  const h  = el.offsetHeight;
  if (left + w > window.innerWidth  - 4) left = mouseX - w - 4;
  if (top  + h > window.innerHeight - 4) top  = mouseY - h - OFFSET;
  el.style.left = `${left}px`;
  el.style.top  = `${top}px`;
}

function render(lines: TooltipLine[]): void {
  const tip = getEl();
  tip.innerHTML = '';

  for (const line of lines) {
    const row      = document.createElement('div');
    const segments = Array.isArray(line) ? line : [line];
    for (const seg of segments) {
      const span       = document.createElement('span');
      span.style.color = seg.color ?? '#ffffff';
      span.textContent = seg.text;
      row.appendChild(span);
    }
    tip.appendChild(row);
  }

  tip.style.display = 'block';
  positionEl();
}

// ---- Public API -------------------------------------------------------------

/**
 * Show a UI-priority tooltip (inventory, equipment, skill card, tab button).
 * Blocks world tooltips while active.
 */
export function setUITooltip(lines: TooltipLine[]): void {
  uiActive = true;
  render(lines);
}

/**
 * Hide the UI tooltip and lift the world-tooltip block.
 */
export function clearUITooltip(): void {
  uiActive = false;
  if (el) el.style.display = 'none';
}

/**
 * Show a world-hover tooltip (tree, rock, NPC, dropped item).
 * No-op if a UI tooltip is currently active.
 */
export function showWorldTooltip(lines: TooltipLine[]): void {
  if (uiActive) return;
  render(lines);
}

/**
 * Hide the world tooltip.
 * No-op if a UI tooltip is currently active.
 */
export function hideWorldTooltip(): void {
  if (uiActive) return;
  if (el) el.style.display = 'none';
}
