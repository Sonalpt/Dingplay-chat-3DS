#!/usr/bin/env node
'use strict';
// Converts the shared Dingplay assets (../assets) into what the 3DS client ships:
//   assets/icons/*.svg        → gfx/icons/<name>.png   64×64, white on transparent (tinted at draw time)
//   Logo_complete_without_bg  → gfx/wordmark.png       320 px wide
//   world-chat-background     → gfx/world-bg.png       400×240 cover crop (Global Room top screen)
//   dingplay-chat-3ds-icon    → icon.png               48×48 SMDH icon
// Run from client/:  node tools/assets.js   (needs `npm i @resvg/resvg-js jimp` somewhere on NODE_PATH)
const fs = require('fs');
const path = require('path');
const { Resvg } = require('@resvg/resvg-js');
const { Jimp } = require('jimp');

const ROOT = path.resolve(__dirname, '..');
const ASSETS = path.resolve(ROOT, '..', 'assets');
const ICON_SIZE = 64;

// UiIcon name → source SVG. Keep in sync with ICON_FILES in source/ui.c.
const ICONS = {
  global: 'global-communication.svg',
  friends: 'friends.svg',
  multiplayer: 'multiplayer.svg',
  user: 'user.svg',
  settings: 'settings.svg',
  send: 'send.svg',
  add: 'add.svg',
  scan: 'magnifying-glass.svg',
  'add-friend': 'add-friend.svg',
  close: 'close.svg',
  return: 'return.svg',
  image: 'image.svg',
  trash: 'trash.svg',
  home: 'home-icon-silhouette.svg',
  message: 'message.svg',
};

async function svgToWhitePng(src, out, size) {
  const svg = fs.readFileSync(src, 'utf8');
  const r = new Resvg(svg, { fitTo: { mode: 'width', value: size }, background: 'rgba(0,0,0,0)' });
  const png = r.render().asPng();
  const im = await Jimp.read(Buffer.from(png));
  // Everything becomes white; only the alpha channel of the artwork survives.
  const d = im.bitmap.data;
  for (let i = 0; i < d.length; i += 4) d[i] = d[i + 1] = d[i + 2] = 255;
  const canvas = new Jimp({ width: size, height: size, color: 0x00000000 });
  canvas.composite(im, Math.round((size - im.width) / 2), Math.round((size - im.height) / 2));
  await canvas.write(out);
}

async function main() {
  fs.mkdirSync(path.join(ROOT, 'gfx', 'icons'), { recursive: true });
  for (const [name, file] of Object.entries(ICONS)) {
    await svgToWhitePng(path.join(ASSETS, 'icons', file), path.join(ROOT, 'gfx', 'icons', `${name}.png`), ICON_SIZE);
    console.log('icon', name);
  }

  const wm = await Jimp.read(path.join(ASSETS, 'images', 'Logo_complete_without_bg.png'));
  wm.resize({ w: 320 });
  await wm.write(path.join(ROOT, 'gfx', 'wordmark.png'));
  console.log('wordmark', wm.width, wm.height);

  const bg = await Jimp.read(path.join(ASSETS, 'images', 'world-chat-background-container.png'));
  bg.cover({ w: 400, h: 240 });
  await bg.write(path.join(ROOT, 'gfx', 'world-bg.png'));
  console.log('world-bg', bg.width, bg.height);

  fs.copyFileSync(path.join(ASSETS, 'dingplay-chat-3ds-icon-48x48.png'), path.join(ROOT, 'icon.png'));
  console.log('icon.png');
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
