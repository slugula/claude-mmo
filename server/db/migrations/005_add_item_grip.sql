-- Migration 005: held-weapon grip transform on item_definitions.
-- Controls how an item's model_equipped sits in the player's hand socket
-- (position/rotation/scale + attach joint). Values are RELATIVE to the current
-- player model's hand bone, authored in the editor's Items tab.
--
-- Run once on prod before restarting the server:
--   psql $DATABASE_URL -f server/db/migrations/005_add_item_grip.sql

ALTER TABLE item_definitions
  ADD COLUMN IF NOT EXISTS grip_joint  TEXT,
  ADD COLUMN IF NOT EXISTS grip_pos_x  REAL NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS grip_pos_y  REAL NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS grip_pos_z  REAL NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS grip_rot_x  REAL NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS grip_rot_y  REAL NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS grip_rot_z  REAL NOT NULL DEFAULT 0,
  ADD COLUMN IF NOT EXISTS grip_scale  REAL NOT NULL DEFAULT 1;
