## Overview

- A clickbox refers to the interactive area (either static or moving) that players can click on to interact with objects, NPCs and other elements in the game world.
- A clickbox is not always shaped as a box and can be represented as virtually any 3D shape.
- Depending on whether clicking an entity should be challenging or not, the clickbox can be intentionally easier or harder to click.
- Clickboxes also apply to the ground tiles in the environment. Each ground tile is a clickbox that players interact with to move their player character.
- Clickboxes can change dynamically, such as when NPCs perform animations that temporarily shrink or shift their clickable area.
- The ways to interact with a clickbox are to left-click to perform the clickbox's primary action or to right-click and select an action from the context menu.
- Whenever a player interacts with a clickbox via an action, they should attempt to pathfind towards the entity until they are at least one tile away. For example, if a player chooses to "Chop" a tree, they will walk up to the tree before beginning the actual action of chopping it down. IF a player chooses to "Attack" an enemy, they will walk up to the enemy before beginning the actual combat phase.
- If for whatever reason, the player cannot reach the subject because their path is blocked, then no action should be performed.
- For melee combat, a player needs to be within a 1 tile distance from the subject