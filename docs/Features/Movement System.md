
- Player occupies one tile at a time
- A* (4-directional, Manhattan heuristic)
- 2048 node safety limit to prevent freezing on unreachable targets
- Path excludes starting tile; player advances 1 tile per tick
- Left-click a walkable tile → A* pathfinding, player walks there (1 tile/tick)
- Player always faces the direction they're moving (4 cardinal directions, 90° snaps)
- Last MOVE_TO action wins within a tick (matches OSRS click-to-interrupt behaviour)
- Yellow semi-transparent ground indicator on hovered walkable tiles (flush with ground)
- Orange pulsing indicator at player's current movement destination