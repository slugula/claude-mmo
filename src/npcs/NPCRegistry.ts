import npcData from '../data/npcs.json';

export interface DropEntry {
  itemId: string;
  quantity: number;
  rate: number;
}

export interface NPCDefinition {
  kind: string;
  name: string;
  size: number;
  isAttackable: boolean;
  ai: 'static' | 'wander';
  examine: string;
  uniqueActions: string[];
  maxHp: number;
  attack: number;
  defense: number;
  attackSpeedTicks: number;
  drops: DropEntry[];
  respawnTicks?: number;
}

const registry = new Map<string, NPCDefinition>();
for (const def of npcData as NPCDefinition[]) {
  registry.set(def.kind, def);
}

export function getNPCDef(kind: string): NPCDefinition {
  const def = registry.get(kind);
  if (!def) throw new Error(`No NPC definition for kind: ${kind}`);
  return def;
}

export function getAllNPCDefs(): NPCDefinition[] {
  return npcData as NPCDefinition[];
}
