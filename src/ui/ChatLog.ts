import type { GameAction } from '../shared/types';

export class ChatLog {
  private static instance: ChatLog | null = null;
  private static dispatchFn: ((action: GameAction) => void) | null = null;
  private static playerNameValue = 'Player';

  static init(dispatch: (action: GameAction) => void): void {
    ChatLog.dispatchFn = dispatch;
    if (!ChatLog.instance) ChatLog.instance = new ChatLog();
  }

  static setPlayerName(name: string): void {
    ChatLog.playerNameValue = name;
    ChatLog.instance?.updateInputLine();
  }

  static log(message: string): void {
    if (!ChatLog.instance) ChatLog.instance = new ChatLog();
    ChatLog.instance.push(message, '#ffe066');
  }

  static chat(message: string): void {
    if (!ChatLog.instance) ChatLog.instance = new ChatLog();
    ChatLog.instance.push(message, '#ffffff');
  }

  private readonly container: HTMLElement;
  private readonly messageArea: HTMLElement;
  private readonly spacer: HTMLElement;
  private readonly inputLine: HTMLElement;
  private buffer = '';

  private constructor() {
    this.container = document.createElement('div');
    this.container.style.cssText = `
      position: fixed;
      left: 12px;
      bottom: 12px;
      z-index: 40;
      width: 560px;
      height: 210px;
      display: flex;
      flex-direction: column;
      font-family: 'Segoe UI', system-ui, sans-serif;
      font-size: 13px;
      font-weight: 600;
      background: rgba(0, 0, 0, 0.55);
      border: 1px solid rgba(92, 58, 30, 0.70);
      border-radius: 4px;
      overflow: hidden;
    `;

    // Scrollable message area — spacer pushes messages to the bottom
    // when there aren't enough to fill the area
    this.messageArea = document.createElement('div');
    this.messageArea.style.cssText = `
      flex: 1;
      min-height: 0;
      overflow-y: auto;
      overflow-x: hidden;
      display: flex;
      flex-direction: column;
      padding: 6px 8px 4px;
      gap: 2px;
    `;

this.spacer = document.createElement('div');
    this.spacer.style.cssText = 'flex: 1;';
    this.messageArea.appendChild(this.spacer);

    this.container.appendChild(this.messageArea);

    const divider = document.createElement('div');
    divider.style.cssText = 'flex-shrink: 0; border-top: 1px solid rgba(255,255,255,0.60);';
    this.container.appendChild(divider);

    this.inputLine = document.createElement('div');
    this.inputLine.style.cssText = `
      flex-shrink: 0;
      color: #ffffff;
      padding: 4px 8px 6px;
      word-break: break-word;
      white-space: pre-wrap;
      text-shadow:
        1px  1px 2px rgba(0,0,0,0.95),
       -1px -1px 2px rgba(0,0,0,0.95),
        1px -1px 2px rgba(0,0,0,0.95),
       -1px  1px 2px rgba(0,0,0,0.95);
    `;
    this.container.appendChild(this.inputLine);

    document.body.appendChild(this.container);

    this.updateInputLine();
    this.setupKeyboard();
  }

  private setupKeyboard(): void {
    window.addEventListener('keydown', (e) => {
      // Never intercept input while a real text field has focus (modal name box, etc.)
      const tag = (document.activeElement?.tagName ?? '').toLowerCase();
      if (tag === 'input' || tag === 'textarea') return;

      if (e.key.startsWith('Arrow')) return;

      if (e.key === 'Backspace') {
        e.preventDefault();
        this.buffer = this.buffer.slice(0, -1);
        this.updateInputLine();
        return;
      }

      if (e.key === 'Enter') {
        e.preventDefault();
        if (this.buffer.trim().length > 0) {
          if (ChatLog.dispatchFn) {
            ChatLog.dispatchFn({ type: 'SEND_CHAT', message: this.buffer });
          }
          this.buffer = '';
          this.updateInputLine();
        }
        return;
      }

      if (e.key.length === 1) {
        this.buffer += e.key;
        this.updateInputLine();
      }
    });
  }

  private updateInputLine(): void {
    this.inputLine.textContent = `${ChatLog.playerNameValue}: ${this.buffer}`;
  }

  private push(message: string, color: string): void {
    const el = document.createElement('div');
    el.textContent = message;
    el.style.cssText = `
      color: ${color};
      line-height: 1.4;
      word-break: break-word;
      text-shadow:
        1px  1px 2px rgba(0,0,0,0.95),
       -1px -1px 2px rgba(0,0,0,0.95),
        1px -1px 2px rgba(0,0,0,0.95),
       -1px  1px 2px rgba(0,0,0,0.95);
    `;
    this.messageArea.appendChild(el);
    this.messageArea.scrollTop = this.messageArea.scrollHeight;
  }
}
