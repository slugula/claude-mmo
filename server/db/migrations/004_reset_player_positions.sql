-- Migration 004: Reset all player positions to the new spawn point (31, 32).
-- Required because the world map has changed and the old spawn (32, 32) may now
-- land on a non-walkable tile (cliff, wall, water, etc.).
-- Also clears any in-progress pathing or combat state that would be stale.
--
-- Run once on prod before restarting the server with the new world map:
--   psql $DATABASE_URL -f server/db/migrations/004_reset_player_positions.sql

UPDATE players
SET player_state = player_state
  || jsonb_build_object(
      'tileX',         31,
      'tileY',         32,
      'destinationX',  31,
      'destinationY',  32,
      'path',          '[]'::jsonb,
      'attackTargetId', null,
      'talkTargetId',   null,
      'pickupItemId',   null,
      'chopTargetX',    null,
      'chopTargetY',    null
    )
WHERE player_state IS NOT NULL;
