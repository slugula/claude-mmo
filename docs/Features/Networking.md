## Overview

The game uses a **server-authoritative WebSocket architecture**. All game logic — tick processing, NPC AI, combat, movement — runs exclusively on the Node.js server. Clients send inputs and render whatever the server sends back. There is no client-side prediction.

This matches OSRS's own model but improves on it with a 3× faster tick rate (200ms vs OSRS's 600ms), making server-authoritative movement feel nearly instant even without prediction.

## How to Run

```
npm run server   # Node.js WS server on :8080
npm run dev      # Vite client on :5173
```

Open multiple browser tabs to `localhost:5173` to test multiplayer. Each tab is an independent player.

## Architecture

```
Browser A  ──actions──▶  Node.js WS Server (:8080)  ◀──actions──  Browser B
           ◀──state ──   (tick loop, world, NPCs,    ──state ──▶
                          all player state, combat)
```

The **server owns**: tick loop, world state, all player states, NPC AI, combat resolution, item drops.

The **client owns**: rendering, input capture, UI, sound.

## Tick Loop

- `server/GameLoop.ts` runs `setInterval(tick, 200ms)` on the server
- Each tick drains all pending player actions, runs them through the pure `processTick()` function, and broadcasts the resulting state to all connected clients
- `processTick()` in `src/engine/TickSystem.ts` is a pure function: `(GameState, Map<playerId, GameAction[]>) → GameState`

## Wire Protocol

**Server → Client**

```jsonc
// Sent once on connection
{ "type": "init", "playerId": "player-1-1234567890", "worldSeed": 42 }

// Sent every tick (200ms) to all clients
{ "type": "state", "tick": 42, "players": {...}, "npcs": [...],
  "droppedItems": [...], "pendingRespawns": [...], "messages": {...} }
```

**Client → Server**

```jsonc
// Sent immediately on each player action (no batching)
{ "type": "actions", "actions": [...] }
```

## World Synchronisation

The world (64×64 tile grid, obstacle placement) is **deterministic** from seed 42. Both the server and every client generate it independently on startup using the same seeded RNG in `src/world/WorldState.ts`. The world is never transmitted after the initial `init` message — only dynamic state (players, NPCs, items) is broadcast each tick.

## Multi-Player State Shape

```ts
GameState.players: Record<string, PlayerState>  // keyed by playerId
GameState.messages: Record<string, string[]>    // per-player chat/system messages produced each tick
```

`messages` uses a `"chat:"` prefix to distinguish player chat from system messages:
- `"chat:Player: hello"` → rendered white in ChatLog
- `"Your inventory is full."` → rendered yellow in ChatLog

## Player Lifecycle

- **Connect**: server generates a UUID player ID, spawns a `PlayerState` at the start tile, sends `init`
- **Disconnect**: player is removed from `GameState.players`; their avatar disappears for all other clients on the next state broadcast

## Key Files

| File | Role |
|---|---|
| `server/index.ts` | WebSocket server, connection/session management |
| `server/GameLoop.ts` | Authoritative tick loop, state broadcast, player spawn/remove |
| `src/engine/TickSystem.ts` | Pure `processTick()` function — shared logic used by the server |
| `src/engine/NetworkClient.ts` | Browser-side WS client wrapper |
| `src/engine/GameEngine.ts` | Wires NetworkClient to renderer; updates state on each broadcast |
| `src/world/WorldState.ts` | Pure world generation (no Babylon dependency) — used by both server and client |
