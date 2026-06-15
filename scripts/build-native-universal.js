const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const MODULE_DIR = path.join(ROOT, 'native/rawcore');
const RELEASE_DIR = path.join(MODULE_DIR, 'build/Release');
const NATIVE_FILE = 'rawcore.node';
const ELECTRON_REBUILD = path.join(ROOT, 'node_modules/.bin/electron-rebuild');
const NODE_GYP = path.join(MODULE_DIR, 'node_modules/.bin/node-gyp');
const TMP_DIR = `/tmp/rawcore-universal-${Date.now()}`;
const TMP_ARM64 = path.join(TMP_DIR, 'rawcore-arm64.node');
const TMP_X64 = path.join(TMP_DIR, 'rawcore-x64.node');

const ELECTRON_VERSION = require(path.join(ROOT, 'node_modules/electron/package.json')).version;

function run(cmd, opts = {}) {
  const cwd = opts.cwd || MODULE_DIR;
  console.log(`\n> ${cmd}`);
  execSync(cmd, { stdio: 'inherit', cwd });
}

fs.mkdirSync(TMP_DIR, { recursive: true });

// Step 1: Build for arm64 using @electron/rebuild (v3)
console.log('\n=== [1/3] Building native addon for arm64 ===');
run(`${ELECTRON_REBUILD} -f -w rawcore --module-dir ${MODULE_DIR}`);
if (!fs.existsSync(path.join(RELEASE_DIR, NATIVE_FILE))) {
  throw new Error('arm64 build did not produce rawcore.node');
}
fs.copyFileSync(path.join(RELEASE_DIR, NATIVE_FILE), TMP_ARM64);
console.log('  ✓ arm64 .node saved');

// Step 2: Clean build dir, rebuild for x64 via node-gyp + Electron headers
console.log('\n=== [2/3] Building native addon for x64 ===');
run(`rm -rf ${path.join(MODULE_DIR, 'build')}`);
run(
  `${NODE_GYP} configure --arch=x64 --target=${ELECTRON_VERSION} --dist-url=https://electronjs.org/headers && ` +
  `${NODE_GYP} build --arch=x64`
);
if (!fs.existsSync(path.join(RELEASE_DIR, NATIVE_FILE))) {
  throw new Error('x64 build did not produce rawcore.node');
}
fs.copyFileSync(path.join(RELEASE_DIR, NATIVE_FILE), TMP_X64);
console.log('  ✓ x64 .node saved');

// Step 3: Merge into universal binary with lipo
console.log('\n=== [3/3] Merging into universal binary ===');
run(`lipo -create ${TMP_ARM64} ${TMP_X64} -output ${path.join(RELEASE_DIR, NATIVE_FILE)}`);

// Cleanup temp
run(`rm -rf ${TMP_DIR}`);

// Verify
console.log('\n=== Verification ===');
const lipoOut = execSync(`lipo -info ${path.join(RELEASE_DIR, NATIVE_FILE)}`).toString().trim();
console.log('  ' + lipoOut);
const hasBoth = lipoOut.includes('arm64') && (lipoOut.includes('x86_64') || lipoOut.includes('x64'));
if (!hasBoth) {
  console.error('  ✗ ERROR: binary is NOT universal!');
  process.exit(1);
}
console.log('  ✓ Universal native addon ready:', path.join(RELEASE_DIR, NATIVE_FILE));
