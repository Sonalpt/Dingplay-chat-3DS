# Dingplay Chat — 3DS / 2DS client

Standalone foreground homebrew (libctru + citro2d) for every model in the 3DS
family. Old 3DS / 2DS is the baseline (268 MHz, ~64 MB); New 3DS gets the same
build with the 804 MHz clock enabled.

```
client/
  Makefile              devkitPro build; fonts are generated from ../fonts at build time
  source/
    main.c              services, navigation stack (B = back), toasts, frame loop
    app.h               data model (Message / Drawing / Friend / Room), settings, screen table
    theme.h             design tokens from the mockups
    ui.c/h              flat toolkit: hard shadows, rounded rects, text, icons, dot grid
    i18n.c/h            EN / FR string table (copy taken verbatim from the two mockups)
    net.c/h             httpc on a worker thread → callbacks on the main thread
    api.c/h             relay API client + model updates + SD cache hooks
    store.c/h           sdmc:/3ds/dingplay-chat/ (config.ini, session.tok, cache/*.json)
    avatar.c/h          RGBA avatars from the relay → tiled GPU textures (LRU of 20)
    draw.c/h            stylus → vector stroke list; rendering of drawings
    voice.c/h           mic → IMA-ADPCM on the fly (DPV1), ndsp playback, notification blip
    adpcm.c/h           codec (twin of relay/src/lib/adpcm.js)
    kbd.c/h             swkbd wrapper
    mii.c/h             owner's Mii from system config + registry of peers' Miis (avatar keys "mii:<hash>")
    local/udsnet.c/h    Local Wireless: host / join / approve / mute / kick, chunked draw + voice
    screens/            one file per mockup screen (01 boot … 09 settings) + common.c (chat log, voice bubble, record panel)
  gfx/logo.t3s          launcher icon → boot splash texture (tex3ds)
  tools/app.rsf         makerom spec for the .cia
  tools/hostcheck/      clang syntax check against cloned devkitPro headers (no devkitARM needed)
```

## Build

1. Install devkitPro and the 3DS tools: https://devkitpro.org/wiki/Getting_Started
   then `dkp-pacman -S 3ds-dev` (brings devkitARM, libctru, citro3d, citro2d, tex3ds, mkbcfnt).
2. `export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM`
3. `make` → `dingplay-chat.3dsx` + `.smdh` (copy to `sdmc:/3ds/` and launch from the Homebrew Launcher).
4. Optional `make cia` needs `bannertool` + `makerom` on PATH and a `banner.png` (256×128) + `banner.wav`.

The Makefile converts `../fonts/Nunito/static/Nunito-Regular.ttf` (body) and `Nunito-Bold.ttf`
(headings) to BCFNT with `mkbcfnt` at two sizes — 12 pt for small UI text, 24 pt for headings —
so small text is drawn near 1:1 and stays crisp; if a file is missing at runtime the app falls
back to the other size, then to the system font.

## On the console

- `sdmc:/3ds/dingplay-chat/config.ini` — `relay=http://host:port` (defaults to
  `http://192.168.1.10:8080`), sync interval, language, toggles. Editable in-app: the boot
  screen offers "Set relay address" when the relay can't be reached; Settings → SELECT.
- `sdmc:/3ds/dspfirm.cdc` — DSP firmware dump, needed for **voice playback** (recording and
  sending work without it). Dump it once with DSP1 or any homebrew that offers the dump.
- Wi-Fi must be connected for Online mode. Local Wireless works with no account and no Wi-Fi.

## Controls (as in the mockups)

| button | where | action |
|---|---|---|
| B | everywhere | back one level (leave room in a local chatroom) |
| Y | online chat | refresh now (forces a poll) |
| Y | local chatroom | open the lobby while staying connected |
| L / R | local chatroom · friends | cycle Type / Draw / Voice tabs · cycle tabs |
| X | local chatroom (host) | Manage room · online chat: Voice · friends: Add friend |
| ↑ / ↓ + A | chat screens | select a voice note in the log and play it (the top screen isn't touchable) |
| START | Home | quit to the Home Menu |

## Protocols

- **Relay**: JSON over plain HTTP, `Authorization: Bearer <token>` (see `relay/README.md`).
  Voice notes go up as raw `DPV1` bodies and come down the same way. Avatars arrive as raw RGBA.
- **Local Wireless**: UDS network with wlancommID `0x00D1A600`, passphrase
  `dingplay-chat:<passcode|open>` (a wrong passcode fails at the UDS layer). Beacon app-data
  is a 66-byte `DPC1` struct (slots, members, flags, room name, host name). Frames are
  `{u8 type, u8 flags, u16 len}` + payload; drawings and voice notes are split into 1400-byte
  chunks and unicast to each member (802.11 unicast is ACKed, broadcast is not). The host
  sends a heartbeat every second; guests derive "signal good / weak / lost" from its age.

## Deviations from the mockups worth knowing

- "Level 12" under the username: Dingplay has no level system, so the line is just `@username`.
- Avatars: a member with a Dingplay account shows their profile picture; without one, the
  console's own Mii (rendered by the relay through Mii Studio, so it needs Wi-Fi — offline
  Local Wireless shows the initial placeholder). In Local Wireless the uid + Mii travel in the
  HELLO / PROFILE frames so every console can look peers up.
- Emoji: Nunito (and the 3DS system font) has no colour emoji, so the Emoji button inserts
  text emoticons (`:)`, `:D`, `<3` …).
- Signal strength for nearby rooms comes from beacon presence across scans (a room missing
  from the latest scan shows "weak signal", gone after three) — libctru exposes no RSSI.
- Themed rooms live in a third tab (FRIENDS · REQUESTS · ROOMS) on screen 04, since the
  spec adds them after the mockups were drawn.
- The 16-slot option is exposed but unverified over UDS chat traffic; 8 is the default.
