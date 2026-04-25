## Overview

Health bars appear above NPCs during and shortly after combat. They are DOM elements projected into world space.

## Appearance

- A horizontal bar split into two segments: green (remaining HP) and red (missing HP)
- Thin black border
- Positioned above the NPC's mesh via world-to-screen projection, updated every frame

## Behaviour

- The bar is **hidden by default** — it only appears after the NPC receives its first hit (including 0-damage hits)
- After the last hit, the bar persists for **5 seconds** then disappears
- Each new hit resets the 5-second timer

## Player HP

The player does not have an overhead health bar. Player HP is displayed in the Skills tab as `current / max` on the Hitpoints skill cell.
