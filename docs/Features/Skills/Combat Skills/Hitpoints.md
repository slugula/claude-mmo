Hitpoints (also known as "health" or "HP") represent a player's health remaining. If a player's hitpoints reach zero, they will die. Players start with 1,154 experience points by default, placing them at exactly level 10.

Players have one hitpoint for every level they have in Hitpoints. A player's health bar is visible over their heads. See: [[Health Bar]]
## Gaining XP
Players gain hitpoints XP by dealing damage with any combat skill. The amount of XP gained is equal to the amount of damage dealt multiplied by 1.33.
## Hit Splats
During combat, whenever you or an enemy deal receive damage, a colored hitsplat will be shown alongside the amount of damage dealt. The hit splat appears immediately at the target's world position when the hit is registered.
### Types of Hit Splats:
- **Red circle** — damage was dealt (shows the damage number)
- **Blue circle** — no damage (miss; shows "0")
### Appearance:
- White bold number centered in the circle with a dark text shadow
- Border color matches the circle fill (darker shade)
### Behavior:
1. The splat appears immediately at the target's world position when the hit is registered
2. It holds in place for **0.5 seconds** (delay phase — no movement)
3. It then floats upward over **1 second** while fading out
4. Total lifetime: 1.5 seconds
5. Splats are world-projected each frame so they track the target's position if it moves.