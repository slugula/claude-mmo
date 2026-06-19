import type { SkillId } from '../shared/types';

// A production recipe: at a facility object, turn an input item into an output
// (with an optional fail output for skill-checked crafts like cooking). This is
// the data-driven blueprint shared by all production skills — cooking today,
// smelting/smithing later. Recipes are loaded from the DB (recipe_definitions);
// there is no JSON fallback (production content only exists in the DB).

export interface ProductionRecipe {
  id: string;
  facilityId: string;          // object id; matches the tile.obstacle of the station
  skill: SkillId;
  requiredLevel: number;
  xp: number;
  inputItemId: string;
  inputQty: number;
  outputItemId: string;
  outputQty: number;
  failItemId: string | null;   // null = never fails (always produces output)
  noFailLevel: number;         // level at/above which success is guaranteed
}

// facilityId -> recipes available there
const byFacility = new Map<string, ProductionRecipe[]>();

export function getRecipesForFacility(facilityId: string): ProductionRecipe[] {
  return byFacility.get(facilityId) ?? [];
}

export function reloadRecipes(defs: ProductionRecipe[]): void {
  byFacility.clear();
  for (const r of defs) {
    if (!byFacility.has(r.facilityId)) byFacility.set(r.facilityId, []);
    byFacility.get(r.facilityId)!.push(r);
  }
}

// Probability this attempt produces the success output rather than the fail
// output. Always-succeed recipes (no failItemId) return 1. Otherwise the chance
// scales linearly from a floor at requiredLevel up to 1.0 at noFailLevel — the
// same "level raises your success rate" feel as the gathering skills.
export function successChance(recipe: ProductionRecipe, level: number): number {
  if (recipe.failItemId === null) return 1;
  if (level >= recipe.noFailLevel) return 1;
  const span = Math.max(1, recipe.noFailLevel - recipe.requiredLevel);
  const t = Math.max(0, level - recipe.requiredLevel) / span;
  const FLOOR = 0.55;
  return Math.min(1, FLOOR + t * (1 - FLOOR));
}
