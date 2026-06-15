// RAWDAW Desktop — preload.
// Exposes a minimal, frozen window.RawCore surface to the renderer. Everything
// crosses the contextIsolation boundary through ipcRenderer.invoke; the renderer
// never sees the native addon or Node APIs directly.
'use strict';
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('RawCore', {
  // detectBpm(channels, sampleRate, opts?) -> Promise<{bpm,bpmRaw,confidence,beats}>
  // channels: Array<Float32Array> (one per channel) — structured-cloned to main.
  detectBpm: (channels, sampleRate, opts) =>
    ipcRenderer.invoke('rawcore:detectBpm', channels, sampleRate, opts),

  // stretchOffline(channels, params) -> Promise<{channels: Float32Array[], frames}>
  stretchOffline: (channels, params) =>
    ipcRenderer.invoke('rawcore:stretchOffline', channels, params),

  // openAudioFiles() -> Promise<Array<{name: string, data: ArrayBuffer}>>
  openAudioFiles: () =>
    ipcRenderer.invoke('rawcore:openAudioFiles'),

  // status() -> Promise<{available, error}>
  status: () => ipcRenderer.invoke('rawcore:status'),

  isNative: true,
});
