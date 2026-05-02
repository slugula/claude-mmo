export const TICK_DURATION_MS = 200;

export const GRID_WIDTH = 64;
export const GRID_HEIGHT = 64;
export const TILE_SIZE = 1;

export const INVENTORY_SLOTS = 28;

export const CAMERA_MIN_RADIUS = 5;
export const CAMERA_MAX_RADIUS = 28;
export const CAMERA_MIN_BETA = Math.PI / 8;       // ~22° — nearly top-down
export const CAMERA_MAX_BETA = Math.PI / 2.2;     // ~82° — near horizontal
export const CAMERA_ROTATE_SPEED = 1.2;           // radians per second when arrow held
export const CAMERA_ZOOM_SPEED = 0.04;
export const CAMERA_DRAG_SENSITIVITY = 0.005;

export const PLAYER_START_X = 32;
export const PLAYER_START_Y = 32;

export const MAX_SKILL_LEVEL = 99;
export const MAX_XP = 200_000_000;

export const OBSTACLE_CLEAR_RADIUS = 6;           // tiles around spawn kept clear
export const OBSTACLE_DENSITY = 0.06;             // ~6% of tiles have obstacles

// Combat / player death
export const PLAYER_DEATH_TICKS = 12;             // ~2.4s dying state before respawn
export const PLAYER_REGEN_INTERVAL_TICKS = 100;   // 1 HP healed passively every 20 seconds

// Gunner skill
export const GUNNER_ATTACK_SPEED = 3;             // ticks between chaingun shots (~600ms)
export const GUNNER_ATTACK_RANGE  = 8;            // max Chebyshev distance for ranged attacks

// Respawn location — change here to move the spawn point
export const RESPAWN_X = PLAYER_START_X;
export const RESPAWN_Y = PLAYER_START_Y;
