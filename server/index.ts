import { createServer, type IncomingMessage } from 'http';
import express from 'express';
import { WebSocketServer, WebSocket } from 'ws';
import { parse } from 'url';
import { GameLoop } from './GameLoop';
import { authRouter, verifyToken } from './auth/router';
import { PlayerRepository } from './db/PlayerRepository';
import type { GameAction, ServerStatePatch, RespawnEntry } from '../src/shared/types';

const PORT = Number(process.env.PORT ?? 8080);
const CHECKPOINT_INTERVAL_MS = 60_000;
const DEFAULT_VIEW_RADIUS = 15;
const MAX_VIEW_RADIUS = 25;

interface ClientActionsMessage {
  type: 'actions';
  actions: GameAction[];
}

interface ClientViewMessage {
  type: 'setViewRadius';
  radius: number;
}

type ClientMessage = ClientActionsMessage | ClientViewMessage;

// ---- HTTP server with Express ----

const app = express();

app.use((req, res, next) => {
  const origin = process.env.ALLOWED_ORIGIN ?? '*';
  res.setHeader('Access-Control-Allow-Origin', origin);
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization');
  if (req.method === 'OPTIONS') { res.sendStatus(204); return; }
  next();
});

app.use('/auth', authRouter);
app.get('/health', (_req, res) => res.json({ ok: true }));

const httpServer = createServer(app);

// ---- Per-player tracking ----

const clients = new Map<string, WebSocket>();
const playerViewRadius = new Map<string, number>();

// ---- Interest-filtered broadcast ----

function chebyshev(ax: number, ay: number, bx: number, by: number): number {
  return Math.max(Math.abs(ax - bx), Math.abs(ay - by));
}

function broadcast(patch: ServerStatePatch): void {
  for (const [playerId, ws] of clients.entries()) {
    if (ws.readyState !== WebSocket.OPEN) continue;

    const self = patch.players[playerId];
    if (!self) continue; // race: player not in state yet

    const cx = self.tileX;
    const cy = self.tileY;
    const r  = playerViewRadius.get(playerId) ?? DEFAULT_VIEW_RADIUS;

    // Filter visible entities by Chebyshev distance (square view area)
    const visiblePlayers: ServerStatePatch['players'] = {};
    for (const [id, p] of Object.entries(patch.players)) {
      if (chebyshev(p.tileX, p.tileY, cx, cy) <= r) {
        visiblePlayers[id] = p;
      }
    }

    const visibleNPCs = patch.npcs.filter(n => chebyshev(n.tileX, n.tileY, cx, cy) <= r);

    const visibleItems = patch.droppedItems.filter(i => chebyshev(i.tileX, i.tileY, cx, cy) <= r);

    const visibleRespawns: RespawnEntry[] = patch.pendingRespawns.filter(e =>
      chebyshev(e.homeX, e.homeY, cx, cy) <= r,
    );

    const visibleDepletedTrees: Record<string, number> = {};
    const visibleTreeHealth: Record<string, number>    = {};
    for (const [key, val] of Object.entries(patch.depletedTrees)) {
      const dash = key.lastIndexOf('-');
      const tx = parseInt(key.slice(0, dash), 10);
      const ty = parseInt(key.slice(dash + 1), 10);
      if (chebyshev(tx, ty, cx, cy) <= r) visibleDepletedTrees[key] = val;
    }
    for (const [key, val] of Object.entries(patch.treeHealth)) {
      const dash = key.lastIndexOf('-');
      const tx = parseInt(key.slice(0, dash), 10);
      const ty = parseInt(key.slice(dash + 1), 10);
      if (chebyshev(tx, ty, cx, cy) <= r) visibleTreeHealth[key] = val;
    }

    const filteredPatch: ServerStatePatch = {
      tick:           patch.tick,
      players:        visiblePlayers,
      npcs:           visibleNPCs,
      droppedItems:   visibleItems,
      pendingRespawns: visibleRespawns,
      messages:       { [playerId]: patch.messages[playerId] ?? [] },
      depletedTrees:  visibleDepletedTrees,
      treeHealth:     visibleTreeHealth,
    };

    ws.send(JSON.stringify({ type: 'state', ...filteredPatch }));
  }
}

// ---- Game loop ----

const loop = new GameLoop(broadcast);
loop.start();

const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', async (ws: WebSocket, req: IncomingMessage) => {
  const { query } = parse(req.url ?? '', true);
  const token = typeof query.token === 'string' ? query.token : null;

  if (!token) {
    ws.close(4001, 'Authentication required');
    return;
  }

  const payload = verifyToken(token);
  if (!payload) {
    ws.close(4001, 'Invalid or expired token');
    return;
  }

  const { sub: playerId, username } = payload;

  if (clients.has(playerId)) {
    ws.close(4002, 'Already connected on another session');
    return;
  }

  let savedState = null;
  try {
    const account = await PlayerRepository.findByUsername(username);
    savedState = account?.playerState ?? null;
  } catch (err) {
    console.error('[server] Failed to load player state for', username, err);
  }

  clients.set(playerId, ws);
  playerViewRadius.set(playerId, DEFAULT_VIEW_RADIUS);

  const isNewPlayer = savedState === null;
  loop.addPlayer(playerId, username, savedState ?? undefined);

  ws.send(JSON.stringify({
    type: 'init',
    playerId,
    tiles:         loop.getWorldTiles(),
    vertexHeights: loop.getVertexHeights(),
    isNewPlayer,
  }));

  console.log(`[server] ${username} (${playerId}) connected — ${isNewPlayer ? 'new' : 'returning'} player`);

  ws.on('message', (raw) => {
    let msg: ClientMessage;
    try {
      msg = JSON.parse(raw.toString()) as ClientMessage;
    } catch {
      return;
    }

    if (msg.type === 'actions' && Array.isArray(msg.actions)) {
      loop.enqueueActions(playerId, msg.actions);
    } else if (msg.type === 'setViewRadius') {
      const r = Math.min(Math.max(1, Math.round(msg.radius)), MAX_VIEW_RADIUS);
      playerViewRadius.set(playerId, r);
    }
  });

  ws.on('close', async () => {
    clients.delete(playerId);
    playerViewRadius.delete(playerId);
    const state = loop.removePlayer(playerId);
    if (state) {
      try {
        await PlayerRepository.save(playerId, state);
      } catch (err) {
        console.error(`[server] Failed to save state for ${username}:`, err);
      }
    }
    console.log(`[server] ${username} disconnected`);
  });

  ws.on('error', (err) => {
    console.error(`[server] WS error for ${username}:`, err.message);
  });
});

// ---- Periodic checkpoint ----

setInterval(async () => {
  const states = loop.getPlayerStates();
  const saves = [...states.entries()].map(([id, state]) =>
    PlayerRepository.save(id, state).catch((err) => {
      console.error(`[server] Checkpoint save failed for ${id}:`, err);
    }),
  );
  await Promise.allSettled(saves);
  if (saves.length > 0) {
    console.log(`[server] Checkpoint saved ${saves.length} player(s)`);
  }
}, CHECKPOINT_INTERVAL_MS);

httpServer.listen(PORT, () => {
  console.log(`[server] Listening on :${PORT}`);
});
