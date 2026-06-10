import { Router, type Request, type Response } from 'express';
import { pool } from './client';

export const entityRouter = Router();

// Localhost-only guard: reject requests from non-loopback IPs.
entityRouter.use((req, res, next) => {
  const ip = req.socket.remoteAddress ?? '';
  if (ip !== '127.0.0.1' && ip !== '::1' && ip !== '::ffff:127.0.0.1') {
    res.status(403).json({ error: 'Entity API is localhost-only' });
    return;
  }
  next();
});

entityRouter.use((req, _res, next) => {
  // Allow all methods for CORS from the editor running on the same machine.
  next();
});

// ---- Helpers ----------------------------------------------------------------

function ok(res: Response, data: unknown) { res.json(data); }
function err(res: Response, e: unknown, status = 500) {
  console.error('[EntityRouter]', e);
  res.status(status).json({ error: String(e) });
}

// Coerce empty strings (and undefined/null) to SQL NULL. The native editor
// serializes optional text fields as "" rather than omitting them; for columns
// with a foreign key (action_id, craft_action_id, drop_item_id, etc.) an empty
// string violates the FK, so it must become NULL.
function nullIfEmpty(v: unknown): string | null {
  if (v === undefined || v === null) return null;
  const s = String(v);
  return s.length === 0 ? null : s;
}

// ---- Actions ----------------------------------------------------------------

entityRouter.get('/actions', async (_req, res) => {
  try {
    const r = await pool.query(`
      SELECT id,
        COALESCE(display_name, '') AS display_name,
        COALESCE(handler_type,  '') AS handler_type
      FROM action_definitions ORDER BY id`);
    ok(res, r.rows);
  } catch (e) { err(res, e); }
});

entityRouter.post('/actions', async (req, res) => {
  try {
    const { id, display_name, handler_type } = req.body;
    await pool.query(
      'INSERT INTO action_definitions (id, display_name, handler_type) VALUES ($1,$2,$3)',
      [id, display_name, handler_type]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.put('/actions/:id', async (req, res) => {
  try {
    const { display_name, handler_type } = req.body;
    await pool.query(
      'UPDATE action_definitions SET display_name=$1, handler_type=$2 WHERE id=$3',
      [display_name, handler_type, req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.delete('/actions/:id', async (req, res) => {
  try {
    await pool.query('DELETE FROM action_definitions WHERE id=$1', [req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

// ---- Skills (fixed set; the editor only authors name + icon) ----------------

entityRouter.get('/skills', async (_req, res) => {
  try {
    const r = await pool.query(`
      SELECT id,
        COALESCE(name, '')      AS name,
        COALESCE(icon_path, '') AS icon_path,
        sort_order
      FROM skill_definitions ORDER BY sort_order, id`);
    ok(res, r.rows);
  } catch (e) { err(res, e); }
});

entityRouter.put('/skills/:id', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(
      'UPDATE skill_definitions SET name=$1, icon_path=$2 WHERE id=$3',
      [b.name, nullIfEmpty(b.icon_path), req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

// ---- Objects ----------------------------------------------------------------

entityRouter.get('/objects', async (_req, res) => {
  try {
    const r = await pool.query(`
      SELECT id,
        COALESCE(name,           '') AS name,
        COALESCE(model_path,     '') AS model_path,
        COALESCE(object_type,    'Decoration')    AS object_type,
        COALESCE(collision,      'full_blocking') AS collision,
        COALESCE(size_x,         1)  AS size_x,
        COALESCE(size_y,         1)  AS size_y,
        COALESCE(action_id,      '') AS action_id,
        COALESCE(required_skill, '') AS required_skill,
        COALESCE(required_level, 0)  AS required_level,
        COALESCE(drop_item_id,   '') AS drop_item_id,
        COALESCE(drop_quantity,  1)  AS drop_quantity,
        COALESCE(respawn_ticks,  25) AS respawn_ticks,
        COALESCE(craft_action_id,'') AS craft_action_id,
        COALESCE(examine_text,   '') AS examine_text,
        COALESCE(default_clip,   '') AS default_clip,
        COALESCE(looping,        TRUE) AS looping,
        COALESCE(rotation_x,     0)  AS rotation_x,
        COALESCE(rotation_y,     0)  AS rotation_y,
        COALESCE(rotation_z,     0)  AS rotation_z,
        COALESCE(depleted_object_id, '') AS depleted_object_id,
        COALESCE(pickable,       TRUE) AS pickable
      FROM object_definitions ORDER BY id`);
    ok(res, r.rows);
  } catch (e) { err(res, e); }
});

entityRouter.get('/objects/:id', async (req, res) => {
  try {
    const r = await pool.query('SELECT * FROM object_definitions WHERE id=$1', [req.params.id]);
    if (r.rows.length === 0) { res.status(404).json({ error: 'Not found' }); return; }
    ok(res, r.rows[0]);
  } catch (e) { err(res, e); }
});

entityRouter.post('/objects', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(`
      INSERT INTO object_definitions
        (id,name,model_path,object_type,collision,size_x,size_y,action_id,required_skill,
         required_level,drop_item_id,drop_quantity,respawn_ticks,craft_action_id,examine_text,
         default_clip,looping,rotation_x,rotation_y,rotation_z,depleted_object_id,pickable)
      VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22)`,
      [b.id,b.name,nullIfEmpty(b.model_path),b.object_type??'Decoration',b.collision??'full_blocking',
       b.size_x??1,b.size_y??1,nullIfEmpty(b.action_id),nullIfEmpty(b.required_skill),b.required_level??null,
       nullIfEmpty(b.drop_item_id),b.drop_quantity??1,b.respawn_ticks??25,nullIfEmpty(b.craft_action_id),nullIfEmpty(b.examine_text),
       nullIfEmpty(b.default_clip),b.looping??true,b.rotation_x??0,b.rotation_y??0,b.rotation_z??0,nullIfEmpty(b.depleted_object_id),b.pickable??true]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.put('/objects/:id', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(`
      UPDATE object_definitions SET
        name=$1,model_path=$2,object_type=$3,collision=$4,size_x=$5,size_y=$6,
        action_id=$7,required_skill=$8,required_level=$9,drop_item_id=$10,
        drop_quantity=$11,respawn_ticks=$12,craft_action_id=$13,examine_text=$14,
        default_clip=$15,looping=$16,rotation_x=$17,rotation_y=$18,rotation_z=$19,depleted_object_id=$20,pickable=$21
      WHERE id=$22`,
      [b.name,nullIfEmpty(b.model_path),b.object_type,b.collision,b.size_x??1,b.size_y??1,
       nullIfEmpty(b.action_id),nullIfEmpty(b.required_skill),b.required_level??null,nullIfEmpty(b.drop_item_id),
       b.drop_quantity??1,b.respawn_ticks??25,nullIfEmpty(b.craft_action_id),nullIfEmpty(b.examine_text),
       nullIfEmpty(b.default_clip),b.looping??true,b.rotation_x??0,b.rotation_y??0,b.rotation_z??0,nullIfEmpty(b.depleted_object_id),b.pickable??true,
       req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.delete('/objects/:id', async (req, res) => {
  try {
    await pool.query('DELETE FROM object_definitions WHERE id=$1', [req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

// ---- NPCs -------------------------------------------------------------------

entityRouter.get('/npcs', async (_req, res) => {
  try {
    const ndefs = await pool.query(`
      SELECT id,
        COALESCE(name,               '') AS name,
        COALESCE(model_path,         '') AS model_path,
        COALESCE(size_x,             1)  AS size_x,
        COALESCE(size_y,             1)  AS size_y,
        COALESCE(is_attackable,      false) AS is_attackable,
        COALESCE(max_hp,             1)  AS max_hp,
        COALESCE(attack,             0)  AS attack,
        COALESCE(strength,           0)  AS strength,
        COALESCE(melee_defense,      0)  AS melee_defense,
        COALESCE(ranged_defense,     0)  AS ranged_defense,
        COALESCE(attack_speed_ticks, 16) AS attack_speed_ticks,
        COALESCE(respawn_ticks,      150)AS respawn_ticks,
        COALESCE(is_talkable,        false) AS is_talkable,
        COALESCE(dialogue,           '') AS dialogue,
        COALESCE(ai,                 'static') AS ai,
        COALESCE(examine_text,       '') AS examine_text
      FROM npc_definitions ORDER BY id`);
    const drops = await pool.query('SELECT * FROM npc_drops');
    const dropMap = new Map<string, object[]>();
    for (const d of drops.rows) {
      if (!dropMap.has(d.npc_id)) dropMap.set(d.npc_id, []);
      dropMap.get(d.npc_id)!.push({ item_id: d.item_id, quantity: d.quantity, rate: d.rate });
    }
    ok(res, ndefs.rows.map(n => ({ ...n, drops: dropMap.get(n.id) ?? [] })));
  } catch (e) { err(res, e); }
});

entityRouter.get('/npcs/:id', async (req, res) => {
  try {
    const n = await pool.query('SELECT * FROM npc_definitions WHERE id=$1', [req.params.id]);
    if (n.rows.length === 0) { res.status(404).json({ error: 'Not found' }); return; }
    const d = await pool.query('SELECT item_id,quantity,rate FROM npc_drops WHERE npc_id=$1', [req.params.id]);
    ok(res, { ...n.rows[0], drops: d.rows });
  } catch (e) { err(res, e); }
});

entityRouter.post('/npcs', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(`
      INSERT INTO npc_definitions
        (id,name,model_path,size_x,size_y,is_attackable,max_hp,attack,strength,
         melee_defense,ranged_defense,attack_speed_ticks,respawn_ticks,is_talkable,dialogue,ai,examine_text)
      VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17)`,
      [b.id,b.name,b.model_path??null,b.size_x??1,b.size_y??1,b.is_attackable??false,
       b.max_hp??1,b.attack??0,b.strength??0,b.melee_defense??0,b.ranged_defense??0,
       b.attack_speed_ticks??16,b.respawn_ticks??150,b.is_talkable??false,
       b.dialogue??null,b.ai??'static',b.examine_text??null]);
    if (Array.isArray(b.drops)) {
      for (const d of b.drops) {
        await pool.query(
          'INSERT INTO npc_drops (npc_id,item_id,quantity,rate) VALUES ($1,$2,$3,$4) ON CONFLICT DO NOTHING',
          [b.id, d.item_id, d.quantity??1, d.rate??1.0]);
      }
    }
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.put('/npcs/:id', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(`
      UPDATE npc_definitions SET
        name=$1,model_path=$2,size_x=$3,size_y=$4,is_attackable=$5,max_hp=$6,attack=$7,
        strength=$8,melee_defense=$9,ranged_defense=$10,attack_speed_ticks=$11,
        respawn_ticks=$12,is_talkable=$13,dialogue=$14,ai=$15,examine_text=$16
      WHERE id=$17`,
      [b.name,b.model_path??null,b.size_x??1,b.size_y??1,b.is_attackable??false,
       b.max_hp??1,b.attack??0,b.strength??0,b.melee_defense??0,b.ranged_defense??0,
       b.attack_speed_ticks??16,b.respawn_ticks??150,b.is_talkable??false,
       b.dialogue??null,b.ai??'static',b.examine_text??null,req.params.id]);
    if (Array.isArray(b.drops)) {
      await pool.query('DELETE FROM npc_drops WHERE npc_id=$1', [req.params.id]);
      for (const d of b.drops) {
        await pool.query(
          'INSERT INTO npc_drops (npc_id,item_id,quantity,rate) VALUES ($1,$2,$3,$4)',
          [req.params.id, d.item_id, d.quantity??1, d.rate??1.0]);
      }
    }
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.delete('/npcs/:id', async (req, res) => {
  try {
    await pool.query('DELETE FROM npc_definitions WHERE id=$1', [req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

// ---- Items ------------------------------------------------------------------

entityRouter.get('/items', async (_req, res) => {
  try {
    const r = await pool.query(`
      SELECT id,
        COALESCE(name,            '') AS name,
        COALESCE(stackable,       false) AS stackable,
        COALESCE(tradable,        true)  AS tradable,
        COALESCE(value,           0)  AS value,
        COALESCE(examine_text,    '') AS examine_text,
        COALESCE(item_type,       'resource') AS item_type,
        COALESCE(equip_slot,      '') AS equip_slot,
        COALESCE(two_handed,      false) AS two_handed,
        COALESCE(melee_attack,    0)  AS melee_attack,
        COALESCE(melee_strength,  0)  AS melee_strength,
        COALESCE(melee_defense,   0)  AS melee_defense,
        COALESCE(ranged_attack,   0)  AS ranged_attack,
        COALESCE(ranged_strength, 0)  AS ranged_strength,
        COALESCE(ranged_defense,  0)  AS ranged_defense,
        COALESCE(required_skill,  '') AS required_skill,
        COALESCE(required_level,  0)  AS required_level,
        COALESCE(tool_type,       '') AS tool_type,
        COALESCE(combat_style,    '') AS combat_style,
        COALESCE(heal_amount,     0)  AS heal_amount,
        COALESCE(sprite_path,     '') AS sprite_path,
        COALESCE(model_dropped,   '') AS model_dropped,
        COALESCE(model_equipped,  '') AS model_equipped,
        COALESCE(grip_joint,      '') AS grip_joint,
        COALESCE(grip_pos_x,      0)  AS grip_pos_x,
        COALESCE(grip_pos_y,      0)  AS grip_pos_y,
        COALESCE(grip_pos_z,      0)  AS grip_pos_z,
        COALESCE(grip_rot_x,      0)  AS grip_rot_x,
        COALESCE(grip_rot_y,      0)  AS grip_rot_y,
        COALESCE(grip_rot_z,      0)  AS grip_rot_z,
        COALESCE(grip_scale,      1)  AS grip_scale
      FROM item_definitions ORDER BY id`);
    ok(res, r.rows);
  } catch (e) { err(res, e); }
});

entityRouter.get('/items/:id', async (req, res) => {
  try {
    const r = await pool.query('SELECT * FROM item_definitions WHERE id=$1', [req.params.id]);
    if (r.rows.length === 0) { res.status(404).json({ error: 'Not found' }); return; }
    ok(res, r.rows[0]);
  } catch (e) { err(res, e); }
});

entityRouter.post('/items', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(`
      INSERT INTO item_definitions
        (id,name,stackable,tradable,value,examine_text,item_type,equip_slot,two_handed,
         melee_attack,melee_strength,melee_defense,ranged_attack,ranged_strength,ranged_defense,
         required_skill,required_level,tool_type,combat_style,heal_amount,sprite_path,model_dropped,model_equipped,
         grip_joint,grip_pos_x,grip_pos_y,grip_pos_z,grip_rot_x,grip_rot_y,grip_rot_z,grip_scale)
      VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17,$18,$19,$20,$21,$22,$23,
              $24,$25,$26,$27,$28,$29,$30,$31)`,
      [b.id,b.name,b.stackable??false,b.tradable??true,b.value??0,b.examine_text??null,
       b.item_type??'resource',b.equip_slot??null,b.two_handed??false,
       b.melee_attack??0,b.melee_strength??0,b.melee_defense??0,
       b.ranged_attack??0,b.ranged_strength??0,b.ranged_defense??0,
       b.required_skill??null,b.required_level??null,b.tool_type??null,b.combat_style??null,
       b.heal_amount??null,b.sprite_path??null,b.model_dropped??null,b.model_equipped??null,
       b.grip_joint??null,b.grip_pos_x??0,b.grip_pos_y??0,b.grip_pos_z??0,
       b.grip_rot_x??0,b.grip_rot_y??0,b.grip_rot_z??0,b.grip_scale??1]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.put('/items/:id', async (req, res) => {
  try {
    const b = req.body;
    await pool.query(`
      UPDATE item_definitions SET
        name=$1,stackable=$2,tradable=$3,value=$4,examine_text=$5,item_type=$6,equip_slot=$7,
        two_handed=$8,melee_attack=$9,melee_strength=$10,melee_defense=$11,ranged_attack=$12,
        ranged_strength=$13,ranged_defense=$14,required_skill=$15,required_level=$16,
        tool_type=$17,combat_style=$18,heal_amount=$19,sprite_path=$20,model_dropped=$21,model_equipped=$22,
        grip_joint=$23,grip_pos_x=$24,grip_pos_y=$25,grip_pos_z=$26,
        grip_rot_x=$27,grip_rot_y=$28,grip_rot_z=$29,grip_scale=$30
      WHERE id=$31`,
      [b.name,b.stackable??false,b.tradable??true,b.value??0,b.examine_text??null,
       b.item_type??'resource',b.equip_slot??null,b.two_handed??false,
       b.melee_attack??0,b.melee_strength??0,b.melee_defense??0,
       b.ranged_attack??0,b.ranged_strength??0,b.ranged_defense??0,
       b.required_skill??null,b.required_level??null,b.tool_type??null,b.combat_style??null,
       b.heal_amount??null,b.sprite_path??null,b.model_dropped??null,b.model_equipped??null,
       b.grip_joint??null,b.grip_pos_x??0,b.grip_pos_y??0,b.grip_pos_z??0,
       b.grip_rot_x??0,b.grip_rot_y??0,b.grip_rot_z??0,b.grip_scale??1,req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});

entityRouter.delete('/items/:id', async (req, res) => {
  try {
    await pool.query('DELETE FROM item_definitions WHERE id=$1', [req.params.id]);
    ok(res, { ok: true });
  } catch (e) { err(res, e); }
});
