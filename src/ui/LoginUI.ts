export interface AuthResult {
  token: string;
  playerId: string;
  username: string;
}

export class LoginUI {
  private readonly overlay: HTMLElement;
  private authCallback: ((result: AuthResult) => void) | null = null;

  private mode: 'login' | 'register' = 'login';
  private readonly authUrl: string;

  // Shared field refs
  private usernameInput!: HTMLInputElement;
  private passwordInput!: HTMLInputElement;
  private submitBtn!: HTMLButtonElement;
  private errorEl!: HTMLElement;
  private titleEl!: HTMLElement;
  private toggleEl!: HTMLElement;

  constructor(authUrl: string) {
    this.authUrl = authUrl;
    this.overlay = this.buildOverlay();
    document.body.appendChild(this.overlay);
  }

  show(): void {
    this.overlay.style.display = 'flex';
    setTimeout(() => this.usernameInput.focus(), 50);
  }

  hide(): void {
    this.overlay.style.display = 'none';
  }

  onAuth(cb: (result: AuthResult) => void): void {
    this.authCallback = cb;
  }

  private setMode(mode: 'login' | 'register'): void {
    this.mode = mode;
    this.titleEl.textContent  = mode === 'login' ? 'Login' : 'Create Account';
    this.submitBtn.textContent = mode === 'login' ? 'Login' : 'Register';
    this.toggleEl.innerHTML = mode === 'login'
      ? 'New player? <span style="color:#ff981f;cursor:pointer" id="toggle-link">Create account</span>'
      : 'Have an account? <span style="color:#ff981f;cursor:pointer" id="toggle-link">Login</span>';
    this.overlay.querySelector('#toggle-link')?.addEventListener('click', () =>
      this.setMode(mode === 'login' ? 'register' : 'login'),
    );
    this.clearError();
  }

  private showError(msg: string): void {
    this.errorEl.textContent = msg;
    this.errorEl.style.display = 'block';
  }

  private clearError(): void {
    this.errorEl.textContent = '';
    this.errorEl.style.display = 'none';
  }

  private async submit(): Promise<void> {
    const username = this.usernameInput.value.trim();
    const password = this.passwordInput.value;

    if (!username || !password) {
      this.showError('Please enter a username and password.');
      return;
    }

    this.submitBtn.disabled = true;
    this.submitBtn.textContent = '…';
    this.clearError();

    try {
      const endpoint = this.mode === 'login' ? '/login' : '/register';
      const res = await fetch(`${this.authUrl}${endpoint}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ username, password }),
      });
      const body = await res.json() as { token?: string; playerId?: string; username?: string; error?: string };

      if (!res.ok || !body.token || !body.playerId || !body.username) {
        this.showError(body.error ?? 'Login failed. Please try again.');
        return;
      }

      this.hide();
      this.authCallback?.({ token: body.token, playerId: body.playerId, username: body.username });
    } catch {
      this.showError('Could not connect to server. Is it running?');
    } finally {
      this.submitBtn.disabled = false;
      this.submitBtn.textContent = this.mode === 'login' ? 'Login' : 'Register';
    }
  }

  private buildOverlay(): HTMLElement {
    const overlay = document.createElement('div');
    overlay.style.cssText = `
      position: fixed; inset: 0; z-index: 9000;
      background: rgba(0,0,0,0.80);
      display: flex; align-items: center; justify-content: center;
    `;

    const dialog = document.createElement('div');
    dialog.style.cssText = `
      background: #1a0d00;
      border: 1px solid #8b6c3e;
      border-radius: 4px;
      padding: 24px 28px 20px;
      width: 260px;
      font-family: 'Segoe UI', system-ui, sans-serif;
      color: #c8a060;
      box-shadow: 0 8px 40px rgba(0,0,0,0.95);
      user-select: none;
    `;

    // Logo / game name
    const logo = document.createElement('div');
    logo.style.cssText = `
      text-align: center; margin-bottom: 18px;
      font-size: 20px; font-weight: 900; letter-spacing: 2px;
      color: #ff981f; text-transform: uppercase;
    `;
    logo.textContent = 'OSRS Prototype';
    dialog.appendChild(logo);

    // Title (Login / Create Account)
    this.titleEl = document.createElement('div');
    this.titleEl.style.cssText = `
      font-size: 11px; font-weight: 700; text-transform: uppercase;
      letter-spacing: 1.2px; color: #c8a060;
      margin-bottom: 14px; text-align: center;
    `;
    dialog.appendChild(this.titleEl);

    // Username
    dialog.appendChild(this.buildLabel('Username'));
    this.usernameInput = this.buildInput('text', 20, 'username');
    dialog.appendChild(this.usernameInput);

    // Password
    dialog.appendChild(this.buildLabel('Password'));
    this.passwordInput = this.buildInput('password', 64, 'current-password');
    this.passwordInput.style.marginBottom = '0';
    dialog.appendChild(this.passwordInput);

    // Error message
    this.errorEl = document.createElement('div');
    this.errorEl.style.cssText = `
      color: #ff4444; font-size: 11px; margin-top: 8px;
      min-height: 14px; display: none;
    `;
    dialog.appendChild(this.errorEl);

    // Submit button
    this.submitBtn = document.createElement('button');
    this.submitBtn.style.cssText = `
      margin-top: 14px; width: 100%;
      background: #3d2010; color: #ff981f;
      border: 1px solid #8b6c3e; border-radius: 3px;
      padding: 8px 0; font-size: 13px; font-weight: 700;
      font-family: inherit; cursor: pointer;
      text-transform: uppercase; letter-spacing: 1px;
    `;
    this.submitBtn.addEventListener('mouseenter', () => {
      if (!this.submitBtn.disabled) this.submitBtn.style.background = '#5a3020';
    });
    this.submitBtn.addEventListener('mouseleave', () => {
      this.submitBtn.style.background = '#3d2010';
    });
    this.submitBtn.addEventListener('click', () => void this.submit());
    dialog.appendChild(this.submitBtn);

    // Toggle link
    this.toggleEl = document.createElement('div');
    this.toggleEl.style.cssText = `
      margin-top: 12px; font-size: 11px; text-align: center; color: #8b6c3e;
    `;
    dialog.appendChild(this.toggleEl);

    overlay.appendChild(dialog);

    // Keyboard submit
    overlay.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); void this.submit(); }
    });

    // Initialize to login mode
    this.setMode('login');

    return overlay;
  }

  private buildLabel(text: string): HTMLElement {
    const el = document.createElement('div');
    el.style.cssText = `
      font-size: 10px; font-weight: 700; text-transform: uppercase;
      letter-spacing: 1px; color: #c8a060; margin-bottom: 5px;
    `;
    el.textContent = text;
    return el;
  }

  private buildInput(type: string, maxLength: number, autocomplete: string): HTMLInputElement {
    const el = document.createElement('input');
    el.type = type;
    el.maxLength = maxLength;
    el.setAttribute('autocomplete', autocomplete);
    el.style.cssText = `
      width: 100%; box-sizing: border-box;
      background: #0d0600; color: #ffffff;
      border: 1px solid rgba(61,32,16,0.70); border-radius: 2px;
      padding: 6px 8px; font-size: 12px; font-family: inherit;
      outline: none; margin-bottom: 12px;
    `;
    el.addEventListener('focus', () => { el.style.borderColor = '#ff981f'; });
    el.addEventListener('blur',  () => { el.style.borderColor = 'rgba(61,32,16,0.70)'; });
    return el;
  }
}
