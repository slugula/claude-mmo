## Overview

- NPCs are entities not directly controlled by the player.
- NPCs have a name, as well as basic combat stats for attacking and defending.
- NPCs have a flag for whether they can be attacked or not.
- An NPC who can be attacked is considered a "Monster". The primary action for these NPCs is "Attack".
- An NPC who cannot be attacked is considered an "NPC". The primary action for these NPCs is "Talk to".
- NPCs can have different AI behaviors controlling how they move.
## Data

NPC definitions are stored in `src/data/npcs.json` — adding a new NPC requires only a JSON entry, no code changes. `NPCRegistry.ts` is a thin wrapper that loads and indexes the JSON at startup.

## Data Model

| Field | Description |
|---|---|
| `kind` | Unique string identifier (used as the NPC type key) |
| `name` | Player-facing display name |
| `size` | Tile footprint (e.g. 1 = 1×1 tile) |
| `isAttackable` | If true, the NPC can be attacked; primary action is "Attack" |
| `ai` | AI behavior: `static` or `wander` |
| `uniqueActions` | List of actions beyond the defaults ("Walk here", "Examine", "Cancel") |
| `examine` | Text logged to chat when the player examines the NPC |
| `maxHp` | Maximum hit points |
| `attack` | Attack stat (affects hit accuracy) |
| `defense` | Defence stat (reduces incoming hit chance) |
| `attackSpeedTicks` | How many ticks between each attack |
| `respawnTicks` | Ticks after death before the NPC respawns (attackable NPCs only) |
| `drops` | Array of drop entries: `{ itemId, quantity, rate }` |

## AI Behaviors

- **Static** — NPC does not move from its initial spawn position
- **Wander** — moves 2–5 tiles toward a random point within 8 tiles of its home, waits 20–80 ticks, then repeats

## Current NPCs

See `docs/Database/NPCs.md` for the full NPC table.
