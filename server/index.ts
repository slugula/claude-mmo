import { WebSocketServer, WebSocket } from 'ws';
import { GameLoop } from './GameLoop';
import type { GameAction, ServerStatePatch } from '../src/shared/types';

const PORT = 8080;

interface ClientMessage {
  type: 'actions';
  actions: GameAction[];
  playerName?: string;
}

const clients = new Map<string, WebSocket>();
let playerCounter = 0;

function broadcast(patch: ServerStatePatch): void {
  const data = JSON.stringify({ type: 'state', ...patch });
  for (const ws of clients.values()) {
    if (ws.readyState === WebSocket.OPEN) ws.send(data);
  }
}

const loop = new GameLoop(broadcast);
loop.start();

const wss = new WebSocketServer({ port: PORT });

wss.on('listening', () => {
  console.log(`[server] Listening on :${PORT}`);
});

wss.on('connection', (ws) => {
  playerCounter++;
  const playerId = `player-${playerCounter}-${Date.now()}`;
  const defaultName = `Player${playerCounter}`;

  clients.set(playerId, ws);
  loop.addPlayer(playerId, defaultName);

  ws.send(JSON.stringify({
    type: 'init',
    playerId,
    worldSeed: loop.getWorldSeed(),
  }));

  console.log(`[server] Player connected: ${playerId}`);

  ws.on('message', (raw) => {
    let msg: ClientMessage;
    try {
      msg = JSON.parse(raw.toString()) as ClientMessage;
    } catch {
      return;
    }

    if (msg.type === 'actions' && Array.isArray(msg.actions)) {
      loop.enqueueActions(playerId, msg.actions);
    }
  });

  ws.on('close', () => {
    clients.delete(playerId);
    loop.removePlayer(playerId);
    console.log(`[server] Player disconnected: ${playerId}`);
  });

  ws.on('error', (err) => {
    console.error(`[server] WS error for ${playerId}:`, err.message);
  });
});
