# Handoff — Dingplay Chat 3DS

Written for: Claude Code continuing this project on Rémy's PC (the machine with devkitPro installed).

This file is the exact state of the project at the end of the first session (2026-09-17, on a Mac
**without** devkitPro). Read it first, then `README.md`, `relay/README.md`, `client/README.md`,
and `dingplay-3ds-implementation-spec.md`. The two `*.dc.html` files are the visual source of
truth for every screen (`data-screen-label` marks 01 Boot → 09 Settings).

---

## 1. Last status message, verbatim

> Both halves are built. Here's where things stand.
>
> **What's in the repo now**
>
> `relay/` — Node.js relay (Fastify + firebase-admin) — runs, `npm test` passes 6/6, smoke-tested.
> I read the mobile app in `~/Desktop/dingconnect` and made the relay speak the **real** Firestore schema, so consoles and phones share the same world chat (`worldChat` with `channel: en|fr`), DMs (`privateChat` mirrored under both users, `conversations/*/threads` unread badges), friends list (`friends/{uid}.friends[]` with the same `isPending/hasSentInvite` semantics), presence (`users.lastConnection`), and the same FCM pushes. Console-specific additions:
>
> - Avatars served pre-decoded as raw RGBA (the console has no image codec).
> - Drawings stored as `type:"image"` + PNG (rasterized in pure JS) **and** a `strokes` field, so phones see a picture and consoles render vectors.
> - Voice as `DPV1` (IMA-ADPCM); with ffmpeg installed it's also transcoded to m4a for phones, and phone AAC notes are transcoded for consoles.
> - Themed rooms under `consoleRooms/` (rules snippet included), login by username **or** email.
>
> `client/` — libctru/citro2d homebrew (~5k lines C): all 11 mockup screens in EN/FR, hard-shadow UI toolkit, httpc on a worker thread, polling with the "Syncing · Ns" countdown + Y refresh, SD cache/token/config, stylus stroke canvas, mic→ADPCM streaming (no 10 s PCM buffer), ndsp playback, and the full Local Wireless stack over UDS (host/approve/deny/mute/kick, passcode = UDS passphrase, chunked unicast draw/voice, heartbeat-based signal).
>
> **What I need from you**
>
> 1. **Firebase service-account JSON** (Project settings → Service accounts → Generate key) → save as `relay/serviceAccount.json`.
> 2. **Web API key** for `POST /auth/login` (password verification goes through Identity Toolkit). `firebase_options.dart` has the Android/iOS keys; if those are app-restricted, create an unrestricted server key.
> 3. Apply `relay/firestore.rules.snippet` to the project's rules, and note the relay must be reachable over **plain HTTP** (3DS `httpc` can't do modern TLS).
> 4. **devkitPro** isn't installed on this Mac, so I couldn't produce a `.3dsx`. I did the next best thing: pulled the real libctru/citro3d/citro2d headers and syntax-checked every file with clang `-Wall -Wextra` — it's clean, and argument types match the real prototypes. Install via https://devkitpro.org/wiki/Getting_Started + `dkp-pacman -S 3ds-dev`, then `make` in `client/`. First-run bugs on real hardware are still likely (texture orientation, swkbd, UDS timing) — that's the next round.
> 5. `sdmc:/3ds/dspfirm.cdc` on the console for voice **playback**.
>
> **Deviations / decisions to confirm (all noted in `client/README.md`)**
>
> - "Level 12" has no backing field → shows friend count. Emoji button inserts text emoticons (no emoji glyphs in Nunito/3DS font). Nearby-room signal comes from beacon presence across scans (libctru exposes no RSSI). Themed rooms are a third tab on the Friends screen. World-chat voice from consoles only plays on phones when ffmpeg is on the relay.
> - Still open from the spec: avatar source (relay currently serves Dingplay profile pics), 16-slot cap (exposed, unverified), real logo/splash assets.

---

## 2. What has and has not been verified

| piece                                                       | verified how                                                                                                                                     | not yet verified                                             |
| ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------ |
| `relay/src/lib/*` (ADPCM, DPV1/WAV, PNG rasterizer, tokens) | `npm test` (6 unit tests, Firebase stubbed by `test/_stub.js`)                                                                                   | —                                                            |
| relay routes                                                | boot smoke test with the stub: `/health`, `/news` answer, auth-gated routes return 401                                                           | **never run against real Firestore** — needs the two secrets |
| `client/` C code                                            | `clang -fsyntax-only -Wall -Wextra` against the real libctru / citro3d / citro2d headers (cloned from GitHub); all files clean, prototypes match | **never compiled with devkitARM, never run on hardware**     |

So the first job on the PC is: `cd client && make`, fix whatever devkitARM/newlib complains about,
then test on a console. Expect small issues, not structural ones.

## 3. Building on the PC

```sh
# devkitPro (Windows: the graphical installer / msys2 shell; Linux: dkp-pacman)
dkp-pacman -S 3ds-dev          # devkitARM, libctru, citro3d, citro2d, tex3ds, mkbcfnt
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM   # msys2 sets these itself

cd client
make                            # → dingplay-chat.3dsx + .smdh
# fonts: Makefile runs mkbcfnt on ../fonts/Nunito/static/Nunito-ExtraBold.ttf and Nunito-Black.ttf
#        into romfs/*.bcfnt (24 pt). If mkbcfnt is missing or the flag differs, fix the two rules
#        near the top of the Makefile ("fonts:" target). The app falls back to the system font if
#        the .bcfnt files are absent, so a font failure is not fatal.
# gfx:   gfx/logo.t3s → romfs/gfx/logo.t3x via tex3ds (boot splash / login logo).
make cia                        # optional; needs bannertool + makerom + banner.png/banner.wav
```

Things most likely to need a touch on first compile:

- `-Wall` warnings from `static const` arrays in `source/theme.h` being unused in some TUs (harmless; add `-Wno-unused-const-variable` if noisy).
- `ssize_t` in `source/ui.c` / `udsnet.c` (comes from `<sys/types.h>` on newlib — add the include if devkitARM complains).
- Makefile paths if the PC checkout doesn't keep `fonts/` next to `client/` (`FONT_SRC_DIR`).

## 4. First things to check on real hardware (ordered by risk)

1. **Texture orientation** — `source/avatar.c: tex_upload_rgba()` writes image row 0 to texture row `th-1` and uses subtex `top=1, bottom=1-h/th` (tex3ds convention). If avatars or the dot grid appear upside down, flip the row mapping in that one function.
2. **Text scale** — `source/ui.c: scale_for()` = `px * 1.364 / lineFeed`. If Nunito renders too big/small compared with the mockups, tune `TEXT_LH_RATIO` (one constant).
3. **Dot-grid tint** — `ui_dots()` relies on `C2D_PlainImageTint` multiplying texture alpha by the tint alpha. If the dots look too strong, bake the alpha into the texture instead.
4. **httpc** — `net.c` runs requests on a worker thread; body up to 128 KB (`httpcInit(0x20000)`). Verify POST with `Content-Type: application/octet-stream` (voice) reaches the relay.
5. **swkbd** — `kbd.c`; check `SWKBD_TYPE_NUMPAD` with `swkbdSetNumpadKeys(&kb, 0, 0)` and password mode.
6. **UDS** — `local/udsnet.c`: `udsInit(0x20000, name)`, recv buffer `0x10000`, passphrase `dingplay-chat:<code|open>`, beacon appdata 66 bytes. Test host + one guest first (HELLO → WELCOME → ROSTER), then draw (chunked) and voice (~60 chunks). If a scan hitches the UI too much, reduce `SCAN_BUF_SIZE` or scan less often (`lobby.c`, 5 s auto-scan).
7. **Mic** — `voice.c` polls `micGetLastSampleOffset()` each frame and encodes on the fly; check gain (`MICU_SetGain(0x50)`) and that the 0.2 s minimum-length rule doesn't swallow real notes.
8. **ndsp** — needs `sdmc:/3ds/dspfirm.cdc`; without it `voice_can_play()` is false and only playback is disabled.
9. **Scissor** on the friends list and draw canvas (`C3D_SetScissor` uses the rotated framebuffer coordinates; `C2D_Flush()` is called before each change).

## 5. Repo map (what lives where)

```
relay/                Node 20+, CommonJS. `npm start`, `npm test`.
  src/server.js       Fastify app, registers routes/*
  src/auth.js         HMAC session tokens, username→email resolve, Identity Toolkit login, user cache
  src/routes/         misc (health/stats/news) · auth · friends · chat (+ /media) · rooms · avatar
  src/lib/            adpcm · wav (DPV1) · png (stroke rasterizer) · media (ffmpeg, cache) · presence · rooms · storage
  firestore.rules.snippet, news.json, .env.example
client/               devkitARM. See client/README.md for the per-file map and controls.
  source/screens/     boot · login · home · friends · chat · lobby · create_room · local_chat · manage_room · settings · common (chat log, voice bubble, record panel)
  source/local/udsnet.c   Local Wireless protocol (PKT_* frames, chunk reassembly, roster, heartbeat)
```

Conventions used throughout: C with `s_` static file state and `g_` globals; screens are a
`ScreenVTable {enter, leave, update, draw_top, draw_bottom}` selected by `SCREENS[]` in `main.c`;
navigation is a stack (`app_go / app_back / app_replace / app_reset_to`), B always pops; `tr(S_*)`
for every user-visible string (EN/FR tables in `i18n.c`); colours/sizes come from `theme.h` and
match the mockup hex values.

## 6. Things that only exist on the Mac (not in this repo)

- The mobile app source, `~/Desktop/dingconnect` (Flutter + Firebase, project `dingplay-e3401`).
  The relay's schema knowledge was taken from `lib/services/*.dart`, `lib/models/*.dart`,
  `functions/index.js` and `firestore.rules` there. If a schema question comes up on the PC, ask
  Rémy for the relevant file rather than guessing.
- The Firebase Web API keys are in `dingconnect/lib/firebase_options.dart` (Android/iOS keys —
  may be app-restricted, see §1 item 2).

## 7. Do not commit

`relay/.env`, `relay/serviceAccount.json`, `relay/cache/`, `relay/node_modules/`,
`client/build/`, `client/romfs/*.bcfnt`, `client/romfs/gfx/`, `*.3dsx/.smdh/.elf/.cia`
(all already in the two `.gitignore` files).

## 8. Suggested next steps, in order

1. `cd client && make` → fix compile errors → run in Citra first (network + touch work; UDS does not), then on hardware.
2. Fill `relay/.env`, `npm start`, log in from the console, watch the relay log for the
   `consoleRooms` composite-index link on the first `/rooms` call and create the index.
3. Test online chat end-to-end with a phone: text both ways, a drawing (phone should show a PNG),
   a voice note (needs ffmpeg on the relay for the phone side).
4. Test Local Wireless with two consoles: host, join, approve, draw, voice, mute, kick, close.
5. Then the open spec decisions (avatar source, 16-slot cap, real logo/splash assets) and any
   pixel polish against the mockups.

For Rémy :

Written for: Claude Code continuing the project on your PC.

HANDOFF.md is at the repo root. It contains:

The last status message, verbatim (the text you pasted).
A verified / not-verified table — the honest split between what was actually run (relay unit tests + stub smoke test) and what was only syntax-checked (all of client/).
Build steps for devkitPro, plus the three things most likely to trip the first make (font rule, ssize_t, Makefile font path).
A ranked list of what to check first on real hardware, each pointing at the exact function/constant to touch (texture orientation, text scale, dot tint, httpc, swkbd, UDS, mic, ndsp, scissor).
Repo map, code conventions, what only exists on this Mac (the dingconnect mobile source and where the Firebase keys are in it), the do-not-commit list, and next steps in order.

One thing to be aware of when you push: relay/node_modules/ and client/romfs/\*.bcfnt are git-ignored, so on the PC you'll run npm install in relay/ and make will regenerate the fonts from fonts/ — keep the fonts/ folder in the repo.
