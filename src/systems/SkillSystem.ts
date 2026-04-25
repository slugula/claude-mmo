import { ALL_SKILLS } from '../shared/types';
import type { SkillId, SkillState, SkillsState } from '../shared/types';
import { MAX_SKILL_LEVEL, MAX_XP } from '../shared/constants';

// OSRS XP table — xpTable[level] = XP required to reach that level (1-indexed)
const xpTable: number[] = buildXPTable();

function buildXPTable(): number[] {
  const table = new Array<number>(MAX_SKILL_LEVEL + 1).fill(0);
  let points = 0;
  for (let level = 1; level < MAX_SKILL_LEVEL; level++) {
    points += Math.floor(level + 300 * Math.pow(2, level / 7));
    table[level + 1] = Math.floor(points / 4);
  }
  return table;
}

export function xpForLevel(level: number): number {
  if (level <= 1) return 0;
  if (level > MAX_SKILL_LEVEL) return xpTable[MAX_SKILL_LEVEL];
  return xpTable[level];
}

export function levelFromXP(xp: number): number {
  let level = 1;
  while (level < MAX_SKILL_LEVEL && xpTable[level + 1] <= xp) {
    level++;
  }
  return level;
}

export function xpToNextLevel(xp: number): number {
  const level = levelFromXP(xp);
  if (level >= MAX_SKILL_LEVEL) return 0;
  return xpTable[level + 1] - xp;
}

export function progressToNextLevel(xp: number): number {
  const level = levelFromXP(xp);
  if (level >= MAX_SKILL_LEVEL) return 1;
  const currentFloor = xpTable[level];
  const nextFloor = xpTable[level + 1];
  return (xp - currentFloor) / (nextFloor - currentFloor);
}

export function addXP(skill: SkillState, amount: number): { skill: SkillState; levelsGained: number } {
  const newXP = Math.min(MAX_XP, skill.xp + amount);
  const newLevel = levelFromXP(newXP);
  const levelsGained = newLevel - skill.level;
  return {
    skill: { xp: newXP, level: newLevel },
    levelsGained,
  };
}

export function createDefaultSkills(): SkillsState {
  const skills = {} as SkillsState;
  for (const id of ALL_SKILLS) {
    const startXP = id === 'hitpoints' ? xpForLevel(10) : 0;
    skills[id] = {
      xp: startXP,
      level: id === 'hitpoints' ? 10 : 1,
    };
  }
  return skills;
}

export function getTotalLevel(skills: SkillsState): number {
  return ALL_SKILLS.reduce((sum, id) => sum + skills[id].level, 0);
}

export function getCombatLevel(skills: SkillsState): number {
  const { attack, strength, defence, hitpoints, prayer, ranged, magic } = skills;
  const base = 0.25 * (defence.level + hitpoints.level + Math.floor(prayer.level / 2));
  const melee = 0.325 * (attack.level + strength.level);
  const range = 0.325 * Math.floor(ranged.level * 1.5);
  const mage  = 0.325 * Math.floor(magic.level * 1.5);
  return Math.floor(base + Math.max(melee, range, mage));
}

export type { SkillId, SkillState, SkillsState };
