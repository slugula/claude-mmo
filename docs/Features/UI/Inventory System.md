### Inventory

- 28 slots in a 4-column grid

- Items represented by 2D canvas sprites with black stroke for contrast against the dark background

- Custom sprites defined in `ItemSprites.ts`; default sprite = colored rectangle with black outline

  
#### Moving Items (Drag & Drop)

- Hold left-click on an item slot and drag the mouse

- Dragged item's slot opacity drops to 40%; a ghost canvas icon follows the cursor

- Releasing over an empty slot moves the item; releasing over an occupied slot swaps both items

- Dispatches `MOVE_SLOT` action

  
#### Dropping Items

- Right-click an inventory slot → context menu with "Drop [Name]", "Examine [Name]", "Cancel"

- "Drop" removes the item from inventory and spawns its 3D mesh at the player's current tile

- Dispatches `DROP_ITEM` action