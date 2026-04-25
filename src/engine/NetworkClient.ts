import type { GameAction, ServerStatePatch } from '../shared/types';

export interface InitMessage {
  type: 'init';
  playerId: string;
  worldSeed: number;
}

export interface StateMessage {
  type: 'state';
  tick: number;
  players: ServerStatePatch['players'];
  npcs: ServerStatePatch['npcs'];
  droppedItems: ServerStatePatch['droppedItems'];
  pendingRespawns: ServerStatePatch['pendingRespawns'];
  messages: ServerStatePatch['messages'];
  depletedTrees: ServerStatePatch['depletedTrees'];
  treeHealth: ServerStatePatch['treeHealth'];
}

export class NetworkClient {
  private ws: WebSocket | null = null;
  private initCallback: ((msg: InitMessage) => void) | null = null;
  private stateCallback: ((msg: StateMessage) => void) | null = null;

  connect(url: string): void {
    this.ws = new WebSocket(url);

    this.ws.addEventListener('message', (event) => {
      let msg: InitMessage | StateMessage;
      try {
        msg = JSON.parse(event.data as string) as InitMessage | StateMessage;
      } catch {
        return;
      }

      if (msg.type === 'init') {
        this.initCallback?.(msg);
      } else if (msg.type === 'state') {
        this.stateCallback?.(msg);
      }
    });
  }

  // Sends immediately — no batching, no flush interval
  sendActions(actions: GameAction[]): void {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
    this.ws.send(JSON.stringify({ type: 'actions', actions }));
  }

  onInit(cb: (msg: InitMessage) => void): void {
    this.initCallback = cb;
  }

  onState(cb: (msg: StateMessage) => void): void {
    this.stateCallback = cb;
  }

  disconnect(): void {
    this.ws?.close();
    this.ws = null;
  }
}
