## Overview

Hit splats are circular damage number indicators that appear above an entity when they receive a hit. They are DOM elements projected into world space.

## Appearance

- **Red circle** — damage was dealt (shows the damage number)
- **Blue circle** — no damage (miss; shows "0")
- White bold number centered in the circle with a dark text shadow
- Border color matches the circle fill (darker shade)

## Behaviour

1. The splat appears immediately at the target's world position when the hit is registered
2. It holds in place for **0.5 seconds** (delay phase — no movement)
3. It then floats upward over **1 second** while fading out
4. Total lifetime: 1.5 seconds

## Positioning

Splats are world-projected each frame so they track the target's position if it moves. Player splats appear at Y=1.2 (above head height); NPC splats appear at Y=0.9.
