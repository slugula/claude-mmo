import type { PlayerState, DroppedItemState, GameAction, WorldState, SkillId } from '../shared/types';
import { addItem, removeItem } from './InventorySystem';
import { getItem } from '../items/ItemRegistry';
import { findPath } from '../world/Pathfinder';

export function processItems(
  player: PlayerState,
  droppedItems: DroppedItemState[],
  actions: GameAction[],
  world: WorldState,
  tick: number,
): { player: PlayerState; droppedItems: DroppedItemState[]; messages: string[] } {
  let nextPlayer = player;
  let nextDropped = [...droppedItems];
  const messages: string[] = [];

  for (const action of actions) {
    if (action.type === 'TAKE_ITEM') {
      const item = nextDropped.find(d => d.id === action.droppedItemId);
      if (!item) continue;

      if (nextPlayer.tileX === item.tileX && nextPlayer.tileY === item.tileY) {
        const result = addItem(nextPlayer.inventory, item.itemId, item.quantity);
        if (result.added) {
          nextPlayer = { ...nextPlayer, inventory: result.inventory, pickupItemId: null };
          nextDropped = nextDropped.filter(d => d.id !== item.id);
        } else {
          messages.push('Your inventory is full.');
          nextPlayer = { ...nextPlayer, pickupItemId: null };
        }
      } else {
        const from = { x: nextPlayer.tileX, y: nextPlayer.tileY };
        const to   = { x: item.tileX, y: item.tileY };
        const path = world.tiles[to.y]?.[to.x]?.walkable ? findPath(world, from, to) : [];
        nextPlayer = {
          ...nextPlayer,
          pickupItemId: item.id,
          attackTargetId: null,
          talkTargetId: null,
          path,
          destinationX: to.x,
          destinationY: to.y,
        };
      }
    }

    if (action.type === 'DROP_ITEM') {
      const slot = nextPlayer.inventory[action.slotIndex];
      if (!slot) continue;
      const newInv = removeItem(nextPlayer.inventory, action.slotIndex, slot.quantity);
      nextPlayer = { ...nextPlayer, inventory: newInv };
      nextDropped.push({
        id: `drop-${tick}-slot${action.slotIndex}`,
        itemId: slot.itemId,
        quantity: slot.quantity,
        tileX: nextPlayer.tileX,
        tileY: nextPlayer.tileY,
        droppedAtTick: tick,
      });
    }

    if (action.type === 'MOVE_SLOT') {
      const inv = [...nextPlayer.inventory];
      const temp = inv[action.fromSlot];
      inv[action.fromSlot] = inv[action.toSlot];
      inv[action.toSlot] = temp;
      nextPlayer = { ...nextPlayer, inventory: inv };
    }

    if (action.type === 'EQUIP_ITEM') {
      const stack = nextPlayer.inventory[action.slotIndex];
      if (!stack) continue;
      const def = getItem(stack.itemId);
      if (!def?.equipSlot) continue;

      if (def.requirements) {
        let blocked = false;
        for (const [skill, reqLevel] of Object.entries(def.requirements) as [SkillId, number][]) {
          const playerLevel = nextPlayer.skills[skill]?.level ?? 0;
          if (playerLevel < reqLevel) {
            messages.push(`You need level ${reqLevel} ${skill} to use this.`);
            blocked = true;
            break;
          }
        }
        if (blocked) continue;
      }

      const slot = def.equipSlot;
      const equipped = { ...nextPlayer.equipped };
      let inv = [...nextPlayer.inventory];

      if (equipped[slot]) {
        const current = equipped[slot]!;
        const addResult = addItem(inv.map((s, i) => i === action.slotIndex ? null : s), current.itemId, current.quantity);
        if (!addResult.added) { messages.push('Your inventory is full.'); continue; }
        inv = addResult.inventory;
      } else {
        inv = removeItem(inv, action.slotIndex, stack.quantity);
      }

      equipped[slot] = { itemId: stack.itemId, quantity: stack.quantity };
      nextPlayer = { ...nextPlayer, inventory: inv, equipped };
    }

    if (action.type === 'UNEQUIP_ITEM') {
      const equipped = { ...nextPlayer.equipped };
      const stack = equipped[action.slot];
      if (!stack) continue;
      const result = addItem(nextPlayer.inventory, stack.itemId, stack.quantity);
      if (!result.added) { messages.push('Your inventory is full.'); continue; }
      delete equipped[action.slot];
      nextPlayer = { ...nextPlayer, inventory: result.inventory, equipped };
    }

    if (action.type === 'SEND_CHAT') {
      if (action.message.trim().length === 0) continue;
      const msg = `${nextPlayer.playerName}: ${action.message}`;
      messages.push(`chat:${msg}`);
      nextPlayer = { ...nextPlayer, chatMessage: action.message, chatMessageTick: tick };
    }
  }

  // Arrival check — player walked to item tile
  if (nextPlayer.pickupItemId && nextPlayer.path.length === 0) {
    const item = nextDropped.find(d => d.id === nextPlayer.pickupItemId);
    if (!item) {
      nextPlayer = { ...nextPlayer, pickupItemId: null };
    } else if (nextPlayer.tileX === item.tileX && nextPlayer.tileY === item.tileY) {
      const result = addItem(nextPlayer.inventory, item.itemId, item.quantity);
      if (result.added) {
        nextPlayer = { ...nextPlayer, inventory: result.inventory, pickupItemId: null };
        nextDropped = nextDropped.filter(d => d.id !== item.id);
      } else {
        messages.push('Your inventory is full.');
        nextPlayer = { ...nextPlayer, pickupItemId: null };
      }
    }
  }

  return { player: nextPlayer, droppedItems: nextDropped, messages };
}
