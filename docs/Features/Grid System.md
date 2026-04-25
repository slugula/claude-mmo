## Overview

The game world is a discrete grid of tiles. All positions, movement, and collision are expressed in tile coordinates, not continuous world units.

## Tiles

- Each tile is a 1×1 unit square
- Tile coordinates map directly to 3D world coordinates: tile `(x, y)` → world position `(x, 0, y)`
- Every tile is either **walkable** or **blocked**
- Blocked tiles cannot be entered by any entity

## Entity Placement

- All entities (player, NPCs, dropped items, obstacles) are positioned at the centre of their occupied tile(s)
- The player and most NPCs occupy a single 1×1 tile at all times
- Entities can occupy multiple tiles (e.g. a 2×2 boss enemy), positioned at the centre of their footprint

## Adjacency

- The game uses 4-directional adjacency (north, south, east, west) — no diagonal movement
- "Adjacent" means exactly 1 tile away in a cardinal direction
- Melee combat requires the player to be within 1 tile of the target
- Interaction with world objects (trees, rocks, NPCs) also requires adjacency before the action begins

## World Size

The prototype world is 64×64 tiles. Tile `(0,0)` is the top-left corner. The player spawns near the centre.
