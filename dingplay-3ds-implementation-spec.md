# Dingplay Chat — New Nintendo 3DS — Implementation Spec

## How to use this document

The two attached files (`Dingplay_Chat_3DS_dc.html` and `Dingplay_Chat_3DS_-_FR_dc.html`) are the **visual source of truth** — exact layout, spacing, colors, copy, in EN and FR. This document does **not** repeat that pixel-level detail. It covers what the mockups can't: the data model, the networking protocols, the on-device architecture, and the decisions needed to turn 11 static screens into a working homebrew app.

Read the HTML files first screen-by-screen (`data-screen-label` attributes mark each one: 01 Boot → 09 Settings), then use this doc for everything behind them.

## Target hardware & toolchain

- **Target**: all models in the 3DS family — 3DS, 3DS XL, 2DS, New 3DS, New 3DS XL, New 2DS XL. The app must run on Old 3DS/2DS hardware (ARM11 ~268 MHz, ~64 MB app-available RAM) as the baseline; New 3DS models (~804 MHz, ~124 MB app-available RAM) get the same app, just with more headroom. This means memory budgets for chat logs, drawing buffers, and voice recording must be sized for the Old 3DS's constraints — keep allocations lean, cap in-memory message history, and stream voice data to SD rather than holding full 10s PCM buffers in RAM before encoding.
- **Toolchain**: devkitARM + libctru, citro2d/citro3d for rendering. Standard homebrew `.3dsx`/`.cia` build via devkitPro's pipeline.
- **No CFW plugin injection.** This is a standalone foreground app launched from the Homebrew Launcher / CIA — not a 3GX plugin. It owns the whole console while running.

## Screen inventory (maps to the mockup files)

| # | Screen | Top screen role | Bottom screen role |
|---|--------|------------------|---------------------|
| 01 | Boot | Logo splash | Progress + Wi-Fi check |
| 02 | Login | Branding, "no account" App/Play Store nudge | Username/password fields, local-wireless escape hatch |
| 03 | Home | Identity, online friend count, unread count, news banner | ONLINE / LOCAL mode tiles, Friends, Settings |
| 04 | Friends & Global | Detail card for selected row | Friends/Requests tab list, Global Room entry |
| 05 | Online chat (hero) | Full chat log, "Syncing · Ns" indicator | Input, contact switcher, Emoji/Voice/Send |
| 06 | Local lobby | Radar visualization of nearby rooms | Room list, Host a Room |
| 06b | Create room | Live preview of the broadcast card | Name, slot count, passcode toggle |
| 06c | Manage room (host only) | Roster + pending-join panel | Accept/deny, mute/kick, invite/close |
| 07 | Local chatroom | Mixed-type message log (text/draw/voice bubbles) | Type/Draw/Voice tab switcher |
| 08 | Voice message element | — (component reference, not a screen) | — |
| 09 | Settings | Account, Wi-Fi + Local Wireless status | Toggles, sync interval, language, sign out |

Button mappings shown in the mockups (keep these consistent everywhere they apply): **B** = back, **Y** = force a manual refresh in online chat, **L/R** = cycle Type/Draw/Voice tabs without leaving the stylus, **START** = quit to Home Menu from the Home screen.

## Design tokens (extracted from the HTML for direct reuse in code)

- **Colors**: ink `#241F1A`, cream background `#FFF3E0`, orange `#F7941E`, green `#2FB86E`, blue `#37A8EE`, red (destructive) `#EF4444`, warm neutral `#EBDFC9` / `#6B6357` for secondary text and inactive states.
- **Font**: Nunito (weights 700/800/900 used throughout — this app is bold-everywhere, no light/regular weight anywhere in the mockups). **Open question**: Nunito as a web font isn't directly usable in libctru; see "Font on device" below.
- **Shape language**: thick 2–3px borders, hard drop-shadows (`Npx Npx 0 #241F1A`, no blur) rather than soft shadows, generously rounded corners (9–20px depending on element size). Recreate this as a flat "hard shadow" primitive (an offset solid rectangle drawn behind the element) since citro2d has no native box-shadow.
- **Screen surfaces**: exact native resolutions — top **400×240** (display-only, never handle touch input here; on 2DS models this is the upper portion of the single panel), bottom **320×240** (all touch/stylus input lives here). The console-shaped chrome around the screens in the mockups is presentation only, not part of the app.
- **Background texture**: a subtle dot-grid pattern appears behind several bottom-screen views, with a slow diagonal drift animation. Recreate as a small repeating dot texture rendered/tiled by citro2d; keep the animation cheap (one texture offset increment per frame, not a redraw of the whole pattern).

## Data model

Keep messages, rooms, and contacts as small structs designed for the payload sizes this hardware can actually push over local wireless and a polled relay — not a port of Dingplay's mobile schema.

- **Message**: `id`, `sender_id`, `type` (`text` | `draw` | `voice`), `payload`, `timestamp`, `room_id` (global / friend-DM id / local-room id).
- **Draw payload**: a **vector stroke list**, not a raster image — `[{color, width, points: [(x,y), ...]}, ...]`. This is both far smaller to transmit and matches what the canvas mockup (07) actually captures: 5 colors, 3 pen widths, an eraser. Render the same stroke list locally and on receiving consoles.
- **Voice payload**: a compressed audio blob, capped at **10 seconds** (per the mockup's recording UI) to keep it small enough for a local wireless frame or a relay upload from a console with no real upload bandwidth to spare.
- **Room** (local wireless): `name`, `host_id`, `slot_count`, `locked` (bool + passcode), `members[]`, `pending_joins[]` — matches screens 06b/06c exactly.

## Networking — two independent stacks

### 1. Online mode (account chat, friends, global room)

- **Client never talks to Firebase directly.** It talks to a relay server over plain HTTP (the console's `httpc`/`soc:U` services don't need to negotiate modern TLS against Google's endpoints if the relay handles that — see the earlier conversation for why this matters).
- **The relay MUST be Node.js** (Express or Fastify, either is fine). It uses the Firebase Admin SDK server-side to read/write Firestore and validate Dingplay accounts. The 3DS sees a simple JSON-over-HTTP API; the relay does all Firebase interaction. This is a hard requirement, not a suggestion — the relay codebase lives alongside Dingplay's existing Node/Firebase tooling.
- **Polling, not push.** The console polls the relay on an interval selectable in Settings (mockup shows 3s / 5s / 15s, defaulting to 5s) via something like `GET /chat/{room_id}?since={last_message_id}`. The chat view's "Syncing · Ns" indicator and the **Y = Refresh now** button (screen 05) should map directly to this poll cycle and a manual force-poll.
- **Auth**: log in once (screen 02) against the relay, which validates against the real Dingplay account backend and returns a session token. Cache that token on SD card if "Stay signed in" is checked; never re-send the raw password on every poll — only the token.
- **Sending a message**: `POST /chat/{room_id}` with the token; relay writes through to Firebase.
- **Online themed chatrooms (console-exclusive feature)**: beyond friends DMs and the single global room, logged-in users can **create and join themed chatrooms** (e.g., "Mario Kart 7 Tonight", "Retro Talk", "Salle B12 Homework Help") that live on the relay and are visible to any connected console. These are **not** available on the Dingplay mobile app — they are exclusive to console clients (3DS now, modded Switch when that client ships). This is both a differentiator that gives the console version its own identity and a practical constraint: the mobile app's chat UX is already designed, and these lightweight topic rooms fit the 3DS's "school break" use case better than they'd fit a phone. The relay manages room creation, listing, membership, and message routing identically to how it handles the global room — just with a user-provided name and optional topic tag. List them via `GET /rooms?type=themed`, create via `POST /rooms`.

### 2. Local Wireless mode (no account, no internet)

- Built on the `uds`/NWM local wireless service — the same primitive already used for 3DS-LWO's UDS interception work, just used here to originate a session rather than intercept a game's.
- **Host/join model**: one console hosts (creates the beacon others discover — screen 06b), others scan and join (screen 06). The host approves/denies join requests and can mute/kick (screen 06c) — the host is the authority for room membership, there's no separate server for local mode.
- **Slot count**: mockup UI offers 4/8/16 as selectable choices. Treat 16 as **unverified** — confirm real-world reliable node count for UDS chat traffic (not game state sync) before exposing it in the shipped UI; default the UI to 8 until that's tested.
- **Wire format**: keep frames small and simple — a compact binary format (not JSON) for text/stroke/voice-chunk packets, since this is raw local wireless bandwidth, not an HTTP body. Draw strokes should be batched/throttled (e.g., flush every N points or every frame) rather than sent point-by-point.
- **Range/session semantics**: a room only exists while the host's app is in the foreground; nothing persists past the host closing it. "Weak signal" / "out of range" states shown in the mockups (Bus 42, Maya's join banner) should map to real UDS connection-quality signals, not a fake indicator.

## Input specifics

- **Text entry**: every tap-to-type field (login, room name, chat compose) opens the system software keyboard applet (`swkbd`). This briefly suspends the app — expected and unavoidable, don't try to build a custom in-app keyboard to avoid it.
- **Drawing**: capture raw touch/stylus samples from the bottom-screen touch panel while in Draw mode; convert to the stroke-list format above in real time as the user draws, rendering locally immediately (don't wait on a round-trip for local feedback).
- **Voice**: use the `mic` service's shared-memory PCM buffer (hold-to-record UI in screen 07b maps directly to starting/stopping capture on button-down/button-up or touch-down/touch-up). Encode to a low-bitrate codec (IMA-ADPCM is a reasonable, cheap-to-implement choice for this hardware) immediately after capture, before it's sent anywhere. Playback goes through the DSP service.

## State/navigation flow

Home (03) is the hub. From there: Online → Friends & Global (04) → Chat (05); Online → Themed Rooms list → Chat (same chat view as DMs/global, just a different room_id); Local → Lobby (06) → either join an existing room (straight to 07) or Create (06b) → become host → Manage (06c, host-only overlay reachable from within 07) while still in the chatroom. Settings (09) and Friends are reachable from Home directly. B always steps back one level in this tree; there's no deep-linking need beyond that.

**Local lobby access from inside a chatroom**: when a user is already in a local wireless chatroom (screen 07), they should be able to open the local lobby (screen 06) without leaving the room — e.g., to see who else is nearby, check other available rooms, or invite someone. The UDS connection stays alive in the background while the lobby view is shown; the user can tap back into their active room without rejoining. This avoids the frustrating flow of having to leave a room, scan, then re-join just to check what's around.

## Persistence (SD card)

- Cached session token (online mode "stay signed in").
- Recently synced messages per room, so re-opening a chat doesn't start blank while waiting on the first poll.
- Local settings: sync interval, language (FR/EN — the two mockup files are already fully localized copies of the same 11 screens, so treat FR/EN as a straight string-table swap, not a layout difference).
- No local wireless room state persists past the session — rooms are ephemeral by design.

## Explicitly out of scope for v1

- Video, images, stickers, or any media type beyond text / vector drawing / short voice notes.
- Live/continuous voice (calls) — voice is always a discrete recorded-then-sent message, 10s max.
- Any overlay-during-gameplay mode — this is a standalone foreground app only.

## Open decisions (need Rémy's input before/while building)

1. **Avatars**: mockups reuse Dingplay mobile profile pictures (`assets/mii1.png` etc. are placeholders, not literally Mii data) — confirm whether the console pulls the user's real Dingplay avatar via the relay, falls back to the console's own Mii, or both.
2. **Font on device**: Nunito Black/ExtraBold/Bold need to become a bundled bitmap or TTF font for citro2d — check legibility at the ~8–9px sizes used for secondary text before locking it in; a fallback system font may read better at that size even if it's less on-brand.
3. **Local wireless slot cap**: verify 16 is realistically reliable for chat traffic before shipping it as a UI option; 8 is the safer default.
4. **Logo assets**: launcher icon (48×48) and the boot splash mark are placeholders (dashed boxes) in screens 01/02 — need the real files before those screens are final.
