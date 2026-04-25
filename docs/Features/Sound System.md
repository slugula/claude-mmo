## Overview

All sounds are synthesised procedurally using the Web Audio API. There are no external audio files. Sounds are event-driven and triggered by game state changes each tick.

## Browser Autoplay Policy

Browsers block audio until the user interacts with the page. The AudioContext starts in a suspended state and resumes automatically on the player's first mouse click or key press — no user action beyond normal gameplay is required.

## Sound Events

| Event | Sound Design |
|---|---|
| Player takes a hit | Thuddy impact — lowpass noise burst + low sine sweep (120→40 Hz) |
| Player lands a hit | Sharp swipe/thwack — mid sine sweep (700→250 Hz) + brief highpass noise |
| Item equipped | Metallic double-clink — two overlapping sine tones (1100→800 Hz, then 900→650 Hz) |
| Item unequipped | Soft single clink — one sine tone (650→450 Hz), quieter than equip |

## Trigger Timing

Sounds are triggered inside the render loop's per-tick gate by comparing the previous and current game state:

- **Hit sound** — fires when `player.lastHitTick` advances
- **Strike sound** — fires when `player.lastAttackTick` advances
- **Equip sound** — fires when an equipment slot transitions from empty → occupied
- **Unequip sound** — fires when an equipment slot transitions from occupied → empty
