import { pool } from './client';
import type { ItemDefinition } from '../../src/shared/types';
import { reloadItems } from '../../src/items/ItemRegistry';
import type { NPCDefinition, DropEntry } from '../../src/npcs/NPCRegistry';
import { reloadNPCs } from '../../src/npcs/NPCRegistry';

function rowToItemDef(row: Record<string, unknown>): ItemDefinition {
  const def: ItemDefinition = {
    id:        row.id        as string,
    name:      row.name      as string,
    stackable: row.stackable as boolean,
    value:     row.value     as number,
  };
  if (row.tradable      != null) def.tradable   = row.tradable     as boolean;
  if (row.examine_text  != null) def.examine     = row.examine_text as string;
  if (row.equip_slot    != null) def.equipSlot   = row.equip_slot   as ItemDefinition['equipSlot'];
  if (row.tool_type     != null) def.toolType    = row.tool_type    as ItemDefinition['toolType'];
  if (row.combat_style  != null) def.combatStyle = row.combat_style as ItemDefinition['combatStyle'];
  if (row.two_handed    != null) def.twoHanded   = row.two_handed   as boolean;
  if (row.required_skill != null && row.required_level != null) {
    def.requirements = { [row.required_skill as string]: row.required_level as number } as ItemDefinition['requirements'];
  }
  const ma = (row.melee_attack    as number) ?? 0;
  const ms = (row.melee_strength  as number) ?? 0;
  const md = (row.melee_defense   as number) ?? 0;
  const ra = (row.ranged_attack   as number) ?? 0;
  const rs = (row.ranged_strength as number) ?? 0;
  const rd = (row.ranged_defense  as number) ?? 0;
  if (ma || ms || md || ra || rs || rd) {
    def.stats = { meleeAttackBonus: ma, meleeStrengthBonus: ms, meleeDefenseBonus: md,
                  rangedAttackBonus: ra, rangedStrengthBonus: rs, rangedDefenseBonus: rd };
  }
  return def;
}

function rowToNPCDef(row: Record<string, unknown>, drops: DropEntry[]): NPCDefinition {
  const sizeX = (row.size_x as number) ?? 1;
  const sizeY = (row.size_y as number) ?? 1;
  const isAttackable = row.is_attackable as boolean;
  const isTalkable   = row.is_talkable  as boolean;
  const uniqueActions: string[] = [];
  if (isAttackable) uniqueActions.push('Attack');
  if (isTalkable)   uniqueActions.push('Talk-to');
  return {
    kind:             row.id              as string,
    name:             row.name            as string,
    size:             Math.max(sizeX, sizeY),
    sizeX, sizeY, isAttackable, isTalkable,
    dialogue:         row.dialogue        as string | undefined,
    ai:              (row.ai              as 'static' | 'wander') ?? 'static',
    examine:         (row.examine_text    as string) ?? '',
    uniqueActions,
    maxHp:            (row.max_hp             as number) ?? 1,
    attack:           (row.attack             as number) ?? 0,
    strength:         (row.strength           as number) ?? 0,
    meleeDefense:     (row.melee_defense      as number) ?? 0,
    rangedDefense:    (row.ranged_defense     as number) ?? 0,
    attackSpeedTicks: (row.attack_speed_ticks as number) ?? 16,
    respawnTicks:     (row.respawn_ticks      as number) ?? 150,
    drops,
  };
}

// Raw DB rows for the four definition tables, cached at startup. They carry the
// client-only rendering fields (sprite_path, model_*, depleted_object_id,
// pickable, …) that the gameplay registries drop, and are relayed verbatim to
// the client in the `init` message so shared builds get the authored content
// without needing localhost DB access.
let clientDefs: {
  items:   Record<string, unknown>[];
  objects: Record<string, unknown>[];
  npcs:    Record<string, unknown>[];
  actions: Record<string, unknown>[];
  skills:  Record<string, unknown>[];
} = { items: [], objects: [], npcs: [], actions: [], skills: [] };

export function getClientDefs() { return clientDefs; }

export async function loadEntitiesFromDB(): Promise<void> {
  try {
    const [itemRows, npcRows, dropRows, objectRows, actionRows] = await Promise.all([
      pool.query('SELECT * FROM item_definitions   ORDER BY id'),
      pool.query('SELECT * FROM npc_definitions    ORDER BY id'),
      pool.query('SELECT * FROM npc_drops'),
      pool.query('SELECT * FROM object_definitions ORDER BY id'),
      pool.query('SELECT * FROM action_definitions ORDER BY id'),
    ]);
    // Skills are queried separately and tolerantly: if the skill_definitions
    // table hasn't been migrated yet, skill icons just stay empty instead of
    // breaking the load of every other definition.
    let skillRows: { rows: Record<string, unknown>[] } = { rows: [] };
    try {
      skillRows = await pool.query('SELECT * FROM skill_definitions ORDER BY sort_order, id');
    } catch {
      console.warn('[EntityLoader] skill_definitions not found — run schema.sql to enable skill icons');
    }
    // Strip null-valued columns from every def row before relaying them to the
    // client. The native client parses these with glaze into structs whose
    // string/number fields are non-nullable; a JSON `null` (from a nullable DB
    // column like action_id or required_level) fails the parse for the WHOLE
    // init message, so shared-build clients silently get no item/object/npc
    // defs and render an empty world. Dropping null keys lets glaze fall back
    // to each field's default (kPermissive tolerates missing keys). The
    // localhost EntityRouter path already avoids nulls, which is why dev builds
    // were unaffected.
    const stripNulls = (rows: Record<string, unknown>[]): Record<string, unknown>[] =>
      rows.map(row => {
        const out: Record<string, unknown> = {};
        for (const [k, v] of Object.entries(row)) if (v !== null) out[k] = v;
        return out;
      });
    clientDefs = {
      items:   stripNulls(itemRows.rows),
      objects: stripNulls(objectRows.rows),
      npcs:    stripNulls(npcRows.rows),
      actions: stripNulls(actionRows.rows),
      skills:  stripNulls(skillRows.rows),
    };
    if (itemRows.rows.length > 0) {
      reloadItems(itemRows.rows.map(rowToItemDef));
      console.log(`[EntityLoader] loaded ${itemRows.rows.length} items from DB`);
    }
    if (npcRows.rows.length > 0) {
      const dropMap = new Map<string, DropEntry[]>();
      for (const d of dropRows.rows) {
        const qty = d.quantity as number;
        if (qty <= 0) {
          console.warn(`[EntityLoader] npc_drops row ${d.npc_id}/${d.item_id} has quantity=${qty} — skipping`);
          continue;
        }
        if (!dropMap.has(d.npc_id)) dropMap.set(d.npc_id, []);
        dropMap.get(d.npc_id)!.push({ itemId: d.item_id, quantity: qty, rate: d.rate as number });
      }
      reloadNPCs(npcRows.rows.map(r => rowToNPCDef(r, dropMap.get(r.id as string) ?? [])));
      console.log(`[EntityLoader] loaded ${npcRows.rows.length} NPCs from DB`);
    }
  } catch (e) {
    console.warn('[EntityLoader] DB load failed — registries retain JSON defaults:', e);
  }
}
