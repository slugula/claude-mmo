## Player Avatar - Prototype
- The player's character is humanoid and similar in size and scale to OSRS' player avatars.
- For the scope of this prototype, it is important that the player character's general anatomy is represented - but texturing and animations are not necessary at this stage.

## World - Prototype

- 64×64 tile grid composed of 1x1 tiles
- Procedurally scattered trees (60%) and rocks (40%) using seeded deterministic RNG (seed=42)
- ~6% of tiles have obstacles; clear radius of 6 tiles around player spawn
- Trees: cylindrical trunk + sphere canopy (mesh names encode tile: `tree-trunk-X-Y`, `tree-canopy-X-Y`). Occupies a 1x1 tile.
- Rocks: rotated box mesh (mesh name: `rock-X-Y`). Occupies a 1x1 tile.
- Ground: grass-green DynamicTexture with subtle grid lines

## Player Movement - Prototype
- The game world is composed of 3D game squares. 
- The player always occupies one game square at a time. 
- The player can left-click on a game square to move their character to that position granted the tile is not blocked by collision. 
- The player always faces the tile they are navigating to. 
- The player can only be rotated 90 degrees.

## Game Camera - Prototype
- The camera is always centered on the player during gameplay. 
- Scroll the mouse wheel up to zoom in to a maximum zoom distance 
- Scroll the mouse wheel down to zoom out to a minimum zoom distance 
- Hold down the middle mouse button and pan the camera around the player. 
- Use the arrow keys to rotate the camera left, up, right, down from the player.

## Art Direction
- All gameplay assets which are not related to the UI are 3D including the player character, environment, armor, weapons, etc. 
- Low poly art style for 3D art, similar to Old School RuneScape - but with more charm and detail.
- Avoid overly blocky designs like MineCraft or Roblox for the player character.