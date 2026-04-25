## Overview

The game implements all 23 OSRS skills. Skills track player progression through XP and levels. Each skill starts at level 1 (Hitpoints starts at level 10).

## Skills

Attack, Hitpoints, Mining, Strength, Agility, Smithing, Defence, Herblore, Fishing, Ranged, Thieving, Cooking, Prayer, Crafting, Firemaking, Magic, Fletching, Woodcutting, Runecraft, Slayer, Farming, Construction, Hunter.

## XP and Levelling

- XP formula matches the official OSRS formula exactly
- Level range: 1–99
- Each skill stores both `xp` (total accumulated) and `level` (derived from XP)
- The level is always the floor of what the XP total corresponds to on the XP table

## Skilling Success Rate
https://oldschool.runescape.wiki/w/Skilling_success_rate

## Combat Level

Derived from Attack, Strength, Defence, Hitpoints, Prayer, Ranged, and Magic — same formula as OSRS. Displayed in the status bar as `Cb: X`.

## Total Level

Sum of all 23 skill levels. Displayed in the status bar as `TL: X`.

## Skills Tab

The Skills tab in the UI panel displays each skill in a grid with its current level. Hitpoints shows `current HP / max HP` instead of just the level.

## Currently Active Skills

Only the following skills are actively trained in the prototype:

| Skill | How XP is earned |
|---|---|
| Attack | Landing a hit in melee combat |
| Strength | Landing a hit in melee combat |
| Defence | Receiving a hit in melee combat |
| Hitpoints | Receiving a hit in melee combat |

All other skills are visible in the UI but have no XP sources yet.

## Equipment Requirements

Items can define skill level requirements. If a player attempts to equip an item without meeting the requirements, the action is blocked and a message is logged to the chat.
