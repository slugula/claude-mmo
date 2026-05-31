import type { PlayerState } from '../shared/types';

// Single source of truth for "what is the player currently trying to do".
//
// Every queued/ongoing action stores its intent on the player as a target field
// (combat target, chop/mine/fish tiles, pickup, talk, …). Walking away — or any
// other "cancel everything" event — must clear ALL of them, otherwise the
// owning system re-paths back and resumes the action.
//
// Keep this the ONE place that lists those fields: when you add a new action
// with a target field, add it here and walking away cancels it automatically —
// no need to update MovementSystem (or anywhere else) per action.
export function clearActionIntents<T extends PlayerState>(p: T): T {
  return {
    ...p,
    attackTargetId: null,
    talkTargetId:   null,
    pickupItemId:   null,
    chopTargetX:    null,
    chopTargetY:    null,
    mineTargetX:    null,
    mineTargetY:    null,
    fishTargetX:    null,
    fishTargetY:    null,
  };
}
