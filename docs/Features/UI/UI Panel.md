## Overview

The UI panel is fixed to the bottom-right corner of the screen (12px from the bottom and right edges, matching the chatbox padding). It contains tabbed content panels.

## Tabs

| Tab | Content |
|---|---|
| Inv | 28-slot inventory grid |
| Skills | All 23 skill levels; Hitpoints shows current/max HP |
| Equip | Equipment slots in a human-shaped grid + combat bonuses summary |

- Only one tab is active at a time
- The active tab is highlighted with an orange underline

## Debug Panel (Top-Right)

A separate `#debug-panel` overlay is fixed to the top-right corner of the screen. Shows:
- Current game tick number
- Player tile coordinates `(x, y)`
- Combat level (`Cb: X`)
- Total level (`TL: X`)

## Context Info (Top-Left)

A separate overlay in the top-left corner of the screen (not part of the panel) that shows the current hover context:
- Format: `[Verb] [Subject]` — verb in white, subject in orange
- Examples: `Walk here`, `Attack Chicken`, `Take Egg`
- Updates every frame based on what the cursor is hovering over
- Overridden temporarily when hovering an inventory or equipment slot (shows the item action and name)
