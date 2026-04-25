export class SoundEngine {
  private ctx: AudioContext;

  constructor() {
    this.ctx = new AudioContext();
    // Resume on first user interaction (browser autoplay policy)
    const resume = () => this.ctx.resume();
    document.addEventListener('pointerdown', resume, { once: true });
    document.addEventListener('keydown',     resume, { once: true });
  }

  playHit(): void {
    const t = this.ctx.currentTime;
    this.noise(t, 0.08, 250, 'lowpass',  0.35);
    this.sine(t, 120, 40, 0.08, 0.45, 0.10);
  }

  playStrike(): void {
    const t = this.ctx.currentTime;
    this.sine(t, 700, 250, 0.05, 0.28, 0.07);
    this.noise(t, 0.04, 2000, 'highpass', 0.13);
  }

  playEquip(): void {
    const t = this.ctx.currentTime;
    this.sine(t,        1100, 800, 0.10, 0.22, 0.18);
    this.sine(t + 0.05,  900, 650, 0.08, 0.15, 0.15);
  }

  playUnequip(): void {
    const t = this.ctx.currentTime;
    this.sine(t, 650, 450, 0.08, 0.18, 0.16);
  }

  private sine(
    startTime: number,
    freqStart: number, freqEnd: number, freqDecayDuration: number,
    gainStart: number, gainDecayDuration: number,
  ): void {
    const osc  = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(freqStart, startTime);
    osc.frequency.exponentialRampToValueAtTime(freqEnd, startTime + freqDecayDuration);
    gain.gain.setValueAtTime(gainStart, startTime);
    gain.gain.exponentialRampToValueAtTime(0.001, startTime + gainDecayDuration);
    osc.connect(gain);
    gain.connect(this.ctx.destination);
    osc.start(startTime);
    osc.stop(startTime + gainDecayDuration + 0.01);
  }

  private noise(
    startTime: number,
    duration: number,
    filterFreq: number,
    filterType: BiquadFilterType,
    gainStart: number,
  ): void {
    const bufLen = Math.ceil(this.ctx.sampleRate * duration);
    const buf    = this.ctx.createBuffer(1, bufLen, this.ctx.sampleRate);
    const data   = buf.getChannelData(0);
    for (let i = 0; i < bufLen; i++) data[i] = Math.random() * 2 - 1;

    const src    = this.ctx.createBufferSource();
    src.buffer   = buf;

    const filter = this.ctx.createBiquadFilter();
    filter.type  = filterType;
    filter.frequency.value = filterFreq;

    const gain = this.ctx.createGain();
    gain.gain.setValueAtTime(gainStart, startTime);
    gain.gain.exponentialRampToValueAtTime(0.001, startTime + duration);

    src.connect(filter);
    filter.connect(gain);
    gain.connect(this.ctx.destination);
    src.start(startTime);
    src.stop(startTime + duration + 0.01);
  }
}
