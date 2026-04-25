import { ItemStack } from '../shared/types';
import { getItem } from '../items/ItemRegistry';
import { INVENTORY_SLOTS } from '../shared/constants';

export function createEmptyInventory(): (ItemStack | null)[] {
  return new Array<ItemStack | null>(INVENTORY_SLOTS).fill(null);
}

export function addItem(
  inventory: (ItemStack | null)[],
  itemId: string,
  quantity: number = 1,
): { inventory: (ItemStack | null)[]; added: boolean } {
  const def = getItem(itemId);
  if (!def) return { inventory, added: false };

  const inv = [...inventory];

  if (def.stackable) {
    const slotIdx = inv.findIndex(s => s?.itemId === itemId);
    if (slotIdx !== -1) {
      inv[slotIdx] = { itemId, quantity: inv[slotIdx]!.quantity + quantity };
      return { inventory: inv, added: true };
    }
  }

  const emptySlot = inv.findIndex(s => s === null);
  if (emptySlot === -1) return { inventory, added: false };

  inv[emptySlot] = { itemId, quantity };
  return { inventory: inv, added: true };
}

export function removeItem(
  inventory: (ItemStack | null)[],
  slotIndex: number,
  quantity: number = 1,
): (ItemStack | null)[] {
  const inv = [...inventory];
  const slot = inv[slotIndex];
  if (!slot) return inv;

  if (slot.quantity <= quantity) {
    inv[slotIndex] = null;
  } else {
    inv[slotIndex] = { ...slot, quantity: slot.quantity - quantity };
  }
  return inv;
}

export function countItem(inventory: (ItemStack | null)[], itemId: string): number {
  return inventory.reduce((total, slot) => {
    if (slot?.itemId === itemId) return total + slot.quantity;
    return total;
  }, 0);
}

export function isFull(inventory: (ItemStack | null)[]): boolean {
  return inventory.every(slot => slot !== null);
}

export function freeSlots(inventory: (ItemStack | null)[]): number {
  return inventory.filter(slot => slot === null).length;
}
