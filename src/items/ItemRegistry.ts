import type { ItemDefinition } from '../shared/types';
import itemData from '../data/items.json';

const items = itemData as ItemDefinition[];
const registry = new Map<string, ItemDefinition>();
for (const item of items) registry.set(item.id, item);

export function getItem(id: string): ItemDefinition | undefined {
  return registry.get(id);
}

export function getAllItems(): ItemDefinition[] {
  return items;
}
