## Overview

Items exist in two states: in the world as 3D dropped meshes, or in the player's inventory/equipment as data. Item definitions are stored in `src/data/items.json` — adding a new item requires only a JSON entry, no code changes. `ItemRegistry.ts` is a thin wrapper that loads and indexes the JSON at startup.

## Item Definition Fields

| Field          | Description                                                    |
| -------------- | -------------------------------------------------------------- |
| `id`           | Unique string identifier                                       |
| `name`         | Player-facing display name                                     |
| `stackable`    | Whether multiple instances stack in one inventory slot         |
| `value`        | Base value in gold (GP) — shown on Examine                     |
| `tradable`     | Optional flag marking the item as tradable                     |
| `equipSlot`    | If present, the item is equippable into this slot              |
| `stats`        | Combat bonuses: `attackBonus`, `defenceBonus`, `strengthBonus` |
| `requirements` | Minimum skill levels required to equip                         |
| `examine`      | Text logged to chat when player examines the item              |

## Overworld Items (Dropped)

- Items on the ground are represented as 3D meshes at the tile where they were dropped or spawned
- Mesh root is named `item-{droppedItemId}` — used for raycasting and hover detection
- Meshes are low-profile (flush to the ground or slightly raised)
- Primary action: **Take** — adds the item to inventory if a free slot exists; player must walk to the tile first
- When picked up, the mesh is removed from the scene immediately

## Sprites

- Inventory icons are 2D sprites drawn procedurally on `<canvas>` elements via `ItemSprites.ts`
- Each item can define a custom sprite drawing function; items without one get a colored rectangle with a black outline
- Sprite size is 32×32px, rendered at 70% of the slot size

## Current Items

See `docs/Database/Items.md` for the full table of all 29 defined items.

Notable items:
- **Iron Pickaxe** — equippable Main Hand; +6 attack, +1 defence; requires Mining 1; every player starts with one
- **Egg** — dropped by Chickens at 100% rate; tradable
- **Amulet of str** — equippable Neck; +4 strength bonus; highest value item at 200 GP
