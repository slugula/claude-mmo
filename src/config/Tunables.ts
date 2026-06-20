// Global gameplay tunables — one knob per action type, applied everywhere.
// Loaded from the DB `game_config` table at startup (see EntityLoader); falls
// back to these defaults when a key is missing. Edited via the editor's
// Database ▸ Tunables tab. Server-authoritative only (the client never reads
// these). Adding a new tunable = add a default here + a seed row + read it.
//
// Values are in 200ms server ticks (e.g. 12 ticks = 2.4s between rolls).

export const TUNABLE_DEFAULTS: Record<string, number> = {
  chop_interval:    12,  // ticks between woodcutting success rolls
  mine_interval:    12,  // ticks between mining success rolls
  fish_interval:    12,  // ticks between fishing success rolls
  produce_interval:  3,  // ticks between production attempts (prep / cook / …)
};

let values: Record<string, number> = { ...TUNABLE_DEFAULTS };

// Read a tunable in ticks, falling back to its default (then 1) if unset.
export function tunable(key: string): number {
  return values[key] ?? TUNABLE_DEFAULTS[key] ?? 1;
}

// Replace the live values from DB rows. Unknown keys are ignored; missing keys
// keep their default. Called by the EntityLoader.
export function reloadTunables(rows: { key: string; value: number }[]): void {
  values = { ...TUNABLE_DEFAULTS };
  for (const r of rows) {
    if (r.key in TUNABLE_DEFAULTS && Number.isFinite(r.value)) {
      values[r.key] = Math.max(1, Math.floor(r.value));
    }
  }
}
