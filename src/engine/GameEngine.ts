import {
  Engine, Scene, HemisphericLight, DirectionalLight,
  Vector3, Color3, Color4, MeshBuilder, StandardMaterial,
  Mesh, InstancedMesh, AbstractMesh, HighlightLayer,
} from '@babylonjs/core';
import type { GameState, NPCState, DroppedItemState, GameAction, HoverTarget } from '../shared/types';
import { createWorldState, buildWorldMeshes } from '../world/World';
import { NPCEntity } from '../entities/NPCEntity';
import { DroppedItemEntity } from '../entities/DroppedItemEntity';
import { PlayerEntity } from '../entities/Player';
import { GameCamera } from '../camera/GameCamera';
import { InputManager } from './InputManager';
import { NetworkClient } from './NetworkClient';
import { GameUI } from '../ui/GameUI';
import { ChatLog } from '../ui/ChatLog';
import { ContextInfo } from '../ui/ContextInfo';
import { ContextMenu } from '../ui/ContextMenu';
import { HitSplatManager } from '../ui/HitSplatManager';
import { HealthBarManager } from '../ui/HealthBarManager';
import { ClickFeedback } from '../ui/ClickFeedback';
import type { ClickMarkerColor } from '../ui/ClickFeedback';
import { OverheadChat } from '../ui/OverheadChat';
import { SoundEngine } from '../audio/SoundEngine';
import { PlayerJoinModal } from '../ui/PlayerJoinModal';
import { LoginUI } from '../ui/LoginUI';
import { PLAYER_START_X, PLAYER_START_Y, TICK_DURATION_MS } from '../shared/constants';

const AUTH_URL = (import.meta.env.VITE_AUTH_URL as string | undefined) ?? 'http://localhost:8080/auth';
const WS_URL   = (import.meta.env.VITE_WS_URL  as string | undefined) ?? 'ws://localhost:8080';

export class GameEngine {
  private engine: Engine;
  private scene: Scene;
  private canvas: HTMLCanvasElement;

  private currentState: GameState;
  private prevState: GameState;
  private localPlayerId: string | null = null;
  private lastServerTickTime: number = performance.now();

  private player: PlayerEntity;
  private remotePlayerEntities: Map<string, PlayerEntity> = new Map();
  private remoteOverheadChats: Map<string, OverheadChat> = new Map();

  private camera: GameCamera;
  private input: InputManager;
  private network: NetworkClient;
  private ui: GameUI;
  private contextInfo: ContextInfo;
  private contextMenu: ContextMenu;

  private npcEntities: Map<string, NPCEntity> = new Map();
  private droppedItemEntities: Map<string, DroppedItemEntity> = new Map();
  private hitSplatManager!: HitSplatManager;
  private healthBarManager!: HealthBarManager;
  private clickFeedback!: ClickFeedback;
  private lastHitTick = 0;
  private overheadChat!: OverheadChat;

  private stumpMeshes: Map<string, Mesh> = new Map();
  private joinModal!: PlayerJoinModal;
  private loginUI!: LoginUI;

  private hoverIndicator: Mesh;
  private destIndicator: Mesh;
  private destPulseDir = 1;
  private destPulseScale = 1;
  private hoverHighlight!: HighlightLayer;
  private soundEngine!: SoundEngine;

  constructor(canvas: HTMLCanvasElement) {
    this.canvas  = canvas;
    this.engine  = new Engine(canvas, true, { preserveDrawingBuffer: true, stencil: true });
    this.scene   = new Scene(this.engine);
    this.scene.clearColor = new Color4(0.45, 0.65, 0.85, 1);

    document.addEventListener('contextmenu', (e) => e.preventDefault());

    const worldState = createWorldState();

    this.currentState = {
      tick: 0,
      world: worldState,
      players: {},
      npcs: [],
      droppedItems: [],
      pendingRespawns: [],
      messages: {},
      depletedTrees: {},
      treeHealth: {},
    };
    this.prevState = this.currentState;

    this.setupLights();
    buildWorldMeshes(worldState, this.scene);

    this.player = new PlayerEntity(this.scene, 'local');

    const initialTarget = new Vector3(PLAYER_START_X, 0, PLAYER_START_Y);
    this.camera = new GameCamera(this.scene, canvas, initialTarget);

    this.input = new InputManager(this.scene);
    this.input.setWorld(worldState);
    this.input.setNPCs([]);
    this.input.setDroppedItems([]);

    this.hitSplatManager  = new HitSplatManager(this.scene);
    this.healthBarManager = new HealthBarManager(this.scene);
    this.clickFeedback    = new ClickFeedback();
    this.overheadChat     = new OverheadChat(this.scene);
    this.soundEngine      = new SoundEngine();

    const hoverPts = [
      new Vector3(-0.5, 0.008, -0.5),
      new Vector3( 0.5, 0.008, -0.5),
      new Vector3( 0.5, 0.008,  0.5),
      new Vector3(-0.5, 0.008,  0.5),
      new Vector3(-0.5, 0.008, -0.5),
    ];
    const hoverColors = hoverPts.map(() => new Color4(1, 1, 1, 0.18));
    const hoverLines = MeshBuilder.CreateLines('hover-indicator', { points: hoverPts, colors: hoverColors }, this.scene);
    hoverLines.isPickable = false;
    hoverLines.setEnabled(false);
    this.hoverIndicator = hoverLines as unknown as Mesh;

    this.destIndicator = this.buildIndicator('dest', new Color3(1, 0.6, 0), 0.01);

    this.hoverHighlight = new HighlightLayer('hover-hl', this.scene);
    this.hoverHighlight.innerGlow = false;
    this.hoverHighlight.blurHorizontalSize = 0.4;
    this.hoverHighlight.blurVerticalSize   = 0.4;

    this.contextInfo = new ContextInfo();

    this.contextMenu = new ContextMenu((entry, x, y) => {
      this.input.dispatchEntry(entry);
      this.clickFeedback.spawn(x, y, 'yellow');
    });
    this.input.onCanvasPointerDown = () => this.ui.dismissInventoryMenu();
    this.input.onRightClick = (entries, x, y) => this.contextMenu.show(entries, x, y);
    this.input.onLeftClick  = (x, y, kind) => {
      this.contextMenu.hide();
      const color: ClickMarkerColor = (kind === 'walkable' || kind === 'none') ? 'yellow' : 'red';
      this.clickFeedback.spawn(x, y, color);
    };

    const dispatch = (action: GameAction) => {
      // Immediately apply appearance changes to the local player — no server roundtrip needed
      // for pure visual updates. The server will also process the action so other players see it.
      if (action.type === 'SET_APPEARANCE') {
        ChatLog.setPlayerName(action.playerName);
        this.player.updateAppearance(action.shirtColor, action.skinColor);
      }
      this.network.sendActions([action]);
    };

    this.input.setDispatch(dispatch);

    this.ui = new GameUI(dispatch);
    this.ui.setContextInfo(this.contextInfo);

    this.network = new NetworkClient();

    // ---- Login UI — shown before the WS connects ----
    this.loginUI = new LoginUI(AUTH_URL);
    this.loginUI.show();
    this.loginUI.onAuth(({ token, username }) => {
      // Connect to game server with JWT; world renders once init fires
      this.network.connect(`${WS_URL}?token=${encodeURIComponent(token)}`);

      this.network.onInit((msg) => {
        this.localPlayerId = msg.playerId;
        this.input.setLocalPlayerId(msg.playerId);

        // Show appearance modal after login — pre-fill name with account username
        this.joinModal = new PlayerJoinModal(dispatch);
        this.joinModal.setDefaultName(username);
        this.joinModal.show();
        this.joinModal.onJoin((name, shirtColor, skinColor) => {
          ChatLog.setPlayerName(name);
          this.player.updateAppearance(shirtColor, skinColor);
          ChatLog.log(`${name} has joined the server. Welcome!`);
        });
      });

      this.network.onState((msg) => {
        this.prevState = this.currentState;
        this.currentState = { ...msg, world: worldState };
        this.lastServerTickTime = performance.now();

        this.syncDepletedTrees(msg.depletedTrees ?? {});

        const localId = this.localPlayerId;
        if (!localId) return;

        const msgs = msg.messages[localId] ?? [];
        for (const m of msgs) {
          if (m.startsWith('chat:')) ChatLog.chat(m.slice(5));
          else ChatLog.log(m);
        }

        this.ui.update(this.currentState, localId);
      });
    });

    this.setupResize(canvas);
    this.startRenderLoop();
  }

  private setupLights(): void {
    const ambient = new HemisphericLight('ambient', new Vector3(0, 1, 0), this.scene);
    ambient.intensity   = 0.65;
    ambient.diffuse     = new Color3(1, 0.97, 0.9);
    ambient.groundColor = new Color3(0.3, 0.4, 0.2);

    const sun = new DirectionalLight('sun', new Vector3(-1, -2, -1), this.scene);
    sun.intensity = 0.8;
    sun.diffuse   = new Color3(1, 0.95, 0.8);
    sun.specular  = new Color3(0.1, 0.1, 0.05);
  }

  private buildIndicator(name: string, color: Color3, yOffset: number): Mesh {
    const mesh = MeshBuilder.CreateGround(`${name}-indicator`, { width: 0.9, height: 0.9 }, this.scene);
    mesh.position.y = yOffset;
    mesh.isPickable = false;

    const mat = new StandardMaterial(`${name}-mat`, this.scene);
    mat.diffuseColor  = color;
    mat.emissiveColor = color.scale(0.4);
    mat.alpha = 0.6;
    mesh.material = mat;
    mesh.setEnabled(false);
    return mesh;
  }

  private startRenderLoop(): void {
    let lastTime = performance.now();

    this.engine.runRenderLoop(() => {
      const now = performance.now();
      const dt  = Math.min((now - lastTime) / 1000, 0.1);
      lastTime  = now;

      const localId = this.localPlayerId;
      const alpha = Math.min(1, (now - this.lastServerTickTime) / TICK_DURATION_MS);

      const currPlayer = localId ? this.currentState.players[localId] : undefined;
      const prevPlayer = localId ? (this.prevState.players[localId] ?? currPlayer) : undefined;

      if (currPlayer && prevPlayer) {
        this.player.render(prevPlayer, currPlayer, alpha, this.currentState.tick);
        this.player.updateEquipped(currPlayer.equipped);
        this.player.updateAppearance(currPlayer.shirtColor ?? 'blue', currPlayer.skinColor ?? 'fair');
        this.camera.update(dt, this.input.heldKeys, this.player.worldPosition);
      } else {
        this.camera.update(dt, this.input.heldKeys, new Vector3(PLAYER_START_X, 0, PLAYER_START_Y));
      }

      // Per-tick events — fire once when a new server tick arrives
      if (this.currentState.tick > this.lastHitTick) {
        this.lastHitTick = this.currentState.tick;

        for (const curr of this.currentState.npcs) {
          const prev = this.prevState.npcs.find(n => n.id === curr.id);
          if (prev && curr.lastHitTick > (prev.lastHitTick ?? 0)) {
            this.hitSplatManager.spawn(curr.lastHitDamage, curr.tileX, curr.tileY, 0.9);
            this.healthBarManager.recordHit(curr.id);
          }
        }

        if (currPlayer && prevPlayer) {
          if (currPlayer.lastHitTick > prevPlayer.lastHitTick) {
            this.hitSplatManager.spawn(currPlayer.lastHitDamage, currPlayer.tileX, currPlayer.tileY, 1.2);
            this.soundEngine.playHit();
          }
          if (currPlayer.lastAttackTick > prevPlayer.lastAttackTick) {
            this.soundEngine.playStrike();
            this.player.triggerLunge();
          }
          if (currPlayer.lastChopTick > prevPlayer.lastChopTick) {
            this.player.triggerLunge();
          }
          const SLOTS = ['head','neck','body','legs','feet','hands','ring','leftHand','rightHand','ammo'] as const;
          for (const slot of SLOTS) {
            const wasEquipped = !!prevPlayer.equipped[slot];
            const isEquipped  = !!currPlayer.equipped[slot];
            if (!wasEquipped && isEquipped)  this.soundEngine.playEquip();
            if (wasEquipped  && !isEquipped) this.soundEngine.playUnequip();
          }
        }
      }

      this.hitSplatManager.update();
      this.healthBarManager.update(this.currentState.npcs);
      this.clickFeedback.update();

      if (currPlayer) {
        const playerPos = this.player.worldPosition;
        this.overheadChat.update(
          currPlayer.chatMessage,
          currPlayer.chatMessageTick,
          playerPos.x,
          1.05,
          playerPos.z,
          this.currentState.tick,
        );
      }

      this.input.setPlayers(this.currentState.players);

      // Remote players
      this.syncRemotePlayerEntities(localId);
      for (const [id, entity] of this.remotePlayerEntities) {
        const curr = this.currentState.players[id];
        const prev = this.prevState.players[id] ?? curr;
        if (curr) {
          entity.render(prev, curr, alpha, this.currentState.tick);
          entity.updateEquipped(curr.equipped);
          entity.updateAppearance(curr.shirtColor ?? 'blue', curr.skinColor ?? 'fair');
          this.remoteOverheadChats.get(id)?.update(
            curr.chatMessage, curr.chatMessageTick,
            curr.tileX, 1.05, curr.tileY,
            this.currentState.tick,
          );
        }
      }

      // NPCs
      this.syncNPCEntities(this.currentState.npcs);
      for (const npc of this.currentState.npcs) {
        const prevNPC = this.prevState.npcs.find(n => n.id === npc.id);
        this.npcEntities.get(npc.id)?.render(prevNPC ?? npc, npc, alpha, this.currentState.tick);
      }
      this.input.setNPCs(this.currentState.npcs);

      this.syncDroppedItemEntities(this.currentState.droppedItems);
      this.input.setDroppedItems(this.currentState.droppedItems);

      const hover = this.input.currentHover;
      if (hover.kind === 'walkable') {
        this.hoverIndicator.setEnabled(true);
        this.hoverIndicator.position.x = hover.tileX;
        this.hoverIndicator.position.z = hover.tileY;
      } else {
        this.hoverIndicator.setEnabled(false);
      }

      if (currPlayer) {
        const hasPath = currPlayer.path.length > 0
          || currPlayer.destinationX !== currPlayer.tileX
          || currPlayer.destinationY !== currPlayer.tileY;
        if (hasPath) {
          this.destIndicator.setEnabled(true);
          this.destIndicator.position.x = currPlayer.destinationX;
          this.destIndicator.position.z = currPlayer.destinationY;

          this.destPulseScale += this.destPulseDir * dt * 2;
          if (this.destPulseScale > 1.15) { this.destPulseScale = 1.15; this.destPulseDir = -1; }
          if (this.destPulseScale < 0.85) { this.destPulseScale = 0.85; this.destPulseDir =  1; }
          this.destIndicator.scaling.x = this.destPulseScale;
          this.destIndicator.scaling.z = this.destPulseScale;
        } else {
          this.destIndicator.setEnabled(false);
        }
      }

      this.contextInfo.update(hover, this.currentState.npcs);
      this.updateHoverHighlight(hover);
      this.canvas.style.cursor = this.cursorFor(hover.kind);

      this.scene.render();
    });
  }

  private syncRemotePlayerEntities(localId: string | null): void {
    const liveIds = new Set(
      Object.keys(this.currentState.players).filter(id => id !== localId),
    );
    for (const [id, entity] of this.remotePlayerEntities) {
      if (!liveIds.has(id)) {
        entity.dispose();
        this.remotePlayerEntities.delete(id);
        this.remoteOverheadChats.get(id)?.dispose();
        this.remoteOverheadChats.delete(id);
      }
    }
    for (const id of liveIds) {
      if (!this.remotePlayerEntities.has(id)) {
        this.remotePlayerEntities.set(id, new PlayerEntity(this.scene, id));
        this.remoteOverheadChats.set(id, new OverheadChat(this.scene));
      }
    }
  }

  private updateHoverHighlight(hover: HoverTarget): void {
    this.hoverHighlight.removeAllMeshes();
    if (hover.kind === 'none' || hover.kind === 'walkable') return;

    const cyan = new Color3(0, 0.9, 1);
    const tryAdd = (m: unknown) => {
      if (!(m instanceof AbstractMesh)) return;
      if ((m.visibility ?? 1) < 0.01) return;
      if (m instanceof Mesh) {
        if (m.getTotalVertices() === 0) return;
        this.hoverHighlight.addMesh(m, cyan);
      } else if (m instanceof InstancedMesh) {
        // InstancedMesh shares vertex data with its source; HighlightLayer supports it at runtime
        this.hoverHighlight.addMesh(m as unknown as Mesh, cyan);
      }
    };

    switch (hover.kind) {
      case 'player': return;
      case 'tree':
        tryAdd(this.scene.getMeshByName(`tree-trunk-${hover.tileX}-${hover.tileY}`));
        tryAdd(this.scene.getMeshByName(`tree-canopy-${hover.tileX}-${hover.tileY}`));
        break;
      case 'rock':
        tryAdd(this.scene.getMeshByName(`rock-${hover.tileX}-${hover.tileY}`));
        break;
      case 'npc': {
        if (!hover.npcId) break;
        const root = this.scene.getMeshByName(`npc-root-${hover.npcId}`);
        if (root) { tryAdd(root); root.getChildMeshes().forEach(tryAdd); }
        break;
      }
      case 'item': {
        if (!hover.droppedItemId) break;
        const root = this.scene.getMeshByName(`item-${hover.droppedItemId}`);
        if (root) { tryAdd(root); root.getChildMeshes().forEach(tryAdd); }
        break;
      }
    }
  }

  private cursorFor(kind: string): string {
    switch (kind) {
      case 'tree': case 'rock': case 'npc': case 'item': case 'player': return 'pointer';
      default: return 'default';
    }
  }

  private syncDroppedItemEntities(items: DroppedItemState[]): void {
    const liveIds = new Set(items.map(i => i.id));
    for (const [id, entity] of this.droppedItemEntities) {
      if (!liveIds.has(id)) { entity.dispose(); this.droppedItemEntities.delete(id); }
    }
    for (const item of items) {
      if (!this.droppedItemEntities.has(item.id)) {
        this.droppedItemEntities.set(item.id, new DroppedItemEntity(item.id, item.itemId, item.tileX, item.tileY, this.scene));
      }
    }
  }

  private syncNPCEntities(npcs: NPCState[]): void {
    const liveIds = new Set(npcs.map(n => n.id));
    for (const [id, entity] of this.npcEntities) {
      if (!liveIds.has(id)) { entity.dispose(); this.npcEntities.delete(id); }
    }
    for (const npc of npcs) {
      if (!this.npcEntities.has(npc.id)) {
        this.npcEntities.set(npc.id, new NPCEntity(npc.id, npc.kind, this.scene));
      }
    }
  }

  private syncDepletedTrees(depletedTrees: Record<string, number>): void {
    const currentKeys = new Set(Object.keys(depletedTrees));

    // Hide newly depleted trees and spawn stumps
    for (const key of currentKeys) {
      if (!this.stumpMeshes.has(key)) {
        const [xs, ys] = key.split('-');
        const x = parseInt(xs, 10);
        const y = parseInt(ys, 10);

        this.scene.getMeshByName(`tree-trunk-${x}-${y}`)?.setEnabled(false);
        this.scene.getMeshByName(`tree-canopy-${x}-${y}`)?.setEnabled(false);

        // Spawn stump — short cylinder matching trunk diameter
        const stump = MeshBuilder.CreateCylinder(`stump-${x}-${y}`, {
          diameter: 0.18, height: 0.12, tessellation: 6,
        }, this.scene);
        stump.position.x = x;
        stump.position.y = 0.06;
        stump.position.z = y;
        stump.isPickable = false;
        const stumpMat = new StandardMaterial(`stump-mat-${key}`, this.scene);
        stumpMat.diffuseColor = new Color3(0.26, 0.13, 0.04);
        stump.material = stumpMat;
        stump.convertToFlatShadedMesh();

        this.stumpMeshes.set(key, stump);
      }
    }

    // Restore respawned trees and remove stumps
    for (const [key, stump] of this.stumpMeshes) {
      if (!currentKeys.has(key)) {
        const [xs, ys] = key.split('-');
        const x = parseInt(xs, 10);
        const y = parseInt(ys, 10);

        this.scene.getMeshByName(`tree-trunk-${x}-${y}`)?.setEnabled(true);
        this.scene.getMeshByName(`tree-canopy-${x}-${y}`)?.setEnabled(true);

        stump.material?.dispose();
        stump.dispose();
        this.stumpMeshes.delete(key);
      }
    }
  }

  private setupResize(canvas: HTMLCanvasElement): void {
    new ResizeObserver(() => this.engine.resize()).observe(canvas);
    window.addEventListener('resize', () => this.engine.resize());
  }

  dispose(): void {
    this.network.disconnect();
    this.engine.dispose();
  }
}
