-- Run once against the target PostgreSQL database:
--   psql -d osrs -f server/db/schema.sql

CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS players (
  id            UUID        PRIMARY KEY DEFAULT gen_random_uuid(),
  username      TEXT        UNIQUE NOT NULL,
  password_hash TEXT        NOT NULL,
  player_state  JSONB,
  created_at    TIMESTAMPTZ DEFAULT now(),
  last_login    TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS players_username_idx ON players (username);

-- ---- Entity Definitions (data-driven game content) -------------------------

CREATE TABLE IF NOT EXISTS action_definitions (
  id            TEXT PRIMARY KEY,
  display_name  TEXT NOT NULL,
  handler_type  TEXT NOT NULL   -- gather_resource | production_facility | equip | eat | talk | bank | examine
);

CREATE TABLE IF NOT EXISTS object_definitions (
  id              TEXT PRIMARY KEY,
  name            TEXT NOT NULL,
  model_path      TEXT,
  object_type     TEXT NOT NULL DEFAULT 'Decoration',     -- Decoration | ResourceNode | ProductionFacility
  collision       TEXT NOT NULL DEFAULT 'full_blocking',  -- none | full_blocking | half_blocking
  size_x          INT  NOT NULL DEFAULT 1,
  size_y          INT  NOT NULL DEFAULT 1,
  -- ResourceNode fields
  action_id       TEXT REFERENCES action_definitions(id),
  required_skill  TEXT,
  required_level  INT,
  drop_item_id    TEXT,
  drop_quantity   INT  NOT NULL DEFAULT 1,
  respawn_ticks   INT  NOT NULL DEFAULT 25,
  -- ProductionFacility fields
  craft_action_id TEXT REFERENCES action_definitions(id),
  examine_text    TEXT
);

CREATE TABLE IF NOT EXISTS npc_definitions (
  id                 TEXT PRIMARY KEY,
  name               TEXT    NOT NULL,
  model_path         TEXT,
  size_x             INT     NOT NULL DEFAULT 1,
  size_y             INT     NOT NULL DEFAULT 1,
  is_attackable      BOOLEAN NOT NULL DEFAULT FALSE,
  max_hp             INT     NOT NULL DEFAULT 1,
  attack             INT     NOT NULL DEFAULT 0,
  strength           INT     NOT NULL DEFAULT 0,
  melee_defense      INT     NOT NULL DEFAULT 0,
  ranged_defense     INT     NOT NULL DEFAULT 0,
  attack_speed_ticks INT     NOT NULL DEFAULT 16,
  respawn_ticks      INT     NOT NULL DEFAULT 150,
  is_talkable        BOOLEAN NOT NULL DEFAULT FALSE,
  dialogue           TEXT,
  ai                 TEXT    NOT NULL DEFAULT 'static',  -- static | wander
  examine_text       TEXT
);

CREATE TABLE IF NOT EXISTS npc_drops (
  npc_id   TEXT  NOT NULL REFERENCES npc_definitions(id) ON DELETE CASCADE,
  item_id  TEXT  NOT NULL,
  quantity INT   NOT NULL DEFAULT 1,
  rate     FLOAT NOT NULL,
  PRIMARY KEY (npc_id, item_id)
);

CREATE TABLE IF NOT EXISTS item_definitions (
  id              TEXT    PRIMARY KEY,
  name            TEXT    NOT NULL,
  stackable       BOOLEAN NOT NULL DEFAULT FALSE,
  tradable        BOOLEAN NOT NULL DEFAULT TRUE,
  value           INT     NOT NULL DEFAULT 0,
  examine_text    TEXT,
  item_type       TEXT    NOT NULL DEFAULT 'resource',  -- resource | equipment | food
  -- Equipment
  equip_slot      TEXT,    -- head|body|legs|feet|hands|neck|ring|leftHand|rightHand|ammo
  two_handed      BOOLEAN NOT NULL DEFAULT FALSE,
  melee_attack    INT     NOT NULL DEFAULT 0,
  melee_strength  INT     NOT NULL DEFAULT 0,
  melee_defense   INT     NOT NULL DEFAULT 0,
  ranged_attack   INT     NOT NULL DEFAULT 0,
  ranged_strength INT     NOT NULL DEFAULT 0,
  ranged_defense  INT     NOT NULL DEFAULT 0,
  required_skill  TEXT,
  required_level  INT,
  tool_type       TEXT,   -- axe | pickaxe
  combat_style    TEXT,   -- melee | gunner
  -- Food
  heal_amount     INT,
  -- Assets
  sprite_path     TEXT,
  model_dropped   TEXT,
  model_equipped  TEXT
);

-- ---- Seed data (idempotent) ------------------------------------------------

INSERT INTO action_definitions (id, display_name, handler_type) VALUES
  ('chop',    'Chop',    'gather_resource'),
  ('mine',    'Mine',    'gather_resource'),
  ('fish',    'Fish',    'gather_resource'),
  ('harvest', 'Harvest', 'gather_resource'),
  ('smith',   'Smith',   'production_facility'),
  ('cook',    'Cook',    'production_facility'),
  ('equip',   'Equip',   'equip'),
  ('eat',     'Eat',     'eat'),
  ('talk',    'Talk-to', 'talk'),
  ('bank',    'Bank',    'bank'),
  ('examine', 'Examine', 'examine')
ON CONFLICT (id) DO NOTHING;

INSERT INTO object_definitions (id, name, object_type, collision, action_id, required_skill, required_level, drop_item_id, drop_quantity, respawn_ticks, examine_text) VALUES
  ('tree',         'Tree',         'ResourceNode',       'full_blocking', 'chop', 'woodcutting', 1,    'logs',       1, 25,  'A sturdy tree.'),
  ('rock',         'Rock',         'ResourceNode',       'full_blocking', 'mine', 'mining',      1,    'copper_ore', 1, 50,  'A rocky outcrop.'),
  ('chest',        'Chest',        'Decoration',         'full_blocking', 'bank', NULL,           NULL, NULL,         1, 0,   'A secure bank chest.'),
  ('fishing_spot', 'Fishing Spot', 'ResourceNode',       'none',          'fish', NULL,           1,    'raw_shrimp', 1, 10,  'A calm fishing spot.'),
  ('fence',        'Fence',        'Decoration',         'half_blocking', NULL,   NULL,           NULL, NULL,         1, 0,   'A wooden fence.')
ON CONFLICT (id) DO NOTHING;

INSERT INTO npc_definitions (id, name, size_x, size_y, is_attackable, max_hp, attack, strength, melee_defense, ranged_defense, attack_speed_ticks, respawn_ticks, is_talkable, ai, examine_text) VALUES
  ('chicken',    'Chicken',    1, 1, TRUE,  3,  1, 1, 1, 0,  16, 150, FALSE, 'wander', 'It''s a chicken.'),
  ('shopkeeper', 'Shopkeeper', 1, 1, FALSE, 10, 0, 0, 0, 0,  0,  0,   TRUE,  'static', 'This is a friendly shopkeeper.')
ON CONFLICT (id) DO NOTHING;

INSERT INTO npc_drops (npc_id, item_id, quantity, rate) VALUES
  ('chicken', 'egg', 1, 1.0)
ON CONFLICT (npc_id, item_id) DO NOTHING;

INSERT INTO item_definitions (id, name, stackable, tradable, value, item_type, equip_slot, two_handed, melee_attack, melee_strength, melee_defense, ranged_attack, ranged_strength, ranged_defense, required_skill, required_level, tool_type, combat_style, examine_text) VALUES
  ('coins',           'Coins',           TRUE,  TRUE,  1,   'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('logs',            'Logs',            FALSE, TRUE,  10,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('oak_logs',        'Oak logs',        FALSE, TRUE,  25,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('willow_logs',     'Willow logs',     FALSE, TRUE,  40,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('copper_ore',      'Copper ore',      FALSE, TRUE,  8,   'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('tin_ore',         'Tin ore',         FALSE, TRUE,  8,   'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('iron_ore',        'Iron ore',        FALSE, TRUE,  30,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('bronze_bar',      'Bronze bar',      FALSE, TRUE,  20,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('iron_bar',        'Iron bar',        FALSE, TRUE,  55,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('raw_shrimp',      'Raw shrimp',      FALSE, TRUE,  5,   'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('shrimp',          'Shrimp',          FALSE, TRUE,  10,  'food',      NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('raw_trout',       'Raw trout',       FALSE, TRUE,  15,  'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('trout',           'Trout',           FALSE, TRUE,  30,  'food',      NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('tinderbox',       'Tinderbox',       FALSE, TRUE,  1,   'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('egg',             'Egg',             FALSE, TRUE,  2,   'resource',  NULL,         FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('arrow',           'Bronze arrow',    TRUE,  TRUE,  1,   'equipment', 'ammo',       FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('axe',             'Bronze axe',      FALSE, TRUE,  16,  'equipment', 'rightHand',  FALSE, 4,  0, 0, 0, 0, 0, 'woodcutting', 1,    'axe',     NULL,     NULL),
  ('iron_axe',        'Iron axe',        FALSE, TRUE,  8,   'equipment', 'rightHand',  FALSE, -1, 0, 2, 0, 0, 0, 'woodcutting', 1,    'axe',     NULL,     NULL),
  ('pickaxe',         'Iron Pickaxe',    FALSE, TRUE,  25,  'equipment', 'rightHand',  FALSE, 6,  0, 1, 0, 0, 0, 'mining',      1,    'pickaxe', NULL,     'A sturdy iron pickaxe, good for mining.'),
  ('bronze_sword',    'Bronze sword',    FALSE, TRUE,  40,  'equipment', 'rightHand',  FALSE, 6,  3, 0, 0, 0, 0, NULL,          NULL, NULL,      'melee',  NULL),
  ('iron_sword',      'Iron sword',      FALSE, TRUE,  120, 'equipment', 'rightHand',  FALSE, 10, 5, 0, 0, 0, 0, NULL,          NULL, NULL,      'melee',  NULL),
  ('bronze_shield',   'Bronze shield',   FALSE, TRUE,  30,  'equipment', 'leftHand',   FALSE, 0,  0, 5, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('leather_helm',    'Leather helm',    FALSE, TRUE,  20,  'equipment', 'head',       FALSE, 0,  0, 1, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('bronze_helm',     'Bronze helm',     FALSE, TRUE,  80,  'equipment', 'head',       FALSE, 0,  0, 3, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('leather_body',    'Leather body',    FALSE, TRUE,  30,  'equipment', 'body',       FALSE, 0,  0, 2, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('leather_legs',    'Leather legs',    FALSE, TRUE,  25,  'equipment', 'legs',       FALSE, 0,  0, 1, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('leather_gloves',  'Leather gloves',  FALSE, TRUE,  10,  'equipment', 'hands',      FALSE, 0,  0, 1, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('leather_boots',   'Leather boots',   FALSE, TRUE,  15,  'equipment', 'feet',       FALSE, 0,  0, 1, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('gold_ring',       'Gold ring',       FALSE, TRUE,  50,  'equipment', 'ring',       FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('amulet',          'Amulet of str',   FALSE, TRUE,  200, 'equipment', 'neck',       FALSE, 0,  4, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('bronze_longsword','Bronze longsword',FALSE, TRUE,  60,  'equipment', 'rightHand',  FALSE, 8,  7, 0, 0, 0, 0, NULL,          NULL, NULL,      'melee',  'Careful not to poke your eye out!'),
  ('kinetic_charges', 'Kinetic Charges', TRUE,  TRUE,  1,   'equipment', 'ammo',       FALSE, 0,  0, 0, 0, 0, 0, NULL,          NULL, NULL,      NULL,     NULL),
  ('basic_chaingun',  'Basic Chaingun',  FALSE, TRUE,  200, 'equipment', 'rightHand',  TRUE,  0,  0, 0, 8, 4, 0, NULL,          NULL, NULL,      'gunner', 'A heavy two-handed energy weapon.')
ON CONFLICT (id) DO NOTHING;
