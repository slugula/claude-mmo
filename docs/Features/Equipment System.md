## Overview

Players can equip items from their inventory into dedicated equipment slots. Equipped items provide combat stat bonuses and are visually represented on the player avatar.

## Equipment Slots

Ten slots arranged in a human-shaped grid in the Equipment tab:

| Slot | Label | Description |
|---|---|---|
| head | Head | Helmets, hats |
| neck | Neck | Amulets |
| body | Body | Chestpieces |
| legs | Legs | Legwear |
| feet | Feet | Boots |
| hands | Hands | Gloves |
| ring | Ring | Rings |
| rightHand | Main Hand | Weapons and tools |
| leftHand | Off-hand | Shields, off-hand weapons |
| ammo | Ammo | Arrows, bolts |

## Equipping an Item

- Left-click an equippable item in the inventory, or select "Wield" / "Wear" from its right-click menu
- The item moves from the inventory into the appropriate equipment slot
- If the slot is already occupied, the previously equipped item is swapped back into the inventory slot the new item came from
- If the inventory is full and a swap would be needed, the action is blocked with a message

## Unequipping an Item

- Left-click an occupied equipment slot, or right-click and select "Remove"
- The item is returned to the first available inventory slot
- If the inventory is full, the action is blocked with a message

## Stat Bonuses

Each item definition can specify:
- **Attack bonus** — adds to hit accuracy in combat
- **Defence bonus** — reduces incoming hit chance
- **Strength bonus** — increases max hit

Bonuses from all equipped items are summed and displayed at the bottom of the Equipment tab.

## Equipment Requirements

Items can require a minimum skill level to equip (e.g. Attack level 1 for a weapon). Attempting to equip an item without meeting requirements blocks the action and logs a message.

## Visual Representation

- The Equipment tab shows each slot as a grid cell; occupied slots display the item's 2D sprite
- Empty slots show a single-letter label indicating the slot type
- When a Main Hand item is equipped, a corresponding 3D mesh appears in the player avatar's right hand in the game world
