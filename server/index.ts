import { createServer, type IncomingMessage } from 'http';
import express from 'express';
import { WebSocketServer, WebSocket } from 'ws';
import { parse } from 'url';
import { GameLoop } from './GameLoop';
import { authRouter, verifyToken } from './auth/router';
import { entityRouter } from './db/EntityRouter';
import { getClientDefs } from './db/EntityLoader';
import { PlayerRepository } from './db/PlayerRepository';
import { pool } from './db/client';
import type { GameAction, ServerStatePatch, RespawnEntry } from '../src/shared/types';
import { CHAT_RADIUS } from '../src/shared/constants';

const PORT = Number(process.env.PORT ?? 8080);
const CHECKPOINT_INTERVAL_MS = 60_000;
const DEFAULT_VIEW_RADIUS = 15;
// Patch size grows ~quadratically with radius; the periodic patch-size log
// below is the guard rail — watch it before raising this further.
const MAX_VIEW_RADIUS = 48;
// Log interest-patch bandwidth every N ticks (~30s at 200ms ticks).
const PATCH_LOG_INTERVAL_TICKS = 150;

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

app.use(express.json());
app.use('/auth', authRouter);
app.use('/api/db', entityRouter);
app.get('/health', (_req, res) => res.json({ ok: true }));

const httpServer = createServer(app);

// ---- Per-player tracking ----

const clients = new Map<string, WebSocket>();
const playerViewRadius = new Map<string, number>();

// ---- Interest-filtered broadcast ----

function chebyshev(ax: number, ay: number, bx: number, by: number): number {
  return Math.max(Math.abs(ax - bx), Math.abs(ay - by));
}

// Rolling patch-size telemetry: how many bytes each tick's interest-filtered
// patches cost, summed over all clients. Logged every PATCH_LOG_INTERVAL_TICKS
// so raising view radii has visible bandwidth numbers behind it.
let patchBytesAccum = 0;
let patchBytesMax = 0;
let patchTicks = 0;

function broadcast(patch: ServerStatePatch): void {
  let tickBytes = 0;
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
      const dist = chebyshev(p.tileX, p.tileY, cx, cy);
      if (dist > r) continue;
      // Chat is scoped tighter than visibility: blank chatMessage for players
      // beyond CHAT_RADIUS so this recipient sees them but not their chat
      // (client treats empty chatMessage as no bubble / no log entry).
      visiblePlayers[id] = (id !== playerId && dist > CHAT_RADIUS && p.chatMessage !== '')
        ? { ...p, chatMessage: '' }
        : p;
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

    const visibleDepletedRocks: Record<string, number> = {};
    const visibleRockHealth: Record<string, number>    = {};
    for (const [key, val] of Object.entries(patch.depletedRocks)) {
      const dash = key.lastIndexOf('-');
      const tx = parseInt(key.slice(0, dash), 10);
      const ty = parseInt(key.slice(dash + 1), 10);
      if (chebyshev(tx, ty, cx, cy) <= r) visibleDepletedRocks[key] = val;
    }
    for (const [key, val] of Object.entries(patch.rockHealth)) {
      const dash = key.lastIndexOf('-');
      const tx = parseInt(key.slice(0, dash), 10);
      const ty = parseInt(key.slice(dash + 1), 10);
      if (chebyshev(tx, ty, cx, cy) <= r) visibleRockHealth[key] = val;
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
      depletedRocks:  visibleDepletedRocks,
      rockHealth:     visibleRockHealth,
    };

    const payload = JSON.stringify({ type: 'state', ...filteredPatch });
    tickBytes += payload.length;
    ws.send(payload);
  }

  if (clients.size > 0) {
    patchBytesAccum += tickBytes;
    patchBytesMax = Math.max(patchBytesMax, tickBytes);
    if (++patchTicks >= PATCH_LOG_INTERVAL_TICKS) {
      const avg = Math.round(patchBytesAccum / patchTicks);
      console.log(`[server] patch bandwidth: avg ${avg} B/tick, peak ${patchBytesMax} B/tick ` +
                  `across ${clients.size} client(s) (~${Math.round(avg * 5 / 1024)} KB/s total)`);
      patchBytesAccum = 0; patchBytesMax = 0; patchTicks = 0;
    }
  }
}

// ---- Game loop ----

// Surface async failures instead of letting them crash silently or wedge the
// process. We log (with stack) and keep running — a single bad tick/handler
// shouldn't take the whole dev server down without a trace.
process.on('uncaughtException', (err) => {
  console.error('[server] uncaughtException:', err);
});
process.on('unhandledRejection', (reason) => {
  console.error('[server] unhandledRejection:', reason);
});

const loop = new GameLoop(broadcast);
loop.start();

const wss = new WebSocketServer({ server: httpServer });

wss.on('connection', async (ws: WebSocket, req: IncomingMessage) => {
  const { query } = parse(req.url ?? '', true);
  // Accept token from Authorization header (production WSS path) or URL query param (dev fallback)
  const authHeader = req.headers['authorization'];
  const token = (authHeader?.startsWith('Bearer ') ? authHeader.slice(7) : null)
             ?? (typeof query.token === 'string' ? query.token : null);

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

  const defs = getClientDefs();
  ws.send(JSON.stringify({
    type: 'init',
    waterTiles:    loop.getWaterTiles(),
    overlayTiles:  loop.getOverlayTiles(),
    walls:         loop.getWalls(),
    playerId,
    tiles:         loop.getWorldTiles(),
    vertexHeights: loop.getVertexHeights(),
    isNewPlayer,
    // Entity definitions (raw DB rows, incl. client-only sprite/model fields) so
    // a shared client renders authored content without localhost DB access.
    items:   defs.items,
    objects: defs.objects,
    npcs:    defs.npcs,
    actions: defs.actions,
    skills:  defs.skills,
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

// ---- Graceful shutdown -----------------------------------------------------
// Release everything that keeps the event loop alive (the 200ms tick interval,
// the HTTP/WS servers + open client sockets, the pg pool) so Ctrl+C exits the
// process promptly instead of hanging the terminal. A second Ctrl+C force-quits,
// and a backstop timer guarantees exit even if a close() callback never fires.
let shuttingDown = false;
function shutdown(signal: string): void {
  if (shuttingDown) { process.exit(0); }   // second signal → hard exit
  shuttingDown = true;
  console.log(`\n[server] ${signal} received — shutting down...`);
  try { loop.stop(); } catch { /* ignore */ }            // clear the tick interval
  for (const client of wss.clients) client.terminate();  // drop open WS sockets
  wss.close();
  httpServer.close(() => {
    pool.end().then(() => process.exit(0)).catch(() => process.exit(0));
  });
  // Backstop: if a handle refuses to close, exit anyway. unref() so this timer
  // alone never keeps the process alive.
  setTimeout(() => process.exit(0), 1500).unref();
}
process.on('SIGINT',  () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));
