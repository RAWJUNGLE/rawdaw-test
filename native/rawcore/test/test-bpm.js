// Verifies the compiled native addon from Node (Linux/system-Node ABI).
// Generates kick click-tracks at known tempos and checks detectBpm, then
// exercises stretchOffline for uniform + pitch + marker modes.
'use strict';
const rawcore = require('..');

function clickTrack(bpm, seconds, sr) {
  const n = Math.floor(seconds * sr);
  const buf = new Float32Array(n);
  const secPerBeat = 60 / bpm;
  for (let t = 0; t < seconds; t += secPerBeat) {
    const start = Math.floor(t * sr);
    const dur = Math.floor(0.18 * sr);
    for (let i = 0; i < dur && start + i < n; i++) {
      const tt = i / sr;
      const clickEnv = Math.exp(-tt / 0.002);
      const bodyEnv = Math.exp(-tt / 0.045);
      const freq = 55 + 90 * bodyEnv;
      buf[start + i] += bodyEnv * Math.sin(2 * Math.PI * freq * tt) * 0.85 + clickEnv * 0.6;
    }
  }
  let peak = 1e-6;
  for (const v of buf) peak = Math.max(peak, Math.abs(v));
  for (let i = 0; i < n; i++) buf[i] /= peak;
  return buf;
}

let fails = 0;
console.log('--- detectBpm ---');
for (const sr of [44100, 48000]) {
  for (const bpm of [90, 100, 120, 128, 140, 174]) {
    const mono = clickTrack(bpm, 16, sr);
    const r = rawcore.detectBpm([mono], sr, { bpmMin: 80, bpmMax: 180 });
    const cands = [r.bpm, r.bpm * 2, r.bpm / 2];
    const err = Math.min(...cands.map((c) => Math.abs(c - bpm)));
    const ok = err <= 2;
    if (!ok) fails++;
    console.log(
      `  sr=${sr} truth=${bpm} bpm=${r.bpm.toFixed(2)} ` +
      `raw=${r.bpmRaw.toFixed(1)} conf=${r.confidence.toFixed(2)} ` +
      `beats=${r.beats.length} -> ${ok ? 'OK' : 'FAIL'}`
    );
  }
}

console.log('--- stretchOffline ---');
const sr = 44100;
const n = sr * 2;
const a = new Float32Array(n);
for (let i = 0; i < n; i++) a[i] = 0.5 * Math.sin(2 * Math.PI * 220 * (i / sr));

const u = rawcore.stretchOffline([a, a], { sampleRate: sr, timeRatio: 1.5, semitones: 0 });
const uOk = Math.abs(u.frames - n * 1.5) < 2 && u.channels.length === 2;
if (!uOk) fails++;
console.log(`  uniform 1.5x: frames=${u.frames} chans=${u.channels.length} -> ${uOk ? 'OK' : 'FAIL'}`);

const p = rawcore.stretchOffline([a, a], { sampleRate: sr, timeRatio: 1.0, semitones: 5 });
const pOk = p.frames === n;
if (!pOk) fails++;
console.log(`  pitch +5st: frames=${p.frames} -> ${pOk ? 'OK' : 'FAIL'}`);

const m = rawcore.stretchOffline([a, a], {
  sampleRate: sr, secondsPerBeat: 0.5,
  markers: [{ inputBeat: 0, outputBeat: 0 }, { inputBeat: 2, outputBeat: 3 }, { inputBeat: 4, outputBeat: 4 }],
});
const mOk = m.frames > 0 && m.channels.length === 2;
if (!mOk) fails++;
console.log(`  markers: frames=${m.frames} chans=${m.channels.length} -> ${mOk ? 'OK' : 'FAIL'}`);

console.log(fails === 0 ? '\nALL PASSED' : `\n${fails} FAILED`);
process.exit(fails === 0 ? 0 : 1);
