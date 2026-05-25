import type { HoverTarget, GameAction, NPCState } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { getNPCDef } from '../npcs/NPCRegistry';

export interface ContextEntry {
  verb: string;
  subject: string;
  subjectSuffix?: string;  // displayed in yellow after subject (e.g. player level)
  // null = examine / flavour only — handled client-side, never enqueued
  action: GameAction | null;
  // Flavour text shown on Examine; undefined for non-examine entries
  examineText?: string;
}

function walkHere(tileX: number, tileY: number): ContextEntry {
  return { verb: 'Walk here', subject: '', action: { type: 'MOVE_TO', targetX: tileX, targetY: tileY } };
}

function examine(subject: string, text: string): ContextEntry {
  return { verb: 'Examine', subject, action: null, examineText: text };
}

function uniqueForNPC(npc: NPCState): ContextEntry[] {
  const def = getNPCDef(npc.kind);
  return def.uniqueActions.map(verb => ({
    verb,
    subject: def.name,
    action: verbToAction(verb, npc),
  }));
}

function verbToAction(verb: string, npc: NPCState): GameAction | null {
  switch (verb) {
    case 'Attack':  return { type: 'ATTACK_NPC', npcId: npc.id };
    case 'Talk to': return { type: 'TALK_TO',    npcId: npc.id };
    default:        return null;
  }
}

export function getContextActions(target: HoverTarget, npcs: NPCState[]): ContextEntry[] {
  switch (target.kind) {
    case 'walkable':
      return [walkHere(target.tileX, target.tileY)];

    case 'chest':
      return [
        { verb: 'Bank', subject: 'Chest', action: { type: 'OPEN_BANK' } },
        walkHere(target.tileX, target.tileY),
        examine('Chest', 'A secure bank chest.'),
      ];

    case 'tree':
      return [
        { verb: 'Chop down', subject: 'Tree', action: { type: 'CHOP_TREE', tileX: target.tileX, tileY: target.tileY } },
        walkHere(target.tileX, target.tileY),
        examine('Tree', 'A sturdy tree.'),
      ];

    case 'rock':
      return [
        { verb: 'Mine', subject: 'Rock', action: { type: 'MINE_ROCK', tileX: target.tileX, tileY: target.tileY } },
        walkHere(target.tileX, target.tileY),
        examine('Rock', 'A rocky outcrop.'),
      ];

    case 'npc': {
      const npc = npcs.find(n => n.id === target.npcId);
      if (!npc) return [];
      const def = getNPCDef(npc.kind);
      return [
        ...uniqueForNPC(npc),
        walkHere(target.tileX, target.tileY),
        examine(def.name, def.examine),
      ];
    }

    case 'item': {
      const name = getItem(target.itemId ?? '')?.name ?? 'Item';
      return [
        { verb: 'Take', subject: name, action: { type: 'TAKE_ITEM', droppedItemId: target.droppedItemId ?? '' } },
        walkHere(target.tileX, target.tileY),
        examine(name, `It\u2019s ${name.toLowerCase()}.`),
      ];
    }

    case 'player': {
      const name  = target.playerName  ?? 'Player';
      const level = target.playerLevel ?? 1;
      const suffix = `(Level-${level})`;
      const playerEntry = (verb: string, action: GameAction | null, examineText?: string): ContextEntry =>
        ({ verb, subject: name, subjectSuffix: suffix, action, examineText });
      return [
        playerEntry('Walk here', { type: 'MOVE_TO', targetX: target.tileX, targetY: target.tileY }),
        playerEntry('Follow',    null, 'Nothing happens. Feature not implemented.'),
        playerEntry('Trade with', null, 'Nothing happens. Feature not implemented.'),
        playerEntry('Examine',   null, `${name} — Combat Level ${level}`),
      ];
    }

    default:
      return [];
  }
}

export function getPrimaryAction(target: HoverTarget, npcs: NPCState[]): ContextEntry | null {
  return getContextActions(target, npcs)[0] ?? null;
}
