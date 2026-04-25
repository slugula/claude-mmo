## Overview

The camera is an orbiting third-person camera that always follows the player. It uses smooth exponential interpolation so it never feels "snappy" or laggy.

## Controls

| Input | Action |
|---|---|
| Scroll wheel up/down | Zoom in / zoom out |
| Middle mouse hold + drag | Orbit around the player |
| Arrow keys | Rotate (left/right) or tilt (up/down) the camera |

- Dragging up (moving mouse upward while middle-clicking) increases the camera's horizontal angle — the view becomes more side-on
- Dragging down lowers the camera toward a top-down view
- Zoom has a minimum and maximum radius enforced at all times

## Follow Behaviour

- The camera target chases the player's interpolated world position each frame using exponential decay
- This means the camera leads the player slightly during movement and snaps smoothly when the player stops
- The follow speed is tuned so the camera never visibly lags behind even at maximum movement speed
