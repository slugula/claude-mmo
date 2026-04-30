import { pool } from './client';
import type { PlayerState } from '../../src/shared/types';

interface PlayerRow {
  id: string;
  username: string;
  password_hash: string;
  player_state: PlayerState | null;
}

export interface AccountRecord {
  id: string;
  username: string;
  passwordHash: string;
  playerState: PlayerState | null;
}

export const PlayerRepository = {
  async findByUsername(username: string): Promise<AccountRecord | null> {
    const res = await pool.query<PlayerRow>(
      'SELECT id, username, password_hash, player_state FROM players WHERE username = $1',
      [username.toLowerCase()],
    );
    if (res.rows.length === 0) return null;
    const row = res.rows[0];
    return {
      id: row.id,
      username: row.username,
      passwordHash: row.password_hash,
      playerState: row.player_state ?? null,
    };
  },

  async create(username: string, passwordHash: string): Promise<string> {
    const res = await pool.query<{ id: string }>(
      'INSERT INTO players (username, password_hash) VALUES ($1, $2) RETURNING id',
      [username.toLowerCase(), passwordHash],
    );
    return res.rows[0].id;
  },

  async save(playerId: string, state: PlayerState): Promise<void> {
    await pool.query(
      'UPDATE players SET player_state = $1 WHERE id = $2',
      [JSON.stringify(state), playerId],
    );
  },

  async updateLastLogin(playerId: string): Promise<void> {
    await pool.query(
      'UPDATE players SET last_login = now() WHERE id = $1',
      [playerId],
    );
  },
};
