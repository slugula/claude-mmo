import express from 'express';
import bcrypt from 'bcryptjs';
import jwt from 'jsonwebtoken';
import { PlayerRepository } from '../db/PlayerRepository';

export const JWT_SECRET = process.env.JWT_SECRET ?? 'dev-secret-change-in-production';

export interface JwtPayload {
  sub: string;    // player UUID
  username: string;
}

export function verifyToken(token: string): JwtPayload | null {
  try {
    return jwt.verify(token, JWT_SECRET) as JwtPayload;
  } catch {
    return null;
  }
}

export const authRouter = express.Router();
authRouter.use(express.json());

authRouter.post('/register', async (req, res) => {
  // req.body is undefined when the request has no body or a non-JSON content
  // type (express.json leaves it unset); guard so destructuring can't throw an
  // uncaught TypeError and crash the handler.
  const { username, password } = (req.body ?? {}) as { username?: string; password?: string };

  if (!username || !password) {
    res.status(400).json({ error: 'username and password required' });
    return;
  }
  if (username.length < 3 || username.length > 20) {
    res.status(400).json({ error: 'username must be 3–20 characters' });
    return;
  }
  if (!/^[a-zA-Z0-9_]+$/.test(username)) {
    res.status(400).json({ error: 'username may only contain letters, numbers, and underscores' });
    return;
  }
  if (password.length < 6) {
    res.status(400).json({ error: 'password must be at least 6 characters' });
    return;
  }

  try {
    const existing = await PlayerRepository.findByUsername(username);
    if (existing) {
      res.status(409).json({ error: 'username already taken' });
      return;
    }
    const passwordHash = await bcrypt.hash(password, 10);
    const playerId = await PlayerRepository.create(username, passwordHash);
    const token = jwt.sign({ sub: playerId, username: username.toLowerCase() } satisfies JwtPayload, JWT_SECRET, { expiresIn: '7d' });
    res.json({ token, playerId, username: username.toLowerCase() });
  } catch (err) {
    console.error('[auth] Register error:', err);
    res.status(500).json({ error: 'internal error' });
  }
});

authRouter.post('/login', async (req, res) => {
  const { username, password } = (req.body ?? {}) as { username?: string; password?: string };

  if (!username || !password) {
    res.status(400).json({ error: 'username and password required' });
    return;
  }

  try {
    const account = await PlayerRepository.findByUsername(username);
    if (!account) {
      res.status(401).json({ error: 'invalid credentials' });
      return;
    }
    const ok = await bcrypt.compare(password, account.passwordHash);
    if (!ok) {
      res.status(401).json({ error: 'invalid credentials' });
      return;
    }
    await PlayerRepository.updateLastLogin(account.id);
    const token = jwt.sign({ sub: account.id, username: account.username } satisfies JwtPayload, JWT_SECRET, { expiresIn: '7d' });
    res.json({ token, playerId: account.id, username: account.username });
  } catch (err) {
    console.error('[auth] Login error:', err);
    res.status(500).json({ error: 'internal error' });
  }
});
