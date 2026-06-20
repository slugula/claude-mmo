-- 007_tunables.sql — Global gameplay tunables (interaction tick rates).
-- Idempotent. Adds the game_config key/value table and seeds the interaction
-- speed knobs read by the gathering/production systems.

CREATE TABLE IF NOT EXISTS game_config (
  key      TEXT PRIMARY KEY,
  value    INT  NOT NULL,
  label    TEXT,
  category TEXT
);

INSERT INTO game_config (key, value, label, category) VALUES
  ('chop_interval',    12, 'Chop interval (ticks)',    'Interaction speed'),
  ('mine_interval',    12, 'Mine interval (ticks)',    'Interaction speed'),
  ('fish_interval',    12, 'Fish interval (ticks)',    'Interaction speed'),
  ('produce_interval',  3, 'Produce interval (ticks)', 'Interaction speed')
ON CONFLICT (key) DO NOTHING;
