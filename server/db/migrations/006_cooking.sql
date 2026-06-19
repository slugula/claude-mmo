-- 006_cooking.sql — Cooking skill + generic production/recipe system.
-- Idempotent: safe to run against an existing DB. Adds the recipe table, the
-- two cooking facilities, the shrimp cooking-chain items, the Cooking skill,
-- the 'prepare' action, and the two cooking recipes.

CREATE TABLE IF NOT EXISTS recipe_definitions (
  id             TEXT  PRIMARY KEY,
  facility_id    TEXT  NOT NULL,
  skill          TEXT  NOT NULL,
  required_level INT   NOT NULL DEFAULT 1,
  xp             FLOAT NOT NULL DEFAULT 0,
  input_item_id  TEXT  NOT NULL,
  input_qty      INT   NOT NULL DEFAULT 1,
  output_item_id TEXT  NOT NULL,
  output_qty     INT   NOT NULL DEFAULT 1,
  fail_item_id   TEXT,
  no_fail_level  INT   NOT NULL DEFAULT 99
);

INSERT INTO skill_definitions (id, name, icon_path, sort_order) VALUES
  ('cooking', 'Cooking', NULL, 7)
ON CONFLICT (id) DO NOTHING;

INSERT INTO action_definitions (id, display_name, handler_type) VALUES
  ('prepare', 'Prepare', 'production_facility')
ON CONFLICT (id) DO NOTHING;

INSERT INTO object_definitions (id, name, object_type, collision, craft_action_id, examine_text) VALUES
  ('prep_table',    'Preparation Table', 'ProductionFacility', 'full_blocking', 'prepare', 'A table for preparing raw ingredients.'),
  ('cooking_range', 'Cooking Range',     'ProductionFacility', 'full_blocking', 'cook',    'A hot range for cooking food.')
ON CONFLICT (id) DO NOTHING;

INSERT INTO item_definitions (id, name, stackable, tradable, value, item_type, heal_amount, examine_text) VALUES
  ('prepared_shrimp', 'Prepared shrimp', FALSE, TRUE, 6,  'resource', NULL, 'Raw shrimp, prepped and ready to cook.'),
  ('cooked_shrimp',   'Cooked shrimp',   FALSE, TRUE, 12, 'food',     3,    'Some nicely cooked shrimp.'),
  ('burnt_shrimp',    'Burnt shrimp',    FALSE, TRUE, 1,  'resource', NULL, 'Oops. Charred beyond edible.')
ON CONFLICT (id) DO NOTHING;

INSERT INTO recipe_definitions (id, facility_id, skill, required_level, xp, input_item_id, input_qty, output_item_id, output_qty, fail_item_id, no_fail_level) VALUES
  ('prepare_shrimp', 'prep_table',    'cooking', 1, 5,  'raw_shrimp',      1, 'prepared_shrimp', 1, NULL,           1),
  ('cook_shrimp',    'cooking_range', 'cooking', 1, 30, 'prepared_shrimp', 1, 'cooked_shrimp',   1, 'burnt_shrimp', 20)
ON CONFLICT (id) DO NOTHING;
