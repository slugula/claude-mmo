import type { PlayerState, DroppedItemState, GameAction, WorldState, SkillId, ShirtColor, SkinColor, EquipSlot } from '../shared/types';
import { addItem, removeItem, freeSlots } from './InventorySystem';
import { bankAddItem, bankRemoveItem, createEmptyBank } from './BankSystem';
import { getItem } from '../items/ItemRegistry';
import { findPath } from '../world/Pathfinder';
import { directionTo } from './CombatSystem';

export function processItems(
  player: PlayerState,
  droppedItems: DroppedItemState[],
  actions: GameAction[],
  world: WorldState,
  tick: number,
): { player: PlayerState; droppedItems: DroppedItemState[]; messages: string[] } {
  if (player.dying) return { player, droppedItems, messages: [] };

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
          // Permanent items stay on the floor — don't remove them
          if (!item.permanent) nextDropped = nextDropped.filter(d => d.id !== item.id);
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
      let equipped = { ...nextPlayer.equipped };
      let inv = [...nextPlayer.inventory];

      // --- Two-handed weapon rules ---
      if (def.twoHanded) {
        // Equipping a two-hander: must free both hand slots first
        for (const hand of ['rightHand', 'leftHand'] as const) {
          if (equipped[hand]) {
            const displaced = equipped[hand]!;
            const addResult = addItem(inv.map((s, i) => i === action.slotIndex ? null : s), displaced.itemId, displaced.quantity);
            if (!addResult.added) { messages.push('Your inventory is full.'); break; }
            inv = addResult.inventory;
            delete equipped[hand];
          }
        }
        // Re-check we actually freed the slots (break above may have left one)
        if (equipped.rightHand || equipped.leftHand) continue;
      } else if (slot === 'rightHand' || slot === 'leftHand') {
        // Equipping a one-handed item: strip two-hander from rightHand if present
        const rhDef = getItem(equipped.rightHand?.itemId ?? '');
        if (rhDef?.twoHanded) {
          const displaced = equipped.rightHand!;
          const addResult = addItem(inv.map((s, i) => i === action.slotIndex ? null : s), displaced.itemId, displaced.quantity);
          if (!addResult.added) { messages.push('Your inventory is full.'); continue; }
          inv = addResult.inventory;
          delete equipped.rightHand;
        }
      }

      // Swap out whatever is currently in the target slot
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

    if (action.type === 'SET_APPEARANCE') {
      const VALID_SHIRT: ShirtColor[] = ['blue', 'red', 'yellow', 'green'];
      const VALID_SKIN:  SkinColor[]  = ['fair', 'tan', 'olive', 'brown'];
      const name       = (action.playerName ?? '').trim().slice(0, 20) || 'Player';
      const shirtColor = VALID_SHIRT.includes(action.shirtColor) ? action.shirtColor : 'blue';
      const skinColor  = VALID_SKIN.includes(action.skinColor)   ? action.skinColor  : 'fair';
      nextPlayer = { ...nextPlayer, playerName: name, shirtColor, skinColor };
    }

    // ---- Banking actions -------------------------------------------------------

    if (action.type === 'OPEN_BANK') {
      // Turn to face the bank chest when it's opened (the client sends the
      // chest tile). The bank UI itself is client-side.
      if (action.tileX !== undefined && action.tileY !== undefined) {
        nextPlayer = {
          ...nextPlayer,
          facing: directionTo(
            { x: nextPlayer.tileX, y: nextPlayer.tileY },
            { x: action.tileX, y: action.tileY }),
        };
      }
    }

    if (action.type === 'DEPOSIT_ITEM') {
      const start = nextPlayer.inventory[action.slotIndex];
      if (!start) continue;
      const itemId = start.itemId;
      // Deposit up to `quantity` units of this item type, pulling from the
      // clicked slot first, then any other inventory slots holding the same
      // item (so "Deposit 10" of 5 non-stackable eggs deposits all 5).
      let inv  = [...nextPlayer.inventory];
      let bank = nextPlayer.bank ?? createEmptyBank();
      let remaining = action.quantity;
      let deposited = false;
      const order = [action.slotIndex,
                     ...inv.map((_, i) => i).filter(i => i !== action.slotIndex)];
      for (const i of order) {
        if (remaining <= 0) break;
        const s = inv[i];
        if (!s || s.itemId !== itemId) continue;
        const take = Math.min(remaining, s.quantity);
        const res  = bankAddItem(bank, itemId, take);
        if (!res.added) { messages.push('Your bank is full.'); break; }
        bank      = res.bank;
        inv       = removeItem(inv, i, take);
        remaining -= take;
        deposited = true;
      }
      if (deposited) nextPlayer = { ...nextPlayer, inventory: inv, bank };
    }

    if (action.type === 'MOVE_BANK_SLOT') {
      const bank = [...(nextPlayer.bank ?? createEmptyBank())];
      if (action.fromSlot >= 0 && action.fromSlot < bank.length &&
          action.toSlot   >= 0 && action.toSlot   < bank.length) {
        const tmp = bank[action.fromSlot];
        bank[action.fromSlot] = bank[action.toSlot];
        bank[action.toSlot]   = tmp;
        nextPlayer = { ...nextPlayer, bank };
      }
    }

    if (action.type === 'DEPOSIT_ALL') {
      let bank = [...(nextPlayer.bank ?? createEmptyBank())];
      let inv  = [...nextPlayer.inventory];
      let full = false;
      for (let i = 0; i < inv.length; i++) {
        const slot = inv[i];
        if (!slot) continue;
        const result = bankAddItem(bank, slot.itemId, slot.quantity);
        if (!result.added) { full = true; break; }
        bank  = result.bank;
        inv[i] = null;
      }
      if (full) messages.push('Your bank is full.');
      nextPlayer = { ...nextPlayer, inventory: inv, bank };
    }

    if (action.type === 'DEPOSIT_WORN') {
      let bank     = [...(nextPlayer.bank ?? createEmptyBank())];
      const equipped = { ...nextPlayer.equipped };
      let full = false;
      for (const slot of Object.keys(equipped) as EquipSlot[]) {
        const stack = equipped[slot];
        if (!stack) continue;
        const result = bankAddItem(bank, stack.itemId, stack.quantity);
        if (!result.added) { full = true; break; }
        bank = result.bank;
        delete equipped[slot];
      }
      if (full) messages.push('Your bank is full.');
      nextPlayer = { ...nextPlayer, equipped, bank };
    }

    if (action.type === 'WITHDRAW_ITEM') {
      const bank      = nextPlayer.bank ?? createEmptyBank();
      const bankStack = bank[action.bankSlot];
      if (!bankStack) continue;
      const qty = Math.min(action.quantity, bankStack.quantity);
      const def = getItem(bankStack.itemId);
      // Stackable: always fits in one slot (merge or new slot).
      // Non-stackable: needs one inventory slot per unit.
      const canFit = def?.stackable
        ? (nextPlayer.inventory.some(s => s?.itemId === bankStack.itemId) || freeSlots(nextPlayer.inventory) > 0 ? qty : 0)
        : Math.min(qty, freeSlots(nextPlayer.inventory));
      if (canFit === 0) { messages.push("You don't have enough inventory space."); continue; }
      if (canFit < qty) messages.push("You don't have enough inventory space to withdraw that many.");
      // addItem places non-stackable items one-per-slot; partial result is intentional
      const invResult = addItem(nextPlayer.inventory, bankStack.itemId, canFit);
      if (!invResult.added) { messages.push("You don't have enough inventory space."); continue; }
      const newBank = bankRemoveItem(bank, action.bankSlot, canFit);
      nextPlayer = { ...nextPlayer, inventory: invResult.inventory, bank: newBank };
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
        // Permanent items stay on the floor — don't remove them
        if (!item.permanent) nextDropped = nextDropped.filter(d => d.id !== item.id);
      } else {
        messages.push('Your inventory is full.');
        nextPlayer = { ...nextPlayer, pickupItemId: null };
      }
    }
  }

  return { player: nextPlayer, droppedItems: nextDropped, messages };
}
