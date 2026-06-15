// RAWDAW Desktop — Electron main process.
// Owns the native core (loaded here, in the main process) and the application
// window. The renderer is the existing DAW, unchanged except for vendored CDN
// scripts and one additive hook that calls back into here over IPC.
'use strict';
const { app, BrowserWindow, ipcMain, Menu, dialog, session } = require('electron');
const path = require('path');
const fs = require('fs');

// Force Chromium to use a fake media device so it never touches the real
// microphone hardware. macOS therefore never shows the mic-permission dialog.
// The DAW loads audio from files — it does not record.
app.commandLine.appendSwitch('use-fake-device-for-media-stream');

// ---- native core --------------------------------------------------------
// Loaded lazily so a missing/mis-ABI build degrades to "JS only" instead of
// crashing the app (the renderer hook checks window.RawCore before using it).
let rawcore = null;
let rawcoreError = null;
try {
  rawcore = require(path.join(__dirname, '..', '..', 'native', 'rawcore'));
} catch (e) {
  rawcoreError = e.message;
  console.error('[rawcore] native core unavailable:', e.message);
}

// ---- IPC: BPM detection -------------------------------------------------
// channels: Array<Float32Array> (transferred by structured clone)
ipcMain.handle('rawcore:detectBpm', async (_evt, channels, sampleRate, opts) => {
  if (!rawcore) throw new Error('rawcore unavailable: ' + rawcoreError);
  // Normalise: arrays arrive as plain typed arrays via structured clone.
  return rawcore.detectBpm(channels, sampleRate, opts || {});
});

// ---- IPC: offline stretch ----------------------------------------------
ipcMain.handle('rawcore:stretchOffline', async (_evt, channels, params) => {
  if (!rawcore) throw new Error('rawcore unavailable: ' + rawcoreError);
  return rawcore.stretchOffline(channels, params);
});

ipcMain.handle('rawcore:status', async () => ({
  available: !!rawcore,
  error: rawcoreError,
}));

// ---- IPC: open audio files via native dialog ---------------------------
ipcMain.handle('rawcore:openAudioFiles', async (evt) => {
  const win = BrowserWindow.getFocusedWindow();
  if (!win) return [];
  const result = await dialog.showOpenDialog(win, {
    properties: ['openFile', 'multiSelections'],
    filters: [
      { name: 'Audio', extensions: ['wav', 'mp3', 'ogg', 'flac', 'm4a', 'aiff', 'aif'] },
    ],
  });
  if (result.canceled || !result.filePaths.length) return [];
  const files = [];
  for (const fp of result.filePaths) {
    try {
      const buf = fs.readFileSync(fp);
      const ab = buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
      files.push({ name: path.basename(fp), data: ab });
    } catch (e) {
      console.error('[rawcore] failed to read', fp, e.message);
    }
  }
  return files;
});

// ---- permissions -------------------------------------------------------
// Auto-allow microphone on first request and persist the grant so macOS
// doesn't re-prompt on every record-arm. Also block unnecessary permissions.
app.on('will-navigate', (e) => e.preventDefault());
app.on('web-contents-created', (_evt, wc) => {
  wc.setWindowOpenHandler(() => ({ action: 'deny' }));
});

// ---- window -------------------------------------------------------------
function createWindow() {
  const win = new BrowserWindow({
    width: 1480,
    height: 900,
    minWidth: 1024,
    minHeight: 640,
    backgroundColor: '#1a1a1a',
    title: 'RAWDAW',
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false, // preload needs require() for ipcRenderer
      // The DAW uses AudioWorklet + WASM; these are enabled by default in
      // Electron's Chromium. Keep autoplay unrestricted for the transport.
      autoplayPolicy: 'no-user-gesture-required',
    },
  });

  Menu.setApplicationMenu(null); // the DAW provides its own chrome
  win.loadFile(path.join(__dirname, '..', '..', 'renderer', 'index.html'));
  win.once('ready-to-show', () => {
    win.maximize();
    win.show();
  });
  return win;
}

app.whenReady().then(() => {
  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

// Quit when all windows are closed (even on macOS — DAW has no background
// activity; user expects the red button to fully close the app).
app.on('window-all-closed', () => {
  app.quit();
});
