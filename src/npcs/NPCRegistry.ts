import npcData from '../data/npcs.json';

export interface DropEntry {
  itemId: string;
  quantity: number;
  rate: number;
}

export interface NPCDefinition {
  kind: string;
  name: string;
  size: number;       // kept for backward-compat; equals max(sizeX, sizeY)
  sizeX: number;
  sizeY: number;
  isAttackable: boolean;
  ai: 'static' | 'wander';
  examine: string;
  uniqueActions: string[];
  maxHp: number;
  attack: number;
  strength: number;
  meleeDefense: number;
  rangedDefense: number;
  attackSpeedTicks: number;
  drops: DropEntry[];
  respawnTicks?: number;
  isTalkable?: boolean;
  dialogue?: string;
}

const registry = new Map<string, NPCDefinition>();
for (const def of npcData as NPCDefinition[]) {
  const d = { ...def, sizeX: (def as NPCDefinition & { size?: number }).size ?? 1, sizeY: (def as NPCDefinition & { size?: number }).size ?? 1 };
  registry.set(d.kind, d);
}

export function getNPCDef(kind: string): NPCDefinition {
  const def = registry.get(kind);
  if (!def) throw new Error(`No NPC definition for kind: ${kind}`);
  return def;
}

export function getAllNPCDefs(): NPCDefinition[] {
  return Array.from(registry.values());
}

// Called by the server-side DB loader to hot-swap definitions without a restart.
export function reloadNPCs(defs: NPCDefinition[]): void {
  registry.clear();
  for (const def of defs) registry.set(def.kind, def);
}
