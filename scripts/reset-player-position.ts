#!/usr/bin/env tsx
//
// One-off script to move a player back to a walkable tile on the current map.
//
// Why this exists: the legacy PLAYER_START_X/Y constants are (128, 128) but
// the actual worldMap.json is 64x64, so new accounts can spawn out of bounds.
// The server's pathfinder then refuses to route their MOVE_TO actions and
// the player is stuck. This script edits the player_state JSONB directly to
// snap them to a known-good tile.
//
// Usage:
//   tsx scripts/reset-player-position.ts <username> [tileX] [tileY]
//
// Defaults: (32, 32) — center of a 64x64 map.
//
// IMPORTANT: stop or disconnect the server first, otherwise its periodic
// checkpoint save will overwrite our update with the old position.

import { pool } from '../server/db/client';

async function main(): Promise<void> {
  const [, , usernameArg, xArg, yArg] = process.argv;
  if (!usernameArg) {
    console.error('Usage: tsx scripts/reset-player-position.ts <username> [tileX] [tileY]');
    process.exit(1);
  }
  const username = usernameArg.toLowerCase();
  const tileX    = xArg ? parseInt(xArg, 10) : 32;
  const tileY    = yArg ? parseInt(yArg, 10) : 32;
  if (!Number.isFinite(tileX) || !Number.isFinite(tileY)) {
    console.error('tileX and tileY must be integers');
    process.exit(1);
  }

  // jsonb_set / jsonb_build_object on the player_state column. We also clear
  // any pending path and pin the destination to the new position so the
  // server doesn't try to continue walking somewhere stale.
  const sql = `
    UPDATE players
    SET player_state = COALESCE(player_state, '{}'::jsonb) || jsonb_build_object(
      'tileX',        $2::int,
      'tileY',        $3::int,
      'destinationX', $2::int,
      'destinationY', $3::int,
      'path',         '[]'::jsonb
    )
    WHERE username = $1
    RETURNING
      username,
      (player_state ->> 'tileX')::int        AS x,
      (player_state ->> 'tileY')::int        AS y,
      (player_state ->> 'destinationX')::int AS dest_x,
      (player_state ->> 'destinationY')::int AS dest_y
  `;
  const res = await pool.query(sql, [username, tileX, tileY]);

  if (res.rows.length === 0) {
    console.error(`No player found with username '${username}'.`);
    process.exit(2);
  }
  const row = res.rows[0];
  console.log(`Reset ${row.username}: tile (${row.x}, ${row.y}), destination (${row.dest_x}, ${row.dest_y}).`);
  console.log('Restart the server (if it was running) or reconnect the client to pick up the change.');
}

main()
  .catch(err => { console.error(err); process.exit(3); })
  .finally(() => pool.end());
