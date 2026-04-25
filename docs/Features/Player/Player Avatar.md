## Overview

The player avatar is a low-poly humanoid that always occupies a single 1×1 tile. It is similar in size and scale to OSRS player avatars.

## Appearance

- Built from box meshes: head, torso, upper arms, lower arms, upper legs, lower legs
- Skin tone and clothing colors are applied per-mesh via material colors
- No textures or animations in the current prototype — anatomy is represented, not detailed
- `renderingGroupId = 1` on all player meshes ensures the player always renders on top of world geometry and NPCs (which use group 0)

## Facing Direction

- The avatar faces one of four cardinal directions: north, south, east, west
- Facing updates each tick based on the direction of movement
- When idle, the avatar holds its last facing direction

## Held Items

- When a weapon or tool is equipped in the Main Hand slot, a 3D mesh representing that item appears in the avatar's right hand
- The held item mesh is a child of the right forearm bone and updates immediately on equip/unequip
- Each item that can be equipped defines its own held mesh appearance (shape and color)
- Items in the Off-hand slot are not yet visually represented

## Scale Reference

The avatar's key Y positions (used for UI anchoring):
- Root: Y=0 (ground level)
- Head centre: ~Y=0.82
- Top of head: ~Y=0.96
- Overhead chat anchor: Y=1.05
