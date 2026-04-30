import { Pool } from 'pg';

export const pool = new Pool({
  connectionString: process.env.DATABASE_URL ?? 'postgresql://postgres:dev@localhost:5432/mmo',
});

pool.on('error', (err) => {
  console.error('[db] Unexpected pool error:', err.message);
});
