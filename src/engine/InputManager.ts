import { Scene, PointerEventTypes } from '@babylonjs/core';
import type { AbstractMesh } from '@babylonjs/core';
import type { GameAction, WorldState, HoverTarget, NPCState, DroppedItemState, PlayerState } from '../shared/types';
import { getContextActions } from '../world/Interactables';
import type { ContextEntry } from '../world/Interactables';
import { ChatLog } from '../ui/ChatLog';
import { getCombatLevel } from '../systems/SkillSystem';

function meshNameToHoverTarget(
  meshName: string,
  hitX: number,
  hitZ: number,
  world: WorldState,
  npcs: NPCState[],
  droppedItems: DroppedItemState[],
  players: Record<string, PlayerState>,
  localPlayerId: string | null,
): HoverTarget {
  if (meshName.startsWith('item-')) {
    const droppedItemId = meshName.slice(5);
    const dropped = droppedItems.find(d => d.id === droppedItemId);
    if (dropped) {
      return { kind: 'item', tileX: dropped.tileX, tileY: dropped.tileY, droppedItemId: dropped.id, itemId: dropped.itemId };
    }
  }

  if (meshName.startsWith('npc-')) {
    const npcId = meshName.slice(4);
    const npc = npcs.find(n => n.id === npcId);
    if (npc) {
      return { kind: 'npc', tileX: npc.tileX, tileY: npc.tileY, npcId: npc.id };
    }
  }

  if (meshName.startsWith('chest-')) {
    const p = meshName.split('-');
    return { kind: 'chest', tileX: parseInt(p[1]), tileY: parseInt(p[2]) };
  }

  if (meshName.startsWith('tree-')) {
    const p = meshName.split('-');
    return { kind: 'tree', tileX: parseInt(p[2]), tileY: parseInt(p[3]) };
  }

  if (meshName.startsWith('rock-')) {
    const p = meshName.split('-');
    return { kind: 'rock', tileX: parseInt(p[1]), tileY: parseInt(p[2]) };
  }

  if (meshName.startsWith('player-') && !meshName.startsWith('player-root-')) {
    const playerId = meshName.slice('player-'.length);
    if (playerId === localPlayerId) return { kind: 'none', tileX: 0, tileY: 0 };
    const p = players[playerId];
    if (p) {
      return {
        kind: 'player',
        tileX: p.tileX,
        tileY: p.tileY,
        playerId,
        playerName: p.playerName,
        playerLevel: getCombatLevel(p.skills),
      };
    }
  }

  if (meshName === 'ground') {
    const tx = Math.round(hitX);
    const ty = Math.round(hitZ);
    if (tx >= 0 && ty >= 0 && tx < world.width && ty < world.height && world.tiles[ty][tx].walkable) {
      return { kind: 'walkable', tileX: tx, tileY: ty };
    }
  }

  return { kind: 'none', tileX: 0, tileY: 0 };
}

export class InputManager {
  private pendingActions: GameAction[] = [];
  private dispatchFn: ((action: GameAction) => void) | null = null;
  readonly heldKeys = new Set<string>();

  currentHover: HoverTarget = { kind: 'none', tileX: 0, tileY: 0 };
  currentHoverMesh: AbstractMesh | null = null;

  onRightClick: ((entries: ContextEntry[], screenX: number, screenY: number) => void) | null = null;
  onLeftClick: ((screenX: number, screenY: number, kind: string) => void) | null = null;
  onCanvasPointerDown: (() => void) | null = null;

  private world: WorldState | null = null;
  private npcs: NPCState[] = [];
  private droppedItems: DroppedItemState[] = [];
  private players: Record<string, PlayerState> = {};
  private localPlayerId: string | null = null;

  constructor(scene: Scene) {
    this.setupPointer(scene);
    this.setupKeyboard();
  }

  setWorld(world: WorldState): void { this.world = world; }
  setNPCs(npcs: NPCState[]): void { this.npcs = npcs; }
  setDroppedItems(items: DroppedItemState[]): void { this.droppedItems = items; }
  setPlayers(players: Record<string, PlayerState>): void { this.players = players; }
  setLocalPlayerId(id: string): void { this.localPlayerId = id; }
  setDispatch(fn: (action: GameAction) => void): void { this.dispatchFn = fn; }

  drainActions(): GameAction[] {
    const actions = this.pendingActions;
    this.pendingActions = [];
    return actions;
  }

  enqueue(action: GameAction): void {
    this.pendingActions.push(action);
  }

  // Dispatches a context entry — either fires dispatch directly or handles Examine client-side.
  dispatchEntry(entry: ContextEntry): void {
    if (entry.examineText !== undefined) {
      ChatLog.log(entry.examineText);
      return;
    }
    if (entry.action !== null) {
      if (this.dispatchFn) {
        this.dispatchFn(entry.action);
      } else {
        this.pendingActions.push(entry.action);
      }
    }
  }

  private setupPointer(scene: Scene): void {
    scene.onPointerObservable.add((info) => {
      if (!this.world) return;

      const pick = scene.pick(scene.pointerX, scene.pointerY, (mesh) => mesh.isPickable);

      if (!pick?.hit || !pick.pickedMesh || !pick.pickedPoint) {
        if (info.type === PointerEventTypes.POINTERMOVE) {
          this.currentHover = { kind: 'none', tileX: 0, tileY: 0 };
          this.currentHoverMesh = null;
        }
        return;
      }

      const target = meshNameToHoverTarget(
        pick.pickedMesh.name,
        pick.pickedPoint.x,
        pick.pickedPoint.z,
        this.world,
        this.npcs,
        this.droppedItems,
        this.players,
        this.localPlayerId,
      );

      if (info.type === PointerEventTypes.POINTERMOVE) {
        this.currentHover = target;
        this.currentHoverMesh = pick.pickedMesh;
        return;
      }

      if (info.type === PointerEventTypes.POINTERDOWN) {
        this.onCanvasPointerDown?.();
        const evt = info.event as PointerEvent;
        const entries = getContextActions(target, this.npcs);

        if (evt.button === 0) {
          this.onLeftClick?.(evt.clientX, evt.clientY, target.kind);
          if (entries.length > 0) this.dispatchEntry(entries[0]);
        } else if (evt.button === 2) {
          if (entries.length > 0 && this.onRightClick) {
            this.onRightClick(entries, evt.clientX, evt.clientY);
          }
        }
      }
    });
  }

  private setupKeyboard(): void {
    window.addEventListener('keydown', (e) => {
      this.heldKeys.add(e.key);
      if (e.key.startsWith('Arrow')) e.preventDefault();
    });
    window.addEventListener('keyup', (e) => {
      this.heldKeys.delete(e.key);
    });
  }
}
