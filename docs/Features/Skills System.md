## Overview
Skills track player progression through XP and levels. Each skill starts at level 1 (Hitpoints starts at level 10). While Skills fall into different categories such as Combat, Production, and Gathering - the categories are just used as a way to convey the use case for each skill.

## Skills
See: \OSRS Prototype\docs\Features\Skills

## XP and Levelling

- XP formula matches the official OSRS formula exactly
- Level range: 1–99
- Each skill stores both `xp` (total accumulated) and `level` (derived from XP)
- The level is always the floor of what the XP total corresponds to on the XP table

## Skilling Success Rate
https://oldschool.runescape.wiki/w/Skilling_success_rate

## Combat Level

Derived from combat skills; Hitpoints, Defense, etc. — same formula as OSRS but without Attack and Strength. Displayed in the status bar as `Cb: X`.

## Total Level

Sum of all skill levels. Displayed in the status bar as `TL: X`.

## Skills Tab

The Skills tab in the UI panel displays each skill in a grid with its current level. 
Hitpoints shows `current HP / max HP` instead of just the level.

## Equipment Requirements

Items can define skill level requirements. If a player attempts to equip an item without meeting the requirements, the action is blocked and a message is logged to the chat.
