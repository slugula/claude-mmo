/**
 * Legacy shim — delegates to the unified Tooltip singleton.
 * `showItemTooltip` no longer needs an anchor element; it follows the cursor.
 */
import { setUITooltip, clearUITooltip } from './Tooltip';
import type { TooltipLine } from './Tooltip';

export function showItemTooltip(lines: TooltipLine[]): void {
  setUITooltip(lines);
}

export function hideItemTooltip(): void {
  clearUITooltip();
}
