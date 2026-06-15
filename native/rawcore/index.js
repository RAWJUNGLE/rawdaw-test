// Loads the compiled native addon and re-exports a small, stable JS surface.
// The Electron main process requires this; the renderer never touches it
// directly (it goes through preload's contextBridge).
'use strict';

let native;
try {
  native = require('./build/Release/rawcore.node');
} catch (e) {
  try {
    native = require('./build/Debug/rawcore.node');
  } catch (e2) {
    throw new Error(
      'rawcore native addon not built. Run `npm run rebuild` in native/rawcore ' +
      '(or `npx @electron/rebuild` when targeting Electron). Original error: ' +
      e.message
    );
  }
}

module.exports = {
  // detectBpm(channels, sampleRate, opts?) -> {bpm,bpmRaw,confidence,beats[]}
  detectBpm: native.detectBpm,
  // stretchOffline(channels, opts) -> {channels: Float32Array[], frames}
  stretchOffline: native.stretchOffline,
};
