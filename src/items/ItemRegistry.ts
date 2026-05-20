import type { ItemDefinition } from '../shared/types';
import itemData from '../data/items.json';

const registry = new Map<string, ItemDefinition>();
for (const item of itemData as ItemDefinition[]) registry.set(item.id, item);

export function getItem(id: string): ItemDefinition | undefined {
  return registry.get(id);
}

export function getAllItems(): ItemDefinition[] {
  return Array.from(registry.values());
}

// Called by the server-side DB loader to hot-swap definitions without a restart.
export function reloadItems(defs: ItemDefinition[]): void {
  registry.clear();
  for (const def of defs) registry.set(def.id, def);
}
