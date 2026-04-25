## Overview

The game world is a fixed 64×64 tile grid. Each tile is a 1×1 unit square. The world is generated once at startup using a seeded deterministic RNG (seed=42) and does not change during a session.

## Tiles

- Every tile is either **walkable** or **blocked**
- Blocked tiles are occupied by a static obstacle (tree or rock)
- ~6% of tiles are blocked; a clear radius of 6 tiles around the player spawn point is always guaranteed
- The ground plane is a single flat mesh with a grass-green DynamicTexture and subtle grid lines for visual reference

## Obstacles

Two obstacle types are procedurally placed at world generation:

**Trees (60% of obstacles)**
- Cylindrical trunk + sphere canopy
- Mesh names encode position: `tree-trunk-{x}-{y}`, `tree-canopy-{x}-{y}`
- Occupy a 1×1 tile
- Primary action: Chop (not yet implemented)

**Rocks (40% of obstacles)**
- Rotated box mesh
- Mesh name: `rock-{x}-{y}`
- Occupy a 1×1 tile
- Primary action: Mine (not yet implemented)

## Art Direction

- All gameplay objects (world, characters, items on ground) are 3D low-poly
- UI elements are 2D DOM overlays — no 3D UI
- Item inventory icons are 2D canvas sprites
- Low-poly style similar to OSRS but with more charm and detail; avoid overly blocky designs (Minecraft/Roblox aesthetic is not the target)
