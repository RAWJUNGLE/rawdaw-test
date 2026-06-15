const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const OUT = path.resolve(__dirname, '..', 'build-resources');
const SIZE = 1024;

// Colors from the DAW theme
const BG       = [0x2b, 0x2b, 0x2b]; // panel bg
const ACCENT   = [0xff, 0x7a, 0x45]; // accent orange
const SHADOW   = [0x1a, 0x1a, 0x1a]; // darker shadow

function pointInTriangle(px, py, x1, y1, x2, y2, x3, y3) {
  const d1 = (px - x2) * (y1 - y2) - (x1 - x2) * (py - y2);
  const d2 = (px - x3) * (y2 - y3) - (x2 - x3) * (py - y3);
  const d3 = (px - x1) * (y3 - y1) - (x3 - x1) * (py - y1);
  const hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  const hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(hasNeg && hasPos);
}

function pointInCircle(px, py, cx, cy, r) {
  return (px - cx) ** 2 + (py - cy) ** 2 <= r * r;
}

console.log('Generating 1024x1024 icon...');

// PPM data: 3 bytes per pixel, R G B
const buf = Buffer.alloc(SIZE * SIZE * 3);
const H = SIZE / 2;

// Triangle vertices for a play-button shape (pointing right)
const tri = [
  [-250, -360],   // top-left
  [-250,  360],   // bottom-left
  [ 380,    0],   // right point
];

// Secondary shape: smaller parallelogram behind it
const para = [
  [-80, -200],
  [220, -200],
  [220,  200],
  [-80,  200],
];

// Anti-aliasing: 2x2 sub-pixel samples
for (let y = 0; y < SIZE; y++) {
  for (let x = 0; x < SIZE; x++) {
    const i = (y * SIZE + x) * 3;
    let color;

    // Sub-pixel sampling for anti-aliasing
    let inTriCount = 0;
    let inParaCount = 0;
    const sub = [-0.25, 0.25];
    for (const sy of sub) {
      for (const sx of sub) {
        const px = (x + sx) - H;
        const py = (y + sy) - H;
        if (pointInTriangle(px, py, ...tri.flat())) inTriCount++;
        if (pointInTriangle(px, py, ...para.flat())) inParaCount++;
      }
    }

    const isTri = inTriCount > 0;
    const isPara = inParaCount > 0;

    if (isTri || isPara) {
      // Gradient from accent to slightly darker
      const t = (y / SIZE);
      const r = Math.round(ACCENT[0] - t * 30);
      const g = Math.round(ACCENT[1] - t * 20);
      const b = Math.round(ACCENT[2] - t * 15);
      color = [r, g, b];
    } else {
      // Background with subtle gradient
      const t = (y / SIZE);
      color = [
        Math.round(BG[0] - t * 15),
        Math.round(BG[1] - t * 15),
        Math.round(BG[2] - t * 15),
      ];
    }

    buf[i]     = color[0];
    buf[i + 1] = color[1];
    buf[i + 2] = color[2];
  }
}

// Write PPM
const ppmPath = path.join(OUT, 'icon_raw.ppm');
const header = `P6\n${SIZE} ${SIZE}\n255\n`;
const ppmBuf = Buffer.alloc(header.length + buf.length);
ppmBuf.write(header);
buf.copy(ppmBuf, header.length);
fs.writeFileSync(ppmPath, ppmBuf);

// Convert to PNG via sips (built into macOS)
const pngPath = path.join(OUT, 'icon_raw.png');
console.log('Converting to PNG via sips...');
execSync(`sips -s format png "${ppmPath}" --out "${pngPath}"`, { stdio: 'inherit' });

// Create .iconset directory
const iconsetDir = path.join(OUT, 'RAWDAW.iconset');
if (fs.existsSync(iconsetDir)) {
  fs.rmSync(iconsetDir, { recursive: true });
}
fs.mkdirSync(iconsetDir, { recursive: true });

// Required sizes for a macOS icon
const sizes = [
  [16,  'icon_16x16.png'],
  [32,  'icon_16x16@2x.png'],
  [32,  'icon_32x32.png'],
  [64,  'icon_32x32@2x.png'],
  [128, 'icon_128x128.png'],
  [256, 'icon_128x128@2x.png'],
  [256, 'icon_256x256.png'],
  [512, 'icon_256x256@2x.png'],
  [512, 'icon_512x512.png'],
  [1024,'icon_512x512@2x.png'],
];

for (const [size, name] of sizes) {
  const outPath = path.join(iconsetDir, name);
  execSync(`sips -z ${size} ${size} "${pngPath}" --out "${outPath}"`, { stdio: 'inherit' });
}

// Generate .icns
console.log('Creating .icns with iconutil...');
const icnsPath = path.join(OUT, 'icon.icns');
execSync(`iconutil -c icns "${iconsetDir}" -o "${icnsPath}"`, { stdio: 'inherit' });

// Cleanup temp files
fs.unlinkSync(ppmPath);
fs.unlinkSync(pngPath);
fs.rmSync(iconsetDir, { recursive: true });

console.log('✓ Icon created:', icnsPath);
const stats = fs.statSync(icnsPath);
console.log('  Size:', (stats.size / 1024).toFixed(0), 'KB');
