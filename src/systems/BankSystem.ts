import type { ItemStack } from '../shared/types';
import { BANK_SLOTS } from '../shared/constants';

export function createEmptyBank(): (ItemStack | null)[] {
  return new Array<ItemStack | null>(BANK_SLOTS).fill(null);
}

/**
 * Add items to the bank. Unlike the inventory, the bank always stacks by itemId
 * regardless of the item's `stackable` flag — one slot per unique item type.
 */
export function bankAddItem(
  bank: (ItemStack | null)[],
  itemId: string,
  quantity: number,
): { bank: (ItemStack | null)[]; added: boolean } {
  const b = [...bank];

  // Find existing slot for this item type
  const existing = b.findIndex(s => s?.itemId === itemId);
  if (existing !== -1) {
    b[existing] = { itemId, quantity: b[existing]!.quantity + quantity };
    return { bank: b, added: true };
  }

  // Use an empty slot
  const emptySlot = b.findIndex(s => s === null);
  if (emptySlot === -1) return { bank, added: false };

  b[emptySlot] = { itemId, quantity };
  return { bank: b, added: true };
}

export function bankRemoveItem(
  bank: (ItemStack | null)[],
  slotIndex: number,
  quantity: number,
): (ItemStack | null)[] {
  const b = [...bank];
  const slot = b[slotIndex];
  if (!slot) return b;

  if (slot.quantity <= quantity) {
    b[slotIndex] = null;
  } else {
    b[slotIndex] = { ...slot, quantity: slot.quantity - quantity };
  }
  return b;
}
