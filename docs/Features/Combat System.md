## Overview

Combat is turn-based and tick-driven — one attack attempt per combatant per tick (200ms). The player initiates combat by clicking an attackable NPC. Combat continues automatically until either the player or NPC dies, or the player cancels by moving away.

## Initiating Combat

- Left-click or select "Attack" from the right-click menu on an attackable NPC
- The player pathfinds toward the target; combat begins once within 1 tile (melee range)
- Walking away (issuing a MOVE_TO action) cancels combat

## Attack Resolution (per tick)

1. Check if attacker is within melee range (1 tile)
2. Roll hit/miss using attacker's Attack stat vs defender's Defence stat
3. On a hit: roll damage between 1 and max hit (derived from Strength)
4. On a miss: deal 0 damage (a 0-damage hit still triggers hit splats and health bars)
5. Both the player and the targeted NPC attack on the same tick if both are in range

## Stats

- **Attack** — determines hit accuracy
- **Strength** — determines max hit
- **Defence** — reduces incoming hit chance
- **Hitpoints** — current and maximum HP; death occurs at 0

Combat bonuses from equipped items (attack bonus, strength bonus, defence bonus) stack additively and modify the base stats.

## Death

**NPC death:**
- NPC is removed from the world
- Loot is dropped at the NPC's tile (current drop rate: 100%)
- A respawn entry is queued (see: Respawn System)

**Player death:** not yet implemented.

## Hit Splats

- A circular damage number appears above the target immediately when a hit is registered
- Blue circle = 0 damage (miss); red circle = damage dealt
- The splat holds in place for 0.5 seconds, then floats upward and fades over 1 second
- See: `UI/Hit Splat.md`

## Health Bars

- A green/red HP bar appears above an NPC after it receives its first hit
- Disappears after 5 seconds of no new hits
- Player HP is shown in the Skills tab as `current/max`
- See: `UI/Health Bar.md`

## Respawn System

- Each attackable NPC definition includes a `respawnTicks` value
- Chicken respawn time: 150 ticks (30 seconds)
- After death, the NPC is added to `GameState.pendingRespawns` with a target tick
- When `currentTick >= respawnAtTick`, the NPC is spawned at its home tile (or nearest walkable tile)
