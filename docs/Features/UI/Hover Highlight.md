## Overview

Two distinct visual effects communicate interactability when the player hovers over entities and tiles.

## Tile Hover (Walkable Ground)

- A white square outline appears around the hovered tile, flush with the ground
- Semi-transparent (50% opacity)
- Only appears on walkable tiles — no indicator on blocked tiles or entities

## Entity Hover (Interactable Objects)

- A **cyan glow** (outline highlight) renders around the hovered entity's mesh
- Applies to: trees, rocks, NPCs, dropped items
- Implemented via Babylon.js `HighlightLayer` with outer glow only (inner glow disabled)
- The glow updates every frame — it is removed and re-added as the hover target changes
- The cursor changes to a pointer (hand icon) when hovering any interactable entity

## Click Feedback

When the player clicks anywhere in the game world, a small translucent circle briefly appears at the click point and fades out over ~450ms:
- **Yellow circle** — clicked a walkable tile, nothing, or a context menu action
- **Red circle** — clicked an interactable entity (tree, rock, NPC, item)
