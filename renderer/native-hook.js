// RAWDAW Desktop — additive native-core hook.
//
// This file changes NO visible UI and adds NO controls. When the app runs under
// Electron the preload exposes window.RawCore; this hook then routes the heavy
// BPM number through the C++ core (the user's shipped BeatDetektor + an
// onset-autocorrelation estimator) while leaving every other behaviour — the
// transient/warp grid, caching, octave folding range — byte-identical to the
// original JS path. In a plain browser (no window.RawCore) this file is inert
// and the original JS detector runs unchanged.
(function () {
  'use strict';
  if (typeof window === 'undefined') return;
  if (!window.RawCore || !window.RawCore.detectBpm) return;       // browser: no-op
  if (typeof window._bdAnalyze !== 'function') return;            // guard: API moved

  const jsAnalyze = window._bdAnalyze;

  window._bdAnalyze = async function (buf) {
    // Keep the original JS analysis: it produces the transients that drive the
    // warp-marker grid, and serves as a guaranteed fallback.
    const res = await jsAnalyze.call(this, buf);

    try {
      const sr = buf.sampleRate;
      const len = Math.min(buf.length, Math.floor(Math.min(buf.duration, 120) * sr));
      const channels = [];
      for (let c = 0; c < buf.numberOfChannels; c++) {
        // copy (not subarray) so the buffer survives structured-clone to main
        channels.push(buf.getChannelData(c).slice(0, len));
      }
      const nat = await window.RawCore.detectBpm(channels, sr, { bpmMin: 80, bpmMax: 180 });
      if (nat && nat.bpm > 0) {
        // Fold into the exact octave window the original returns (55..200).
        let b = nat.bpm;
        while (b >= 200) b /= 2;
        while (b < 55) b *= 2;
        res.bpm = Math.round(b * 10) / 10;
      }
    } catch (e) {
      // Any failure (ABI mismatch, IPC error) silently keeps the JS result.
      console.warn('[rawcore] native BPM unavailable, using JS result:', e && e.message);
    }
    return res;
  };

  console.info('[rawcore] native BPM detection active');
})();
