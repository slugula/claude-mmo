import type { PlayerState, ItemDefinition } from '../shared/types';
import { getItem } from '../items/ItemRegistry';

// =====================================================================
// GatheringTool — which tool a player visually holds while gathering.
// =====================================================================
//
// While a player is performing a gathering action, the client overrides their
// equipped weapon with the relevant tool (an axe while chopping, a pickaxe
// while mining, a fishing rod while fishing). The server resolves the concrete
// item id here so it works for remote players too (the client has no view of
// their inventory) and stays authoritative.
//
// SCALABILITY: adding a new gathering skill only needs one row below mapping its
// "active" predicate to a tool type. The tool type already lives on items
// (ItemDefinition.toolType), so the specific item is found automatically — no
// per-skill rendering code.

type ToolType = NonNullable<ItemDefinition['toolType']>;

const TOOL_ACTIVITIES: { active: (p: PlayerState) => boolean; toolType: ToolType }[] = [
  { active: p => p.chopTargetX !== null, toolType: 'axe' },
  { active: p => p.mineTargetX !== null, toolType: 'pickaxe' },
  { active: p => p.fishTargetX !== null, toolType: 'fishing_rod' },
];

// First item of `toolType` the player holds — equipped hand first, then
// inventory (covers fishing rods, which can't be equipped but sit in the bag).
function findToolOfType(p: PlayerState, toolType: ToolType): string {
  const eq = p.equipped.rightHand;
  if (eq && getItem(eq.itemId)?.toolType === toolType) return eq.itemId;
  for (const slot of p.inventory) {
    if (slot && getItem(slot.itemId)?.toolType === toolType) return slot.itemId;
  }
  return '';
}

// The item id the player should visually hold this tick ('' = none → show the
// normal equipped weapon).
export function resolveActiveTool(p: PlayerState): string {
  for (const a of TOOL_ACTIVITIES) {
    if (a.active(p)) return findToolOfType(p, a.toolType);
  }
  return '';
}
