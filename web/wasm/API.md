# Browser client command/query API

The wasm client runs only the game view; everything the desktop client does through
its menus (connect, chat, settings, key bindings, votes, message boxes) goes through
this API. It is the contract the page UI is built against; `client.html` is a
small test page over it.

Files: `aa_api.js` (the JS wrapper, no build step), `src/tron/gBrowserApi.cpp` (the
exports and events), `src/tron/gBrowser.cpp` (the session lifecycle),
`src/tron/gScene.cpp` (the attract scene), `web/wasm/Makefile` (`API_EXPORTS`,
`-lidbfs.js`).

Three things can be running: a **network session** (`connect`), the **local game**
against the AI (`startLocal`), and the **attract scene** (`scene`), the page's
background. All three are driven the same way, one step per animation frame; only
one of them runs at a time.

## Mechanism

Plain C exports (`extern "C" EMSCRIPTEN_KEEPALIVE aa_*`) called through
`Module.cwrap`, plus one `EM_JS` sink (`Module.__aaEmit(type, jsonText)`) for events.
Chosen over embind because the surface is small (about 30 functions), every argument is a
string or a number, and the results are JSON documents anyway; cwrap adds no code to
the wasm, needs no C++ RTTI/glue and keeps the exports greppable
(`EXPORTED_FUNCTIONS` in the Makefile). Queries return a pointer to a static buffer
that cwrap copies at once (valid until the next call); commands return 1/0 and report
the reason for a 0 through the `error` event.

Text: everything crosses the boundary as UTF-8. The game's own strings are Latin-1
(its bitmap font has 256 glyphs); the wrapper receives them with bytes >= 0x80 written
as `\u00xx` (so a Latin-1 name reads correctly in JS) and folds UTF-8 input back to
Latin-1 (characters above U+00FF become `?`). The exception is chat in a room that carries
UTF-8 (`setChatUtf8(true)`, below): `chat()` then sends the text as typed, and lines from
such rooms carry UTF-8 inside those `\u00xx` bytes, next to Latin-1 names from desktop
clients. A page reads well formed UTF-8 sequences as the characters they spell and every
other byte as Latin-1 (the play page's `src/chat/text.ts` `decodeGameText`; the server reads
the same way, `src/tools/tUtf8.h`). Color codes (`0xRRGGBB`, `0xRESETT`) are
kept in `line`/`coloredName` fields and stripped in `text`/`name` fields;
`aa.stripColors()` and `aa.colorSpans()` are provided for rendering.

## Setup (page side)

```html
<canvas id="canvas" width="800" height="600" tabindex="0"></canvas>
<script src="aa_api.js"></script>
<script>
  var Module = {
      canvas: document.getElementById("canvas"),
      arguments: ["--userdatadir", "/aa/user"],   // var/ below it is the persistent mount
      print: console.log, printErr: console.log
  };
  var aa = AA.prepare(Module);                    // before client.js loads
  aa.on("ready", function () { /* queries allowed from here on */ });
</script>
<script src="client.js"></script>                 <!-- or client_opt.js -->
```

`AA.prepare(Module, options)` installs the event sink, the persistence mount
(`options.persist !== false`), the resource proxy (`options.resourceProxy`, see
`setResourceProxy`) and the export bindings, and returns the API object. It
must run before `client.js` executes (it pushes a `Module.preRun` step).
`AA.stripColors`/`AA.colorSpans` are also available statically.

The canvas needs no focus for the game keys: the client listens on `window` and takes
every key except the browser's own shortcuts (the reserved list below). Keys are left
to the page while an `<input>`, `<textarea>`, `<select>` or contentEditable element has
the focus, so text fields never leak keys into the game; give the focus back to the
canvas (or anything that is not a text field) when the field closes. The same holds for
any element inside one marked `data-aa-keys="page"`: a page menu that takes the keyboard
while it is open, as the desktop client's in-game menu does (the play page's Esc menu).
Those keys are neither queued nor `preventDefault`ed (the page handles them, Escape
included: it raises no `menuOpen` then); a key held when the focus moved in is still
released to the game.

Reserved keys (left to the browser, cannot be bound): every Meta/Cmd/Win combination;
F5, F11, F12; Ctrl(+Shift)+L, T, W, N, R, Tab, F4, PageUp, PageDown, 0-9, -, =;
Ctrl+Shift+I, J, C, Delete. Everything else is the game's and `preventDefault`ed:
modifier keys themselves (Ctrl/Alt/Shift as brake keys), Ctrl/Alt/Shift + a turn key,
F1-F4, F6-F10, Ctrl+S/P/F/D/H/O/U (the browser's save/print/find... do not open while
the page has the focus). Escape while an element is fullscreen (`document.fullscreenElement`)
is the browser's too: it leaves fullscreen and does not reach the game. The canvas' CSS
size is the layout's business (the test page fills the viewport width up to 1280 px at
16:9); the client sizes its backing store from the CSS size times `devicePixelRatio`
(capped at 2) on window resizes, a `ResizeObserver` on the canvas and pixel ratio
changes, and reports it as `size=WxH dpr=` on the status line. Fullscreen is the page's
job through the Fullscreen API (`canvas.requestFullscreen()` from a user gesture, the
test page's "fullscreen" button); the `:fullscreen` element gets the screen's CSS size
and the same path follows it, and exiting restores the layout size.

Background tabs: `requestAnimationFrame` stops while `document.hidden`; the client then
runs its frames (network + simulation, no rendering) from a Worker timer at 10 Hz
(`setInterval` fallback) and switches back when the tab is visible, so the connection
survives an arbitrary time in the background and a turn key pressed right after
switching back is applied at once. A window blur (another window took the focus) keeps
rendering; only keys held at that moment are released.

## State machine

```
              connect(url)                    DataChannel open
   idle ───────────────────────► connecting ─────────────────────► (login)
     ▲                              │                                  │
     │ page load                    │ handshake timeout (20 s),        │ login ok
     │                              │ signalling/ICE failure,          ▼
     │                              │ login timeout/denied          syncing
     │                              │                                  │ two object syncs,
     │                              ▼                                  │ game object arrived
  disconnected ◄─────────────── (session ends) ◄────────────────── playing ◄──► fetching
     │  reason = "requested" | "connection lost" | "login denied (...)" |
     │           "login timeout" | "no game state" | "data channel did not open (state n)" |
     │           "exception <name>: <text>" | "handshake not started" | ...
     └──── connect(url) again (same rules as from idle)
```

- `idle`: nothing running; the page's `#ws://...` fragment, if present, connects at
  once, otherwise the client waits for `connect()`.
- `connecting`: signalling WebSocket + WebRTC handshake, then the login exchange. The
  `handshake` event marks its steps: `signalling` (the server answered the offer) and `ice`
  (the data channel is open); the login follows, and `syncing` means it was accepted.
- `syncing`: logged in; object relabeling, the second sync, waiting for the game object
  (30 s timeout, reason `no game state`).
- `playing`: the game loop runs; chat, votes and the player list are live. The local
  player object (`players[].local`) exists from the first round start after the join
  on; `chat()` needs it.
- `fetching`: in the game, but the round's map (`MAP_FILE` on the server) is
  not in the client's cache and is being downloaded; `resource` in the state object
  names the file. The game state machine waits in `GS_CREATE_GRID` (no rendering of
  the grid, the network keeps running) and returns to `playing` when the file is in
  the cache, or when every URL failed (the map then fails to load like it does
  natively: `message` event "Map load failure", session ends with reason
  `exception ...`). Happens on every map change, usually once per session.
  Sources, in order: `MAP_URI`, `RESOURCE_REPOSITORY_SERVER/<file>`,
  `RESOURCE_REPOSITORY_CLIENT/<file>`; a fetched map's `DOCTYPE SYSTEM` DTD is fetched
  the same way when it is not in `resource/included`. Fetched files are cached in
  IndexedDB (`/aa/user/resource`, see Persistence) so reconnects and reloads do not
  download again (keyed by the resource path, whatever URL it came from). A direct
  `fetch()` needs the repository to send `Access-Control-Allow-Origin` for the page's
  origin, and `resource.armagetronad.net` does not; so the page names a *resource
  proxy* (`setResourceProxy(base)`, below; the deployed `index.html` sets `/resource/`,
  Caddy's caching CORS proxy in `web/deploy`) and every URL is then requested as
  `<base><host>/<path>` - `http://resource.armagetronad.net/resource/A/b.aamap.xml`
  becomes `https://play.example.com/resource/resource.armagetronad.net/resource/A/b.aamap.xml`
  (the scheme dropped; the proxy dials https, or plain http for `host:port`). The DTD
  chain goes through it too; URLs on the page's own origin go direct; the direct fetch is
  not attempted while a proxy is set (it would fail on CORS). Without a proxy (the dev
  page) the fetch is direct. The game server's `RESOURCE_REPOSITORY_SERVER` stays
  untouched. Diagnostic lines (stdout, see "Diagnostics" below): `BROWSER: resource
  <file> fetching from <url>[ via proxy <proxied url>]` / `fetched, <n> bytes` / `fetch
  failed: <why>` / `cached.`.
- `disconnected`: the last session ended, `reason` says why; `connect()` is allowed.
  Every session end saves the configuration (`saveConfig()` semantics).

`connect()` returns `false` (and emits `error`) while a session is running or the URL
is empty; a handshake that cannot even start moves to `disconnected` with reason
`handshake not started`. `disconnect()` is a no-op in `idle`/`disconnected`; in
`connecting` it ends the session at once, in `syncing`/`playing` at the next frame
(reason `requested`). Redirections by the server are reported in the reason and not
followed; a platform room moves browser clients with the `move` event instead ("Moves").

Two more states exist, both standalone (`netstate: 0`), both reachable from
`idle`, `disconnected` and `scene`:

```
   idle / disconnected ──startLocal()──► local ──stopLocal() | disconnect()──► disconnected
             │  ▲                                                   │ (reason "requested")
             │  │ sceneStop()                                       ▼
             └──┴────────scene(1..4)──► scene ───connect(url) | startLocal()───►
```

- `local`: the desktop client's **Local Game** — standalone (`nSTANDALONE`), the
  local player plus the AI opponents of `config/aiplayers.cfg`, the game's own
  rounds, HUD, scoreboard and console overlay, exactly what
  `sg_SinglePlayerGame()` does natively. It is a *session*: `stopLocal()` (or
  `disconnect()`) ends it and the state becomes `disconnected` with reason
  `requested`, like a network session. `chat()`, `getTeams()`, `vote()`, `login()`
  and everything else that needs `nCLIENT` are refused here (the local game has no
  server); settings, bindings and `console()` work as always. The number and
  strength of the AIs are the `SP_NUM_AIS`, `SP_MIN_PLAYERS` and `SP_AI_IQ`
  configuration items.
- `scene`: the attract scene (below) — standalone, no player, no HUD, no console
  overlay, a fixed low camera. It is *not* a session: `sceneStop()` returns to
  `idle` (or `disconnected`, if a session had ended before it), and `connect()` /
  `startLocal()` may be called while it runs — they stop the scene themselves.
  `scene()` while a network session or the local game runs is refused
  (`false` + `error`).

## Functions

All functions throw if called before the `ready` event.

| function | returns | what it does |
|---|---|---|
| `connect(url, options)` | `true` if the handshake started | `url`: `ws://host:port/` or `wss://...` of the server's signalling socket (`WEBRTC_SIGNAL_PORT`, default game port + 1). The host part is used as the server's IP for the log; a DNS name is reported as 127.0.0.1 to the game (the browser does the real resolution). `options` is optional: `{loginToken}` hands a platform login token to this connection (`platformLogin(token)` right after the handshake started; see "Platform login"); `{iceServers, iceTransportPolicy}` are STUN/TURN servers for this connection's `RTCPeerConnection` (see "ICE servers"); `{transport: "websocket"}` sends the game packets over the signalling socket instead of WebRTC (see "WebSocket transport"). `connect(url)` alone is unchanged (no ICE servers). |
| `platformLogin(token)` | `true` if queued | a platform login token for the running network session (`connecting`, `syncing`, `playing`, `fetching`): the client sends it to the server once its connection is logged in, once (see "Platform login"). `false` + `error` without a network session or with an empty token. A token that has not gone out when the session ends is dropped. |
| `disconnect()` | – | ends the running session, see above (also the local game; stops the scene) |
| `startLocal()` | `true` if it started | starts the desktop client's local game against the AI (state `local`, `netstate: 0`). Refused while a network session or the local game runs; a running scene is stopped first. No server, no signalling, no network at all. |
| `stopLocal()` | – | ends the local game at the next frame; the state becomes `disconnected` with reason `requested` |
| `scene(k)` | `true` if accepted | the attract scene (below): starts it if it is not running and plays towards state `k` (1-4), one transition at a time. `k` above the current state plays forward, below it plays the reverse choreography. Refused (`false` + `error`) for `k` outside 1-4, while a session runs, or when the scene's map fails to load. |
| `sceneStop()` | – | tears the scene down; back to `idle` (or `disconnected`) |
| `getState()` | state object (below) | the same object the `state` event carries; cheap, poll it for ping/fps |
| `getPlayers()` | array of player objects | the `players` field of the state alone |
| `turn(dir, count)` | turns taken (0-2) | turns the first local player's cycle as its turn keys do, whatever they are bound to: `dir` < 0 left, else right; `count` 2 a double turn (two turns the cycle queues, as two quick presses), else 1. Nothing during chat, before the round starts or without a living cycle (`ePlayer::Act`). The play page's touch buttons (`web/page/src/components/TouchControls.tsx`) |
| `brake(on)` | whether the cycle took it | holds (`true`) or lets go of (`false`) the first local player's brake, as its brake key's press and release do (`ePlayer::Act`, `gCycle::s_brake`). The play page's touch brake (keyboard layout) |
| `getCycle()` | `{own, alive, speed, rubber, rubberMax, rubberMalus, brake, braking, respawnIn}` or `null` | the cycle the first local player's camera follows (its own, or the one it watches while dead or spectating), as the HUD's gauges read it: `speed` m/s, `rubber` used of `rubberMax` (`CYCLE_RUBBER`; the share is `rubber / rubberMax`), `rubberMalus` the extra rubber use from grinding (0 without), `brake` the braking reservoir (1 full, 0 empty), `braking` the brakes on, `own` whether it is the player's own; `respawnIn` the seconds until the player's dead cycle respawns on a server that tells its browser clients its `RESPAWN_TIME` (below 0 while the new cycle is on its way; `null` when none is due), which the play page counts down (`web/page/src/components/RespawnCountdown.tsx`); `null` without a cycle. Cheap: the play page polls it on animation frames for its compact HUD (`web/page/src/components/CycleHud.tsx`) |
| `chat(text)` | `true` if sent | sends a chat line as the local player; `false` (+`error`) when not in a game. Server side commands work as in the desktop chat line: `/team text`, `/msg name text`, `/vote kick name`, `/players`, `/teams`, `/admin ...` (see the server's chat handler). Empty text is ignored. |
| `teamChat(text)` | `true` if sent | `chat("/team " + text)` |
| `setChatUtf8(on)` | – | whether `chat()` sends UTF-8 as typed (`true`: Korean, Chinese, Japanese get through) or folds it to Latin-1 (`false`, the default: characters above U+00FF become `?`). Only for a room whose server carries UTF-8 chat: one built from this fork reads a browser client's chat as UTF-8 and folds it per recipient for desktop clients (`CHAT_UTF8`); a stock 0.2.9 server turns half of UTF-8's bytes into `_`. The play page sets it for official rooms (`platformLogin` + `serverId`) at every connect. |
| `setPageScores(on)` | – | whether the page draws the score table (`true`): the canvas one (the `SCORE` key's, and the one a round's end shows) is no longer drawn, and every show or hide is a `scores` event instead, whatever key `SCORE` is bound to. `false` (the default): the client draws its own. The play page sets it once the client is ready. |
| `setChatting(on)` | – | marks the local player as chatting (the desktop client does this while its chat line is open; other players see it). Call with `true` when the page opens its chat input, `false` when it closes. |
| `console(line)` | `true` if run | runs one or more configuration lines (`\n` separated) exactly like `/console` in the desktop chat line: owner access level, `tConfItemBase::LoadAll`. Anything from `config/settings*.cfg` and the console command list works: `PLAYER_1 name`, `SAY text`, `INCLUDE keys_wasd.cfg`, `CAMERA_...`. Unknown names print `Unknown command` to the console (no `error` event; the console line is the feedback). Server controlled settings (`nSettingItem`, e.g. `CYCLE_SPEED`) are rejected on a client with a console line. Player/color changes take effect at once (`ePlayerNetID::Update`). |
| `getConfig(name)` | `{name, value, savable, writable, requiredLevel}` or `null` | one configuration item; `value` is its `WriteVal` text (numbers and bools as text, `"1"`/`"0"` for bools); `savable`: goes into `user.cfg`; `writable`: false for server controlled items while connected; `requiredLevel`: access level name. Names are case insensitive. |
| `setConfig(name, value)` | `true` if the item exists | `console(name + " " + value)` with an existence check and an `error` event for unknown names. In a game, a changed value also sends the player to the server (name, color); a call during the login or the sync does not. Values are texts; bools `0`/`1`; multi word strings need no quotes (the rest of the line is the value for `tConfItemLine` items such as `PLAYER_1`). |
| `listConfig(prefix)` | array of `{name, value, savable, writable}` | every configuration item whose name starts with `prefix` (empty: all, about 1,200 items in the browser build). Commands are listed too (`INCLUDE`, `SAY`, `KICK`: `savable` false, `value` ""), and so are the servers' settings (`savable` false); `getConfig` knows both as well. A page listing the player's own settings filters on `savable`. For building settings pages. |
| `saveConfig()` | – | writes `user.cfg` (everything `savable`, key bindings included) plus `var/autoexec.cfg` (below) and flushes the persistent mount to IndexedDB. Also done automatically when a session ends. |
| `getActions()` | array of `{name, kind, description, help, analog}` | every bindable action (58); `kind` is `player` (`CYCLE_TURN_LEFT/RIGHT`, `CYCLE_BRAKE`, `CYCLE_BRAKE_TOGGLE`, `CHAT`, `INSTANT_CHAT_1`..`_28`, `TOGGLE_SPECTATOR`), `camera` (`SWITCH_VIEW`, `GLANCE_LEFT/RIGHT/BACK`, `MOVE_*`, `ZOOM_*`, `LOOK_*`, `BANK_*`), or `global` (`INGAME_MENU` and `CONSOLE_INPUT` raise the `menuOpen`/`consoleOpen` events, `SCORE`, `PAUSE_GAME`, `MESS_UP/DOWN/END` scroll the on-canvas console; `TOGGLE_FULLSCREEN` raises the `fullscreen` event; `SCREENSHOT`, `TOGGLE_MOUSEGRAB`, `RELOAD_TEXTURES` are inert in the browser). `description`/`help` are the localized texts. |
| `getBindings()` | array of `{keysym, keyName, code, action, player}` | the key map: `keysym` is the SDL 1.2 keysym the client stores (what `user.cfg`'s `KEYBOARD` lines hold), `code` the DOM `KeyboardEvent.code` the client's key table maps to it (`null` for keys without one, e.g. mouse buttons), `keyName` a short label, `player` 1-4 for player actions (`INSTANT_CHAT`, turns), 0 for global actions. |
| `setBinding(action, player, key)` | `true` if bound | `key`: a DOM `KeyboardEvent.code` (`"ArrowLeft"`, `"KeyA"`, `"Space"`, `"F1"` – capture it from a `keydown` event) or an SDL keysym number. Replaces whatever the key was bound to (one action per key, any number of keys per action, exactly like the desktop key menu). Keys the client does not translate (`error` event: `no keysym for ...`) cannot be bound: Ctrl/Meta combinations and unknown codes. The change is in memory; `saveConfig()` persists it. |
| `unbind(key)` | `true` | removes the binding of that key |
| `keysymFromCode(code)` / `codeFromKeysym(keysym)` | number / string or `null` | the key table both ways (US layout: `KeyA` is always `a` regardless of the keyboard layout, the game binds physical keys) |
| `getVotes()` | array of `{id, description, details}` | the votes this client may answer (empty unless a vote is running and the client is in a game) |
| `vote(id, accept)` | `true` if the vote existed | casts the vote; the item disappears from the list |
| `getTeams()` | array of `{id, name, coloredName, color, players, humans, joinable, current, next}` | the teams in the game (what the desktop team menu lists): `id` the team's network object id (`players[].teamId` refers to it), `color` 0-15 RGB, `players`/`humans` head counts, `joinable` whether the local player may join (`eTeam::PlayerMayJoin`: room, not locked, not already the player's next team), `current`/`next` the local player's team now / next round. Empty when nobody plays (teams exist only with players in them) or the client is not in a game. |
| `joinTeam(id)` | `true` if the wish was sent | the team menu's "join": leaves spectator mode and sends the team wish (`ePlayerNetID::SetTeamWish`); the server applies it at once or at the next round (`players[].nextTeamId` shows the accepted wish, `teamId` the team in the current round). `error` when not in a game or the id is unknown. |
| `createTeam()` | `true` if the wish was sent | the team menu's "new team" (`CreateNewTeamWish`), also the way into a team after the server took the player out of every team (authentication, see below) |
| `setSpectating(on)` / `leaveTeam()` | `true` | the spectator switch of the local player (the team menu's "spectate", `TOGGLE_SPECTATOR`): `true` leaves the team and watches, `false` plays again (the server assigns a team). `leaveTeam()` is `setSpectating(true)`. Persists as `SPECTATOR_MODE_1`. |
| `login(globalId)` | `true` if the request went out | the desktop in-game menu's "Login" item: sets `USER_1` to `globalId` (`user@authority`; `user@` or `@` for the server's local accounts, `LOCAL_USER`) when given, and asks the server to start an authentication (`ePlayer::LogIn`). The server answers with a password request: the `authRequest` event. Needs a player in the game. The other trigger is the server-side chat command, `chat("/login user@authority")`. |
| `auth(username, password)` | `true` if a request was pending | answers the pending password request: the password is scrambled inside the wasm (the method the server named, `md5`/`bmd5`), the scrambled form is remembered for this run (`PASSWORD_STORAGE` 0; `-1` never, `1` also in `user.cfg`) so a reconnect answers the next request automatically, and the answer goes to the server. The result comes back as console lines (`... has been logged in as user@ at access level "..."`, or `Login failed, reason: ...`; a failed local login is asked again with `failed: true`). The password never appears in any event, log line or export. |
| `authCancel()` | `true` if a request was pending | answers "aborted" (the desktop menu's cancel); the server keeps the player unauthenticated |
| `exportConfig()` | text | `user.cfg` as the game writes it right now (`saveConfig()` first): every `savable` item, the `KEYBOARD` lines of the key map included, minus `PASSWORD` lines (stored hashes). The way to move settings to another browser. |
| `importConfig(text)` | `true` if applied | runs the text through the console path line by line (`console(text)`: `user.cfg` syntax, unknown names print `Unknown command`) and saves. Replaces what the lines name and leaves everything else. |
| `on(type, fn)` / `off(type, fn)` | unsubscribe function / – | event listeners, `fn(data, type)`; `"*"` receives every event |
| `unlockAudio()` | – | resumes the Web Audio context. Browsers create it suspended until a user gesture; `aa_api.js` already calls this from the first `keydown`/`pointerdown` on the page (capture phase), so a page needs it only for a custom "enable sound" control. Safe to call any time. |
| `getAudioState()` | `"none"` / `"suspended"` / `"running"` / `"closed"` | `none`: no device (Web Audio missing, or `SOUND_QUALITY 0` in the configuration); `suspended`: waiting for the gesture. The sound settings are configuration items: `SOUND_QUALITY` (0 off, 1-3 sample rate of the sources, the output rate is the browser's), `SOUND_SOURCES`; there is no master volume in the game. `setConfig` of `SOUND_QUALITY` or `SOUND_CHANNELS` closes and reopens the device at once, as the native sound menu does (0 closes it: `none`); a console line changing them takes effect at the next start. |
| `setResourceProxy(base)` / `getResourceProxy()` | the base in effect (`""` = direct) | the resource proxy (see `fetching`): `""` fetches directly, a path (`"/resource/"`) is resolved against the page's origin, a full `http(s)://` URL is used as is; a trailing `/` is added. Read at every fetch, so it can be set any time (before `ready` too, also as `AA.prepare(Module, {resourceProxy})`). |

Static helpers on `AA` (no runtime needed): `AA.normalizeIceServers(list)` returns the
usable `RTCIceServer` entries of `list` (see "ICE servers"; `[]` for anything else),
`AA.resolveSignalUrl(url)` turns a path
(`"/signal"`) into `ws(s)://<page host>/signal` (wss on https pages) and returns
`ws://`/`wss://` URLs unchanged; `AA.loadServers(url = "servers.json")` fetches a server
list (below) and resolves to `[{name, url, description}]` with the URLs resolved
(`[]` when the file is missing or malformed; the optional `serverId`/`platformLogin`
fields are passed through).

Properties: `aa.ready` (bool), `aa.persistence` (`"idbfs"`, `"memfs"` or `"none"`, see
below), `aa.Module`.

### The state object

```js
{
  state: "idle" | "connecting" | "syncing" | "playing" | "fetching" | "disconnected"
         | "local" | "scene",
  reason: "",            // why the last session ended (see the diagram), "" otherwise
  resource: null,        // in "fetching": the file being downloaded ("Author/cat/map-1.0.aamap.xml")
  url: "ws://...",       // signalling URL of the current/last session ("" for local/scene)
  ice: { local: "relay", remote: "host", protocol: "udp", relayProtocol: "tcp" } | null,
                         // the selected ICE candidate pair of the current/last network session
                         // (null until the data channel is open, and for local/scene; see "ICE servers")
  netstate: 0|1|2,       // the game's nNetState: 0 standalone (idle, local, scene), 2 client
  ping: 21 | null,       // ms, the game's smoothed estimate (null when not connected)
  gametime: 4.5 | null,  // seconds into the round (negative during the countdown)
  fps: 60 | null,
  synced: true,          // the game timer is in sync with the server
  joinIn: 1.4,           // a respawn game holding this player's first cycle (below): seconds until it comes, else null
  authPending: false,    // the server's password request is waiting for auth()/authCancel()
  sceneState: 0,         // the attract scene's state, 1-4; 0 when it is not running
  sceneTransitioning: false, // a scene transition is playing
  players: [ { id: 477, name: "Browser", coloredName: "0xff0000Browser", color: [15, 15, 15],
               score: 3, team: "Browser" | null, teamId: 480 | null, nextTeamId: 480 | null,
               ping: 21, local: true, bot: false, alive: true, spectating: false, chatting: false, active: true }, ... ]
}
```

`id` is the network object id (changes between sessions); `color` is the player's
`COLOR_R/G/B` 0-15 (the cycle color, `r/15` etc.); `team` is the team name (the
player's own name in single player teams), `teamId`/`nextTeamId` the ids of the team in
the current round and of the one the server accepted for the next (`getTeams()`);
`local` marks the player(s) of this client; `bot` an AI player (`gAIPlayer`, which has its
own network class, so a client knows it); `alive` whether the player's cycle rides now (false while it is dead, waiting to respawn, or the player spectates; the score table's Alive). The access level and the authenticated global id
are not synced to clients (the 0.2.9 protocol has no field for them); the login
announcement on the console names them.

## Events

`aa.on(type, function (data, type) {...})`. Every event carries a JSON object.

| event | data | when |
|---|---|---|
| `ready` | `{persistence}` | once, from `main()` after the configuration and language files are loaded; queries before it throw. (`Module.onRuntimeInitialized` is earlier and too early.) |
| `state` | the state object | on every transition of `state` (the diagram above); also once at startup |
| `console` | `{line, text}` | every line the game console prints, `line` with color codes, `text` without: chat, server messages, round results, setting changes, errors. Everything the desktop console shows; the page owns the console display. The client's own `BROWSER:` diagnostics are not among them (stdout only; "Diagnostics" below) unless `BROWSER_CONSOLE_LOG` is 1. |
| `chat` | `{line, text, playerId, name, local}` | a chat line arrived (a subset of the `console` lines, tagged): `line`/`text` are the full `name: text` line as formatted by the server (with/without colors), `playerId`/`name`/`local` identify the sender (`null`/`false` if unknown, e.g. server messages routed through chat). Own lines come back from the server too. |
| `menuOpen` | `{}` | the player pressed the key bound to `INGAME_MENU` (Escape by default): the desktop client opened its in-game menu here; the page should open (or close) its menu overlay - the test page shows the team list (`getTeams`/`joinTeam`/`createTeam`), the spectator switch, a login field and disconnect |
| `authRequest` | `{username, serverMessage, method, failed}` | the server asks for a password (after `login()`, a `/login` chat line, or on its own on servers that require it): `username` the account the server wants the password for, `serverMessage` its prompt ("Login with Authority forums", "Login with Local Account"), `method` the scrambling method, `failed` whether the previous attempt was refused. The page shows a password form and answers with `auth()` or `authCancel()`; `getState().authPending` is `true` meanwhile. The request is dropped when the session ends. No password material is ever part of an event. |
| `consoleOpen` | `{}` | the key bound to `CONSOLE_INPUT` was pressed: the page should open its console command input |
| `fullscreen` | `{}` | the key bound to `TOGGLE_FULLSCREEN` (`p` in the browser's key template, `browser_keys.cfg`) was pressed: the page toggles the browser's fullscreen (`requestFullscreen` / `exitFullscreen`). The event comes in the frame after the key press, inside the press's user activation, so the browser allows it. The game's own display is never switched. |
| `chatOpen` | `{prefill}` | the player pressed the game's chat key (action `CHAT`) or an instant chat whose string ends in `\` (a template): the page should open its chat input with `prefill` in it (the desktop client opened its own line here). Plain instant chats are sent directly and never raise this. |
| `scores` | `{show}` | with `setPageScores(true)`: the score table should show (`true`: the `SCORE` key pressed while hidden, or a round ended and the client shows the scores) or hide (`false`: the key again, or a new round). Never raised otherwise. |
| `message` | `{title, body, timeout}` | what the desktop client shows as a modal message box: server fullscreen messages (`FULLSCREEN_MESSAGE`, greetings, round-end messages on servers that use them), connection errors (`Login denied`, `Connection lost` with the server's kick reason and redirect target in `body`), and internal errors. `timeout` is the seconds the desktop box would stay; the game is not paused. Also printed to the console. |
| `players` | `{players}` | the player list changed (names, scores, teams, join/leave, chat/spectator flags), checked twice per second |
| `votes` | `{votes}` | the vote list changed (a new vote, one expired or was answered), checked twice per second; empty when no vote is pending |
| `handshake` | `{step}` | a network session's handshake reached a step, each once per `connect()` and in order: `step` `"signalling"` when the server answered the offer over the signalling socket, `"ice"` when the data channel opened (ICE and DTLS done). The login comes next (`syncing` once accepted). A session that ends in `connecting` failed in the step after the last one reported: no `signalling` = the signalling socket (unreachable, refused, no answer), `signalling` without `ice` = ICE/DTLS (no UDP path, a relay needed), `ice` = the login. Timing and the route (`getState().ice`) are the page's telemetry. |
| `round` | `{state}` | the game's round rhythm, in a network game and in the local game: `state` `"started"` when a round comes into play on this client (the game enters its play state: the round's cycles exist, the countdown runs; also the round the client joins in), `"ended"` when it leaves it while the session goes on (the server ended the round; the next one is being set up). Once per transition, in order (a standalone game that sets up the next round within one frame still gives both), so `started` and `ended` alternate, beginning with `started`; an `ended` is often followed by the next `started` at once. A session that ends (disconnect, leave, lost connection, the local game stopped) ends without an `ended` for its last round. The client does not know which match a round belongs to; a platform reports matches (`platform-contract/README.md`, "Progression"). |
| `scene` | `{state, transitioning}` | the attract scene changed: `{state: k, transitioning: true}` when the transition towards state `k` starts, `{state: k, transitioning: false}` when `k` is reached (its pose stands still from then on, except for state 4's circling bots). A multi step jump (`scene(4)` from state 1) emits one pair per step. `scene(k)` for the state the scene already stands in emits the "reached" event again right away. |
| `move` | `{moveId, serverId, name, signalUrl, reason}` | the server sends this client to another room (a platform's `move_players`, see "Moves" below): `serverId` and `signalUrl` of the target (as a Quick Play answer has them), `name` its name (may carry colour codes), `reason` `merge` / `drain` / `closer` / `admin`, `moveId` the platform's id of the move. The session goes on; the page is expected to leave (`disconnect()`) and connect to the target. |
| `error` | `{text}` | an API call was refused (not in a game, unknown item/action, key not bindable) |
| `exit` | `{code}` | the runtime exited (should not happen any more; sessions return to `disconnected`) |

Events are dispatched synchronously from the game loop (inside the animation frame
or a DataChannel message callback); keep handlers cheap and do not call `disconnect()`
from inside a `state` handler for the state being entered.

## The attract scene (`scene()`)

The play page's background is the game's own renderer, not page art: a scripted
standalone scene on the same canvas the game plays on, with the page's HTML
overlaid (the canvas has no alpha, its background is the game's black). Graphics
work on the game therefore reaches the page for free, and Play continues on the
same canvas — `connect()` and `startLocal()` take over from the scene directly.

What the scene is (`src/tron/gScene.cpp`): a bare grid built from the default map
(the 500x500 square, scaled by `SCENE_ARENA_SIZE`) with the game's textured floor
(`SCENE_FLOOR_DETAIL`) and, by default, no rim wall (`SCENE_RIM` 0: an open floor
to a black horizon; the rim still bounds the arena), **no `gGame`** (no rounds,
no scores, no win zone), **no players** (the
cycles have no `ePlayerNetID`, so `getPlayers()` stays empty), no HUD, no
scoreboard, no console overlay (`TEXT_OUT` is forced off and restored on
`sceneStop()`), no sky (`UPPER_SKY`/`LOWER_SKY` off), and one fixed camera that
follows nothing. While the scene runs, `getConfig` of `TEXT_OUT`, `SHOW_HUD`,
`SHOW_FPS`, `SHOW_TIME`, `UPPER_SKY`/`LOWER_SKY` reads the scene's 0 (and `FLOOR_DETAIL` the scene's `SCENE_FLOOR_DETAIL`), but `saveConfig()` and
`exportConfig()` write the player's own values, so saving a setting from the page
while the scene is the backdrop is safe. A `setConfig` of one of those items during
the scene is lost at `sceneStop()`: set them while a game runs, or wrap them in
`sceneStop()` / `scene(n)`.

**The replay (default, `SCENE_REPLAY` 1).** For now the floor does not follow the
states: it replays recorded Team Sumo rounds, one after the other in a loop, whatever
`k` the page asks for. `scene(k)` still answers with its `scene` event pair (the
transition ends on the next frame), so the page's contract is unchanged. Each clip is
the "late chaos" moment of a real round as the tronstats highlight reel defines it
(every team still alive at 65 s with no cycle owning the zone, else three teams at
75 s): the whole lobby squeezed together in the shrinking zone, about 22-25 s each,
from 3 s before the moment to 2.5 s after the crowd thinned out. The cycles are not
simulated: every frame each is placed where its recording says it was, its wall
turns at the recorded corners (missed turns between the log's samples rebuilt), it
explodes where and when its death line says (blowing the rounds' 3-unit hole into the walls
around), and its wall stands 7.6 s after; the
walls already on the floor when a clip starts are the round's own. The team zones
(Team Sumo's fortress zones on the arena centre) shrink with the round's clock, spin
as a fortress zone does (faster the closer it is to being conquered, from the
exported conquest level) and collapse when the game's conquest rules say they fell.
The camera (`SCENE_REPLAY_CAMERA_MODE`) follows the action. **Chase** (2) rides behind and above one cycle, its heading smoothed so sumo's constant
turning does not whip the view, in shots of `SCENE_REPLAY_CAMERA_SHOT` seconds that
cut to another cycle: one in the thick of the crowd, preferably one about to die (the
shot ends on its explosion, held 1.5 s). **Orbit** (1) watches the whole crowd from
above: it aims at the live cycles' centre, already leaning towards a cycle about to
die, backs off with the crowd's spread and circles it slowly; it glides and only
jumps when a clip starts. **Fixed** (0) stands where `SCENE_REPLAY_CAMERA_X/_Y/_Z/
_PITCH/_DIR` say. The data is
`src/tron/gSceneReplayData.h`, made by `web/wasm/scene_replay.py` from tronstats
match archives (which rounds, and how the walls and timing are rebuilt, is in its
docstring). With `SCENE_REPLAY` 0 (read when the scene starts) the scene plays the
states below instead.

The four states (`SCENE_REPLAY` 0), transitions three seconds each (`SCENE_TRANSITION_TIME`):

| k | what is on the floor |
|---|---|
| 1 | one cycle in the local player's colour (`COLOR_R/G/B_1`) drives in from the left, stops mid floor, its trail behind it |
| 2 | a red cycle drives in on a parallel track and stops alongside: two parallel trails, a duel pose (cyan instead of red when the player's own colour is close to red - the game gives a fresh profile one of four random colours) |
| 3 | four more cycles in different cycle colours come in from the horizon and fan out, then stop |
| 4 | the extra cycles (2 and 3) drive off; the one cycle stays and two AI-grey bots circle a square each |

`scene(k)` with `k` above the current state plays the transitions forward one at
a time (`scene(4)` from state 1 plays 1→2, 2→3, 3→4 and emits a `scene` event
pair for each). With `k` below it, the cycles a state does not show **un-park and
keep driving the way they were pointing until the transition ends, then they and
their trails are removed** — a plausible "they drive off", *not* a true time
reversal (nobody drives backwards, and nothing is rewound). Coming back up
re-spawns them from their entry point.

Nothing in the scene can die by itself: the scene's cycles override `EdgeIsDangerous` to
`false`, so no wall, trail or rim is ever a collision for them and the choreography
never has to be exact (the replay's deaths are the recording's, not collisions). The tracks are laid out so
that no cycle crosses another's trail while a state is being held; during a
transition a retiring cycle may drive through one (it is on its way out).
Trails do not decay while a cycle is alive; `SCENE_WALLS_LENGTH` caps their
length (the state 4 squares are deliberately longer than that, so a circling bot
never catches its own tail).

Settings (all `tSettingItem`s, so they are read and written through
`getConfig`/`setConfig`/`console` like any other, and like the custom camera they
are *not* part of `user.cfg`; they only exist in the browser build):

| setting | default | what it does |
|---|---|---|
| `SCENE_CAMERA_X` | -0.88 | camera position along x, as a fraction of the arena's half width from its centre (-1 is the rim) |
| `SCENE_CAMERA_Y` | 0 | the same along y |
| `SCENE_CAMERA_Z` | 6 | camera height above the floor, in grid units |
| `SCENE_CAMERA_PITCH` | -0.18 | the slope of the view (dz per unit driven forward); negative looks down |
| `SCENE_CAMERA_DIR` | 0 | heading in degrees, 0 = along +x (the default camera looks east across the whole grid) |
| `SCENE_CAMERA_FOV` | 80 | field of view, as the game's cameras use it |
| `SCENE_TRANSITION_TIME` | 3 | seconds one state change takes |
| `SCENE_WALLS_LENGTH` | 70 | grid units of trail kept behind a cycle (the four states; the replay uses its rounds' 400) |
| `SCENE_REPLAY` | 1 | play the recorded rounds instead of the four states; read when the scene starts |
| `SCENE_REPLAY_X` | 0.4 | where the recorded arena's centre lies on the floor, x as a fraction of the arena (0 is the west rim, where the default camera sits) |
| `SCENE_REPLAY_Y` | 0.5 | the same along y |
| `SCENE_REPLAY_SPEED` | 1 | the replay's playback rate (applies from the next clip) |
| `SCENE_REPLAY_ZONE_HEIGHT` | 0.5 | how tall the replay's zones are drawn (the game's `ZONE_HEIGHT`, 5 in a game; restored on `sceneStop()`) |
| `SCENE_REPLAY_CAMERA_MODE` | 3 | the replay's camera: 0 fixed, 1 orbit (the whole crowd from above), 2 chase (behind one cycle), 3 cinematic (the chase, slowly rising up over the arena and back down: `SCENE_REPLAY_CINEMA_*`), 4 player (the player's own custom camera on one cycle, below) |
| `SCENE_PLAYER_CAMERA` | 0 | 1: the player camera whatever `SCENE_REPLAY_CAMERA_MODE` says; **not saved** (the play page's Settings sheet turns it on while its CAMERA tab is open, so a save meanwhile does not keep the scene on it). The player camera is the game's own `eCamera` in `CAMERA_CUSTOM` with the live `CAMERA_CUSTOM_*` / `CAMERA_GLANCE_*` values and `START_FOV_1`, on the cycle that lives longest from now; no timed cuts, a new cycle 0.8 s after the followed one dies |
| `SCENE_PLAYER_COURSE` | 0 | with the player camera, a course instead of the recorded rounds: one cycle in the player's colour (`COLOR_*_1`) driving a track generated a few seconds ahead, at `SCENE_PLAYER_SPEED`, turning towards the middle of the stadium's floor when the floor runs out: 1 a mixed loop, 2 steady straights, 3 S-bends (one turn each way), 4 reversals (two quick turns the same way), 5 a brake to a quarter of the speed and back up; the part can change at any time (it takes the next section, within about 3 s). 0 goes back to the recorded rounds at the next clip. Not saved |
| `SCENE_PLAYER_COURSE_WALLS` | 40 | the course's trail, grid units (the rounds keep their long walls) |
| `SCENE_PLAYER_GLANCE` | 0 | the player camera's glance, held while set: 1 left, 2 right, 3 back (the scene takes no keys); not saved |
| `SCENE_PLAYER_SPEED` | 30 | m/s: the pace the player camera takes the replay at. The recordings are a fast room's and play at `SCENE_REPLAY_SPEED` (about 58 m/s), and the custom camera pulls back and rises with speed (`_FROMSPEED`), so while the player camera runs the replay cycles' `Speed()` is scaled to bring the clip's mean to this (the stock `CYCLE_SPEED`); they still move as recorded |
| `SCENE_REPLAY_CAMERA_ORBIT` | 70 | seconds the orbit camera takes to circle the crowd once (0: no orbit) |
| `SCENE_REPLAY_CAMERA_SHOT` | 7 | seconds the chase camera follows one cycle before it cuts to another |
| `SCENE_REPLAY_CAMERA_X/_Y/_Z/_PITCH/_DIR/_FOV` | -0.45 / 0 / 22 / -0.4 / 0 / 70 | the fixed camera while the replay plays (`_FOV` for every mode), meant as the `SCENE_CAMERA_*` ones: higher and closer, over the recorded zone's rim looking in (the states keep `SCENE_CAMERA_*`; separate so that values players already saved for those do not hold the replay's framing back) |
| `SCENE_ARENA_SIZE` | 0.5 | the arena size multiplier the scene builds its grid with (500 map units times this), pinned so the backdrop looks the same whatever a game before it left behind |
| `SCENE_RIM` | 0 | draw the rim wall while the scene runs (0: hidden, the floor runs to the horizon) |
| `SCENE_FLOOR_DETAIL` | 3 | the floor while the scene runs, `FLOOR_DETAIL` values: 0 off, 1 lines, 2 the grid texture, 3 two textures; the game's own `FLOOR_DETAIL` is restored on `sceneStop()` |
| `SCENE_DEBUG` | 0 | log every spawn, turn and park of the director (and each replay clip's start) as `BROWSER: scene: ...` |
| `SCENE_STADIUM` | 1 | the stadium around the scene's arena (`src/tron/gStadium.cpp`; the modelled one from `art/stadium/`, see `STADIUM_MODEL`): two tiers of stepped rows with a seated crowd, a roof with floodlight banks, light cones and pools of light on the floor, scoreboards, the lights reflected in the floor; 0 hides it. Not saved yet |
| `SCENE_STADIUM_FIT` / `SCENE_STADIUM_MARGIN` | 1 / 16 | while the replay plays, the stands stand around the area its clips use (every clip's walls while it plays, plus `SCENE_STADIUM_MARGIN` grid units, within the arena) instead of the whole arena: the recorded map keeps its size and the stands, at the arena's scale, come in closer (250x250 -> 150x201 for the shipped clips). 0: around the arena. The scripted states (no replay) and games always use the arena. Read when the scene starts |
| `SCENE_VIEW` | 0 | 1: a camera for looking at the whole stadium, over every other scene camera: it circles the centre of the stadium's floor (`SCENE_STADIUM_FIT`) at `SCENE_VIEW_DISTANCE` (1: the floor's edge in the camera's direction; every 0.1 more is another tenth of half the arena out, among the stands, which keep the arena's scale; negative: on the far side of the centre, still facing the same way, i.e. close to a stand and looking at it), `SCENE_VIEW_HEIGHT` grid units up, starting at `SCENE_VIEW_ANGLE` degrees and turning `SCENE_VIEW_SPEED` degrees a second (0 stands still; it turns with `SCENE_FREEZE` too), looking at the point `SCENE_VIEW_LOOK_HEIGHT` above the centre, field of view `SCENE_VIEW_FOV`. Not saved |
| `SCENE_VIEW_DISTANCE/_HEIGHT/_ANGLE/_SPEED/_LOOK_HEIGHT/_FOV` | 1.55 / 104 / 200 / 3 / 0 / 75 | see `SCENE_VIEW`; the defaults are a high establishing orbit inside the bowl, turning once in two minutes |
| `SCENE_REPLAY_CHASE_BACK/_HEIGHT/_LEAD` | 16.5 / 6.5 / 8 | the replay's chase camera: grid units behind its cycle, above the floor, and how far ahead of the cycle it looks (sideways); 6 / 4 / 1.5 is close enough to see a cycle up close. The camera never leaves the stadium's floor |
| `SCENE_REPLAY_CHASE_PATH` / `_TURN` / `_FOLLOW` | 1.2 / 0.8 / 0.15 | smoothing on the cycle's turns (modes 2 and 3; seconds): the camera heads along the cycle's path averaged over `_PATH` (a sumo cycle turns 90 degrees several times a second), turns with a critically damped spring of time `_TURN` (no sudden change of turning speed), and follows the cycle's position with one of time `_FOLLOW`; camera, cycle and look point stay on one line, so the cycle stays in the middle of the frame however long the swing. Smaller is snappier |
| `SCENE_REPLAY_CHASE_FOLLOW_ALONG` | 0.1 | the spring (seconds) on how far the cycle is ahead of the followed position along the camera's heading: the camera stays close to `_BACK` behind the cycle without jerking back and forth with each leg of a zigzag |
| `SCENE_REPLAY_CHASE_TILT` | 0.4 | the tilt's own critically damped spring (seconds): the tilt that puts the cycle at `_FRAME` is not re-solved every frame from the live camera-to-cycle distance (which changes on every turn and bobbed the view up and down), the cycle moves on screen with its own weaving instead. 0 re-solves it every frame |
| `SCENE_REPLAY_FRAME_ZONE` | 1 | keeps the followed cycle inside a safe zone of the frame, clear of the page's menu column and wordmark: its position smoothed over `SCENE_REPLAY_FRAME_SMOOTH` is held inside the zone shrunk by `_MARGIN` (so its weaving around that stays inside the zone) by turning and tilting the view (never moving the camera), through springs: while it is inside, the view only drifts back to its aim over `_RECENTER`; when it would leave, the target moves just enough to keep it in, and the view's turn follows on a spring of `_CORRECT`. 0: the plain framing (the cycle in the middle across) |
| `SCENE_REPLAY_FRAME_X_MIN/_X_MAX/_Y_MIN/_Y_MAX` | 0.42 / 0.8 / 0.45 / 0.8 | the safe zone, in fractions of the frame from the left and from the top |
| `SCENE_REPLAY_FRAME_X` | 0.6 | where across the cycle rests (down: `SCENE_REPLAY_CHASE_FRAME`) |
| `SCENE_REPLAY_FRAME_MARGIN` / `_SMOOTH` / `_RECENTER` / `_CORRECT` | 0.12 / 0.3 / 1 / 0.4 | see `SCENE_REPLAY_FRAME_ZONE` (seconds but the margin) |
| `SCENE_CAMERA_TRACE` | 0 | 1 logs a `camtrace <time> <yaw> <x> <y> <height> <rise> <cycle heading> <subject> <pitch> <distance> <cycle screen y> <dt> <cycle x> <cycle y> <cycle screen x> <view turn>` line every frame while the replay camera follows a cycle (for measuring its motion). Not saved |
| `SCENE_REPLAY_CHASE_FRAME` | 0.62 | where the followed cycle sits in the frame, from the top (0..1); the tilt follows from it and the camera's height and distance, so a lower, farther camera keeps more of the stands above the horizon |
| `SCENE_REPLAY_CINEMA_PERIOD` | 15 | mode 3: seconds of one rise and fall; every clip starts low and eases up (a cosine) |
| `SCENE_REPLAY_CINEMA_HEIGHT_LOW/_HIGH`, `_BACK_LOW/_HIGH` | 5 / 24, 12 / 34 | mode 3: the camera's height and distance behind its cycle at the low, close end and at the high, far end |
| `SCENE_REPLAY_CINEMA_FRAME_HIGH` | 0.72 | mode 3: the cycle's place in the frame at the high end (from `SCENE_REPLAY_CHASE_FRAME` at the low end), a little lower so the stands show over the arena |
| `SCENE_REPLAY_CINEMA_HOLD` | 0.35 | mode 3 with reduced motion: the fixed point between low (0) and high (1) the camera holds |
| `SCENE_REDUCED_MOTION` | 0 | 1 holds the cinematic camera still (`SCENE_REPLAY_CINEMA_HOLD`); `aa_api.js` sets it from the browser's `prefers-reduced-motion` and follows changes. Not saved |
| `SCENE_EXPLOSION_RAYS` | 0.06 | the explosions' rays while the scene runs, a fraction of the stock 200 units (`EXPLOSION_RAY_LENGTH`; at full length they streaked across the stadium); restored on `sceneStop()` |
| `SCENE_FLOOR_REFLECT` | 0.25 | the trails' (and cycles') reflection in the floor while the scene runs (`FLOOR_REFLECT`, off in every quality tier for play); < 0 leaves the player's values; restored on `sceneStop()` |
| `SCENE_FLOOR_REFLECT_SPREAD` | 0.8 | how far down the mirror image reaches (`FLOOR_REFLECT_SPREAD`): short, so the reflection fades out close to the wall's foot like on a dark wet floor; restored on `sceneStop()` |
| `SCENE_DOF` | 1 | depth of field with bokeh over the scene (`src/render/rGLES2.cpp`, "depth of field"): the action in focus, the stands, floodlights and LED bands behind going soft, bright points opening into discs. Runs where the bloom runs (the Medium quality tier and up) on WebGL 2; nothing on WebGL 1 or the Low tier. Not saved |
| `SCENE_DOF_FOCUS` | 0 | the focus distance in grid units from the eye; 0 follows the scene camera's target (`SCENE_VIEW`: its look point; the follow camera: its cycle; the replay cameras: where the view meets the floor). The focus jumps with the camera, it never animates a pull |
| `SCENE_DOF_STRENGTH` | 7 | the largest blur radius in pixels at 1536 wide (the aperture); 0 turns it off |
| `SCENE_DOF_BAND` | 0.3 | the in-focus band, as `1 - focus/distance` behind the focus (0.3: the nearest tier of stands stays fairly crisp, the far stands and lights go soft) |
| `SCENE_DOF_NEAR` | 0.35 | the blur in front of the focus relative to behind it (0: the near field stays sharp) |
| `SCENE_DOF_THRESHOLD` / `SCENE_DOF_GAIN` | 0.55 / 6 | the brightness above which a pixel becomes a bokeh disc, and how strongly it is weighted up |
| `SCENE_DOF_SHAPE` | 6 | the disc's corners: 6 hexagonal, 0 round, 5..8 another polygon |
| `SCENE_DOF_RINGS` | 4 | the bokeh's quality: rings of taps, 1..4 (6, 18, 36, 60 taps) |

Tuning the depth of field from the browser console:

```js
aa.setConfig("SCENE_DOF", "0")                 // off, to compare
aa.setConfig("SCENE_DOF_STRENGTH", "20"); aa.setConfig("SCENE_DOF_BAND", "0.05")   // stronger, shallower
aa.setConfig("SCENE_DOF_SHAPE", "0")           // round bokeh (6: hexagonal)
aa.setConfig("SCENE_DOF_GAIN", "10"); aa.setConfig("SCENE_DOF_THRESHOLD", "0.45") // brighter, more discs
aa.setConfig("SCENE_DOF_FOCUS", "60")          // a fixed focus (0: follow the camera)
aa.setConfig("SCENE_DOF_NEAR", "0")            // keep everything in front of the focus sharp
aa.setConfig("SCENE_FLOOR_REFLECT", "0.1"); aa.setConfig("SCENE_FLOOR_REFLECT_SPREAD", "0.5") // reflections
aa.setConfig("BROWSER_QUALITY", "3")           // High tier (the depth of field needs Medium or up)
```

Looking at the stadium from the browser console (the play page's backdrop takes them live):

```js
// high establishing orbit over the whole bowl
aa.setConfig("SCENE_VIEW", "1")
// a still, high view of the full bowl from inside the rim
aa.setConfig("SCENE_VIEW_SPEED", "0"); aa.setConfig("SCENE_VIEW_DISTANCE", "1.5"); aa.setConfig("SCENE_VIEW_HEIGHT", "138"); aa.setConfig("SCENE_VIEW_LOOK_HEIGHT", "0")
// low and wide, slowly turning
aa.setConfig("SCENE_VIEW_DISTANCE", "0.92"); aa.setConfig("SCENE_VIEW_HEIGHT", "8"); aa.setConfig("SCENE_VIEW_LOOK_HEIGHT", "18"); aa.setConfig("SCENE_VIEW_SPEED", "1.5"); aa.setConfig("SCENE_VIEW_FOV", "80")
// close to a stand, looking up at it
aa.setConfig("SCENE_VIEW_SPEED", "0"); aa.setConfig("SCENE_VIEW_DISTANCE", "-0.86"); aa.setConfig("SCENE_VIEW_HEIGHT", "9"); aa.setConfig("SCENE_VIEW_LOOK_HEIGHT", "26")
// back to the replay's own camera
aa.setConfig("SCENE_VIEW", "0")
// the cinematic follow (slowly up over the arena and back down), and back to the chase
aa.setConfig("SCENE_REPLAY_CAMERA_MODE", "3")
aa.setConfig("SCENE_REPLAY_CINEMA_PERIOD", "15"); aa.setConfig("SCENE_REPLAY_CINEMA_HEIGHT_HIGH", "24")  // faster, higher
aa.setConfig("SCENE_REPLAY_CAMERA_MODE", "2")
```

The stadium's own settings apply to the scene and to games alike (the scene forces only
`SCENE_STADIUM` over `STADIUM`); none is saved yet:

| setting | default | what it does |
|---|---|---|
| `STADIUM` | 0 | the stadium in games (local and network): 0 off, 1 on. It only shows with a low rim (`HIGH_RIM 0`) and no sky (`UPPER_SKY 0`, `LOWER_SKY 0`) |
| `STADIUM_INTENSITY` | 1 | crowd lights and floodlights (a livelier ranked presentation would raise it) |
| `STADIUM_REFLECT` | 0.5 | how strongly the floor reflects the stadium's lights; 0 off |
| `STADIUM_SIZE` | 1 | the bowl's scale (it always follows the arena's bounds) |
| `STADIUM_SCOREBOARD` | RETROCYCLES | the scoreboards' dot matrix text: A-Z, 0-9, space, `- . : ! /`, at most 40 characters |
| `STADIUM_LISTS` | 1 | draw the static parts from compiled display lists (0: immediate mode, for measuring) |
| `STADIUM_MODEL` | 1 | the modelled stadium (`models/stadium/*.stm`, `textures/stadium/crowd.png`, made by `art/stadium/`); 0 or missing files: the procedural one |
| `EXPLOSION_RAY_LENGTH` | 1 | the explosions' rays, a fraction of the stock length (the scene sets its own, `SCENE_EXPLOSION_RAYS`) |

Changing a camera, `SCENE_RIM` or `SCENE_FLOOR_DETAIL` setting takes effect on the
next frame (they are applied from the settings every timestep), so a page can pan
or tilt the backdrop live.
The choreography itself is in normalized arena coordinates and scales with
`SCENE_ARENA_SIZE`; the camera height and field of view do not.

## servers.json (the lobby stand-in)

The master servers speak UDP and are out of a browser's reach, so the page
gets its server list from a static `servers.json` next to it:

```json
[
  { "name": "this host", "url": "/signal", "description": "wss://<host>/signal through Caddy" },
  { "name": "EU fortress", "url": "wss://fortress.example.org/signal", "description": "sty+ct+ap" }
]
```

`url` is the signalling URL: absolute `ws://`/`wss://`, or a path resolved against the
page's own host (`AA.resolveSignalUrl`). Two optional fields are for platform login
(below): `serverId` (the server's id at the platform, its `PLATFORM_SERVER_ID`) and
`platformLogin: true` (the server accepts platform login tokens, `PLATFORM_LOGIN 1`).
Without them a page never sends a token to that server:

```json
{ "name": "retrocycles dev", "url": "ws://127.0.0.1:4535/", "description": "",
  "serverId": "dev-local-1", "platformLogin": true }
```
 The test page shows the list as a `<select>`
that fills the URL field; the deploy (`web/deploy`) ships the file next to the bundle.
A page served over https must use `wss://` entries (mixed content is blocked). A real
lobby - a small HTTP directory that polls the master list or that servers announce
themselves to - would replace it; its output should be this same document.

## Persistence

`AA.prepare` mounts IDBFS on `/aa/user/var` (the client's `var/` directory: `user.cfg`,
logs) and on `/aa/user/resource` (downloaded maps, see `fetching`) before the runtime
starts and waits for the load from IndexedDB, so
`user.cfg` — player name, colors, key bindings, camera and every other `savable`
item — survives reloads. On the first run (no `user.cfg` in the mount) the client
seeds it from `/aa/etc/browser_user.cfg` (name `Browser`, the key template `browser_keys.cfg`). Saving
happens on `saveConfig()`, `importConfig()` and at every session end (`st_SaveConfig`
writes `user.cfg`, then `FS.syncfs` pushes the mount to IndexedDB); the IndexedDB write
is asynchronous (allow ~1 s before closing the tab). Without IndexedDB (private windows
in some browsers, `options.persist === false`) the directory is plain MEMFS and
`aa.persistence` says `memfs`. Non-persistent per-run settings live in
`/aa/etc/autoexec.cfg` (window size, tooltips), loaded after `user.cfg`.

The store is per browser profile and per page origin (IndexedDB database `/aa/user/var`
of the page's origin): another browser, another profile, a private window or another
host serving the page starts from the defaults, and clearing the site's data wipes it.
`exportConfig()` / `importConfig(text)` are the way to carry settings across: the
text is `user.cfg` itself (`KEYBOARD <keysym> <action> <player>` lines for the key
map, `PLAYER_1`, `COLOR_*_1`, camera and video settings ...), pasted into another
page's import. Password hashes (`PASSWORD` lines, `PASSWORD_STORAGE 1`) are left out
of the export on purpose.

Settings that are not part of `user.cfg` (`savable: false` in `getConfig`: the
`settings_*.cfg` values, `tSettingItem`) would be lost on reload; natively the user puts
them into `var/autoexec.cfg` by hand. The browser client writes that file itself on
every save with a fixed list — the custom camera: `CAMERA_CUSTOM_BACK`, `_RISE`,
`_PITCH`, `_TURN_SPEED`, `_BACK_FROMSPEED`, `_RISE_FROMSPEED`, `_ZOOM`,
`_TURN_SPEED_180`, the glance (`CAMERA_GLANCE_BACK/_RISE/_PITCH/_BACK_FROMSPEED/_RISE_FROMSPEED`) and `CAMERA_IN_TURN_SPEED`, and the attract scene's `SCENE_CAMERA_X/_Y/_Z/_PITCH/_DIR/_FOV`,
`SCENE_TRANSITION_TIME`, `SCENE_WALLS_LENGTH`, `SCENE_ARENA_SIZE`, `SCENE_RIM`,
`SCENE_FLOOR_DETAIL`, `SCENE_REPLAY`, `SCENE_REPLAY_X/_Y/_SPEED/_ZONE_HEIGHT`, `SCENE_REPLAY_CAMERA_*` including `_MODE`/`_ORBIT`/`_SHOT`, the display's `BROWSER_MSAA`, `BROWSER_BLOOM`,
`BROWSER_BLOOM_INTENSITY`, the quality tier settings and `TRAIL_GLASS` (the player's while the quality is Custom; `gBrowserSettingsText` in
`src/tron/gBrowser.cpp`) — and
`exportConfig()` appends the same lines, so they survive reloads and travel with the
export. Any other `savable: false` item set through `setConfig`/`console` lasts until
the reload.

### The in-game console overlay: `TEXT_OUT`

`TEXT_OUT` (bool, saved in `user.cfg`, default 1) is the game's switch for the
scrolling console lines drawn over the game view (round results, chat echo,
"watching ...", setting changes). The HUD (scores, speed, rubber) is not affected.
The native client forces it on in network games (its console is its only text
display); the browser build honours it in the game too — the page has its own log from
the `console` events, which keep arriving whatever `TEXT_OUT` is. The test page's
"hide/show game log" button is `setConfig("TEXT_OUT", "0"/"1")` + `saveConfig()`. The
connecting phases (login, sync) still show the console regardless.

### Diagnostics: `BROWSER:` lines, `BROWSER_CONSOLE_LOG`

The client's own diagnostics - connection progress (`signalling ...`, `data channel open`,
`ice route ...`, `disconnected <reason>`), the `status netstate=...` line every 5 s,
`visibility ...`, `focus lost ...`, `platform login token queued.`, `login request ...`,
`resource ...`, `scene: ...` - are printed to stdout with the `BROWSER: ` prefix:
`Module.print`, i.e. the page's browser console. They are not written to the game console, so
players see only the game's own text on the canvas and in the `console` events.

`BROWSER_CONSOLE_LOG` (bool, default 0, local, not saved: `savable: false`, back to 0 on
reload) echoes them on the game console too (the canvas overlay and `console` events), for
manual debugging: `aa.setConfig("BROWSER_CONSOLE_LOG", "1")` or `BROWSER_CONSOLE_LOG 1` in
the console.

### Frame rate: `UNCAPPED_FPS`, `MAX_FPS`

By default the game draws one frame per display refresh (`requestAnimationFrame`),
plus one at once for every turn key. `UNCAPPED_FPS` (bool, saved in `user.cfg`,
default 0) is the native client's "vsync off": while a game runs (network session
or local game, not the scene, not a hidden tab) the frames come from a
MessageChannel loop as fast as `MAX_FPS` (int, saved, default 360, 0 = no limit)
allows, and the low latency canvas shows each one as soon as it is drawn — the
scanout picks up the newest frame instead of the one from the last refresh.
The state object's `fps` shows the rate. The price is a busy main thread (one
core at 100 % at 300+ fps; laptops get warm), so it is a player's choice: the page
should expose it as a setting, applied with `setConfig` + `saveConfig` (it takes
effect within a frame, no reconnect). Both settings are the game's own `MAX_FPS`
and a browser-only `UNCAPPED_FPS`; Firefox and Safari have no low latency canvas,
so there the extra frames only cost CPU and show on the display's refreshes. The loop
waits for an animation frame whenever a refresh is overdue, so it never starves the
browser's own: with `MAX_FPS 0` the animation frames had fallen to ~30 a second on a
144 Hz display (and Firefox showed only those).

### Antialiasing: `BROWSER_MSAA`

`BROWSER_MSAA` (int, default 0 = off; a `tSettingItem`, persisted through the
`var/autoexec.cfg` list above) is the sample count of the multisampled framebuffer the
browser client draws into on WebGL 2 (`web/wasm/sdl_shim.cpp`); it is resolved into
the offscreen frame before the copy into the canvas that every frame ends with, so the
low latency presentation is unchanged. `2`, `4` and `8` are the usual choices; the value is clamped
to the context's `GL_MAX_SAMPLES` (4 or 8 on most GPUs, 0 on some software
renderers), and the status line's `msaa=` shows the count in use. It takes effect
within a frame (the renderbuffers are recreated, like on a resize), so the page applies
it with `setConfig` + `saveConfig`, no reconnect. On WebGL 1 there is no offscreen
frame: the setting is ignored and one `BROWSER: BROWSER_MSAA ignored: no offscreen
frame (WebGL 1).` line says so. The cost is the GPU's (memory and fill for the
samples), nothing on the main thread; the HUD and console text are drawn as textured
quads and are unaffected.

### Bloom: `BROWSER_BLOOM`, `BROWSER_BLOOM_INTENSITY`

`BROWSER_BLOOM` (int, default 0 = off) and `BROWSER_BLOOM_INTENSITY` (0..1, default
.35; both `tSettingItem`s, persisted through the `var/autoexec.cfg` list above) add a
soft glow around the *emissive* parts of the picture on WebGL 2 — and only those. The
mask is not a brightness threshold over the finished frame: the GL layer remembers the
draws whose material declares `GL_EMISSION` under per pixel lighting, which in this
game is exactly the cycle trim of `CYCLE_LIGHTING 1` below, draws them once more into
an emission buffer (the emissive term alone, depth tested against the finished frame),
blurs that at a quarter of the resolution and adds `BROWSER_BLOOM_INTENSITY` of it to
the frame before the copy into the canvas. Walls, the floor, trails, the HUD, the
console text and the menus are never part of the mask and never glow, whatever their
brightness; wall edges stay as crisp as with the setting off. `CYCLE_LIGHTING 0`
leaves no emission to pick up, so bloom does nothing there. Both settings take effect
within a frame, no reconnect (`setConfig` + `saveConfig`); the status line's
`bloom=<0|1>/<intensity>` reports them. On WebGL 1 there is no offscreen frame to post
process: the setting is dropped with one `BROWSER: BROWSER_BLOOM ignored: no offscreen
frame (WebGL 1).` line. The cost is fill rate (an emission buffer the size of the
frame and one blended pass over the frame); on a GPU it is small, on a software
rasterizer it is not (about 10 ms per frame under SwiftShader at 1280x720). It works together with `BROWSER_MSAA`
(the frame is resolved first, the multisampled depth with it).

### The cycle's shading: `CYCLE_LIGHTING`

`CYCLE_LIGHTING` (int, default 1, not saved: set it through `console`) picks how the
light cycles are lit. 0 is the game's per vertex lighting. 1 shades them per pixel in
the browser's GL layer and makes the wheels (`CYCLE_GLOW_WHEEL`, 0..1, default 1) and
the body (`CYCLE_GLOW_BODY`, default .25) glow in the player colour, so the colour
reads from the unlit side too and never exceeds the texture's own colour. Walls and
the floor are untouched either way.

### The arena look: `FLOOR_STYLE`, `WALL_STYLE`, `BROWSER_WALL_GLOW`, `WALL_EMIT`, `FLOOR_REFLECT`, `WALL_CONTACT_GLOW`, `ARENA_HAZE`

Settings of the game (`tSettingItem`s, not part of `user.cfg`: set them through
`console`, or through `setConfig` and carry them yourself), **all default off, so the
stock look is what a player who sets none of them gets**. Together they are the
"modern film arena" look — a wet reflective floor, light ribbons, lit haze. Each is
independent and can be judged on its own.

- `FLOOR_STYLE` (int, 0): `1` swaps `textures/floor.png` for
  `textures/floor_tron.png`, one dark grid cell with a soft line, a chamfered edge and
  a noise panel; `2` for `textures/floor_tron_wet.png`, the same cell with a darker
  panel and a brighter, slightly wider line.
  Both are built for `FLOOR_DETAIL 2` (one texture tiled in both axes); `FLOOR_DETAIL
  3` stretches two textures along one axis each and would smear them. The colour is
  still `FLOOR_RED`/`_GREEN`/`_BLUE` (default `.2/.2/.2`, a neutral grey); the cool
  grid is `FLOOR_RED .10`, `FLOOR_GREEN .20`, `FLOOR_BLUE .30`, the alternative
  `config/settings_visual.cfg` already ships commented out. The texture is
  also the upper sky's, so the setting changes that too. In the attract scene
  `SCENE_FLOOR_DETAIL` shadows `FLOOR_DETAIL`: set both.
- `WALL_STYLE` (int, 0): `1` is `textures/dir_wall_tron.png`, a bright crest along the
  top edge fading to a dim foot. `2` is `textures/dir_wall_glass.png`, the translucent
  ribbon: grey **and alpha**, the body at .42 opacity with the zigzag as structure
  inside it, crest and foot opaque. The floor then shows through a trail. The two edges
  a player reads a wall's position from stay hard, but the game does not depth sort
  its trails, so two translucent walls crossing show through each other — a look
  choice, not a default.
- `BROWSER_WALL_GLOW` (float, 0): puts the trail walls into the bloom mask with that
  factor, so the ribbons halo and the halo lands on the floor beside them. Needs
  `BROWSER_BLOOM 1`; browser only.
- `WALL_EMIT` (float, 0) with `WALL_EMIT_NEAR` (14) and `WALL_EMIT_FAR` (45), in grid
  units from the eye: one more additive pass of the trail quads **where they already
  are** — the ribbon's own light on top of the ribbon. Blending only adds, so the
  background term of a translucent wall is untouched and the floor still shows
  through; what changes is that the body gains brightness and saturation and the crest
  clips towards white. This is the single change that makes a trail read as a light
  source rather than a painted band at arena distance.
  The ramp is not optional. The same factor that does that across the arena blows a
  trail standing right next to the camera out to a white wash, so a vertex nearer than
  `WALL_EMIT_NEAR` contributes nothing and the pass fades in to full at
  `WALL_EMIT_FAR`. `WALL_EMIT_FAR <= WALL_EMIT_NEAR` turns the ramp off and applies
  the factor everywhere (for a look at what it does, not for use). Native and browser.
- `FLOOR_REFLECT` (float, 0) with `FLOOR_REFLECT_SPREAD` (2.5) and
  `FLOOR_REFLECT_FADE` (0): an extra additive pass that draws the trails again,
  mirrored through the floor plane and stretched down by the spread, at that fraction
  of their brightness, fading to `FLOOR_REFLECT_FADE` of it at the far end. The floor
  writes no depth, so the mirrored copy is hidden by walls standing in front of it and
  by nothing else. With `BROWSER_WALL_GLOW` it joins the bloom mask too, which is what
  turns it from a second wall into a smear. Native and browser (the bloom half is
  browser only).
- `WALL_CONTACT_GLOW` (float, 0) with `WALL_CONTACT_HEIGHT` (.06): the same pass with
  the trail squashed onto the floor instead, so the wall texture's bright crest lands
  in a tight line where the wall meets the ground.
- `ARENA_HAZE` (float, 0 = off) with `ARENA_HAZE_START` (0 = a quarter of the end) in
  grid units and `ARENA_HAZE_RED`/`_GREEN`/`_BLUE` (.03/.05/.09): linear distance fog
  over the arena view — the walls, the cycles, the rim, the floor plane and the sky
  plane. Emissive draws fade out with it rather than picking the colour up. Off before
  the HUD and the console, which are never hazed. Native and browser.
  It costs contrast without adding much glow (above the horizon there is no geometry
  for fog to touch), so it isn't part of the look below. Keep it at 0 unless a map
  wants fog for its own reasons.

**The recommended look** at gameplay distance:

```
FLOOR_STYLE 2   FLOOR_DETAIL 2   SCENE_FLOOR_DETAIL 2
FLOOR_RED .10   FLOOR_GREEN .20  FLOOR_BLUE .30
BROWSER_BLOOM 1 BROWSER_WALL_GLOW 1   BROWSER_BLOOM_INTENSITY .45
WALL_STYLE 1    WALL_EMIT .85    WALL_EMIT_NEAR 14   WALL_EMIT_FAR 45
FLOOR_REFLECT .5   FLOOR_REFLECT_SPREAD 4
WALL_CONTACT_GLOW .8
ARENA_HAZE 0
```

With `WALL_STYLE 2` instead (opt-in; the translucent ribbon is what makes the close-up
work), raise `WALL_EMIT` to `1.0` — the body is see-through and needs more of its own
light to reach the same reading. Everything else is unchanged.

The extra draws cost next to nothing on a desktop GPU; a software rasterizer pays for
the fill.

### The trail look: `TRAIL_GLASS`, `BROWSER_BLOOM_WIDE`, `BROWSER_BLOOM_SPILL`

Browser only (WebGL 1 and 2), `tSettingItem`s like the arena look; the native client has
none of them.

- `TRAIL_GLASS` (0; every quality tier sets 1, the player's own value while the quality
  is Custom, saved in `var/autoexec.cfg`; the play page's Classic/Modern row): with 1 the
  trail walls are drawn with the alpha blended objects, after every cycle, and without
  their glow (`BROWSER_WALL_GLOW` and `WALL_CONTACT_GLOW` leave them out); 0 is the
  classic look, where they glow. The walls themselves are the game's own (`WALL_STYLE`)
  either way.
- `BROWSER_BLOOM_WIDE` (0 off): the weight of a second bloom level blurred at a
  sixteenth of the frame, four times wider than the first - the long soft halo.
  `BROWSER_BLOOM_SPILL` (0 off): the composite hands what exceeds a quarter of full
  scale in its brightest channel to all three, so strong glow burns towards white
  instead of clipping per channel (additive amber over amber otherwise stops at
  yellow). Both apply to any bloom; both need `BROWSER_BLOOM 1`.

### The lens shift: `BROWSER_LENS_SHIFT`

`BROWSER_LENS_SHIFT` (real, -0.9..0.9, default 0, **not saved**) moves the 3D view's centre
sideways by that share of the half width (normalized device coordinates: -0.5 puts the centre
at a quarter of the width from the left), in the scene and in a game alike: a translation in
clip space before the frustum (`rViewport::Perspective`, browser build only). The camera, its
numbers and the field of view are unchanged; the HUD and the console set their own projection
and stay put; name tags move with the view. The play page sets it while its Settings sheet covers
part of the screen, so the cycle the camera centres shows in the middle of the free part
(`left / width - 1` with the sheet at the right, `right / width` with it at the left), and puts it
back to 0 when the sheet closes. It takes effect on the next frame.

### The gauges' shape: `HUD_GAUGE_KEEP_ASPECT`

`HUD_GAUGE_KEEP_ASPECT` (bool, default 0, **not saved**; `src/tron/gHud.cpp`, every client
build) keeps the HUD's speed, rubber and brake gauges in the proportions they have on a 4:3
screen whatever the screen's shape: every horizontal measure of a gauge (the needle, its 0 and
maximum, the value, the title and their letters' width) is multiplied by `(4/3) * height /
width`, so on an upright or square screen the gauges are round and their words unsqueezed, and
on a wide one narrower than the stretched default. Their sizes and places stay the
`*_GAUGE_SIZE` / `*_GAUGE_LOCX` / `*_GAUGE_LOCY` settings (screen fractions); with it on, a
gauge of size `s` is `s * height / 2` pixels high, about `(4/3)` of that wide, its words `.24`
of it tall. Off, the gauges stretch with the screen as they always have. The play page turns it
on with the rest of its compact HUD (`web/page/src/compact.ts`) and lays the gauges out in
pixels; a gauge's cached drawing is redrawn when the screen's shape changes.

### Graphics quality: `BROWSER_QUALITY`, `BROWSER_RENDER_SCALE`

`BROWSER_QUALITY` 0 Auto (the default), 1 Low, 2 Medium, 3 High, 4 Ultra, -1 Custom
(`src/tron/gBrowserQuality.cpp`). A tier sets the costly look settings above
(bloom, `BROWSER_MSAA`, `TRAIL_GLASS`, `FLOOR_REFLECT`, off in every tier for now). Custom leaves the look settings to the player.
The resolution is the player's alone, in every mode: the device's full resolution (the canvas
backing store's pixels per CSS pixel = the device pixel ratio, at most 2) times
`BROWSER_RENDER_SCALE` (0..1, the play page's RESOLUTION row); no tier and not Auto lowers
it, since the HUD's text and the cycles are drawn into the frame and a lower density stretches
them soft. Auto measures the frame interval and walks the tiers Low, Medium, High, Ultra:
down after two seconds mostly over budget, up after eight comfortable seconds, with a growing
wait after a failed try; it remembers its step (`BROWSER_QUALITY_LEVEL`, 0..3). Its first step
on a new machine comes from the GPU's name: Ultra for NVIDIA/AMD, High for Apple silicon
named by Chrome ("Apple M..."), Medium for Safari's "Apple GPU" (which Intel Macs report too), Low
for the rest (Intel, Mali, Adreno, PowerVR, unknown) and for a software renderer. All three are
saved. The status line shows `quality=auto/<step>:<tier> slow=<share of slow frames> density=`.
Menu: Display Settings, Graphics Quality.

## Authentication (armathentication)

Servers with `ACCESS_LEVEL_PLAY` above the default (or that grant rights to logged in
users) authenticate players through the Krawall protocol: the server sends a password
request with a username and a salt, the client scrambles the password with the method
the server named (`md5` for authorities and local accounts, the legacy `bmd5`) plus the
salt and the address it connected to, and sends the hash back; the server checks it
with the authority (`user@forums` and friends, over HTTP from the server) or with its
own `LOCAL_USER` accounts. The desktop client asks for the password in a modal menu;
the browser client cannot block, so `ePlayer.cpp`'s password callback hands the request
to the page (`authRequest`) and `nAuthentication` keeps it open until `auth()` /
`authCancel()` (`src/network/nAuthentication.cpp`, "deferred" answers) or the session
ends. What starts a login: `login(globalId)` from the page (the desktop menu's item;
`AUTO_LOGIN_1 1` + `USER_1` does it at every connect), or the `/login user@authority`
chat command handled by the server. A remembered scrambled password
(`PASSWORD_STORAGE`) answers the next request without a prompt, so a reconnect is
silent; a refused password comes back as a new request with `failed: true`.

The "address" the client scrambles in is the protocol's pharming check: a desktop
client sends the server's `ip:port`, the server compares it with its own address /
`SERVER_DNS`. The browser client only knows the signalling URL, so it sends that URL's
`host:port` (`play.example.com:443`, `127.0.0.1:4535`) and the server, for peers that
arrive through a transport, compares the host alone with `SERVER_DNS` (as set or
resolved) or its known address. **A server that wants browser logins must set
`SERVER_DNS` to the name or IP the players' signalling URL uses** (or `TRUST_LAN 1`
for a LAN/loopback setup); otherwise every browser login fails with `Server address
mismatch`. A local test server can use `LOCAL_USER tester secret`,
`ACCESS_LEVEL_PLAY 15`, `SERVER_DNS 127.0.0.1`.

## ICE servers (STUN/TURN; the relay for networks without UDP)

By default the browser connects with host candidates only: it needs outbound UDP to the
server's WebRTC port range, and networks that block UDP (schools, offices, some mobile
carriers) cannot play. A TURN relay fixes that: the page passes ICE servers to the
connection,

```js
aa.connect(url, {
  loginToken,                                   // optional, "Platform login"
  iceServers: [ { urls: ["turn:turn.example.org:3478?transport=udp",
                         "turn:turn.example.org:3478?transport=tcp",
                         "turns:turn.example.org:443?transport=tcp"],
                  username: "1790003600:u_01J...", credential: "..." } ],
  iceTransportPolicy: "relay"                   // optional: relay only (tests); default "all"
});
```

and the browser's `RTCPeerConnection` gathers relayed candidates through them (UDP to the
relay, TCP, or TLS on 443 when even that is all that passes). The server needs nothing:
the relay talks UDP to the same public host candidates a direct browser uses. With UDP
open the direct pair still wins (ICE prefers host pairs), so ICE servers cost nothing
where they are not needed. `iceServers` entries are `RTCIceServer`s: `urls` a string or a
list of `stun:`/`stuns:`/`turn:`/`turns:` URLs, `username`/`credential` strings; anything
else is dropped (`AA.normalizeIceServers`). The list goes into the wasm as one JSON string
and from there to `new RTCPeerConnection({iceServers, iceTransportPolicy})` for that one
connection; it is never logged (the log says `ICE servers: 3 URLs (turn 2, turns 1, stun
0), policy all`) and never stored.

Where a page gets them, in this order (`web/page/src/platform/session.ts`):
1. **The platform** (`GET /v1/net/ice-servers`, `platform-contract/README.md`): a TURN
   relay with short-lived credentials (TURN REST API, HMAC of a secret the platform shares
   with the relay), fetched right before every connect next to the login token.
2. **Static configuration**: `platform.json` next to the page may carry `"iceServers"`
   (and `"iceTransportPolicy"`) beside or instead of `"apiUrl"` - a community host without a
   platform points its page at its own coturn:
   ```json
   { "iceServers": [ { "urls": ["turn:turn.example.org:3478?transport=udp",
                                "turn:turn.example.org:3478?transport=tcp"],
                       "username": "community", "credential": "..." } ] }
   ```
   Anyone who loads the page can read these credentials; the relay must therefore only
   relay to the operator's own game servers (coturn `allowed-peer-ip`).
3. Neither: connect without ICE servers, as before. A failed fetch never blocks the connect.

Which path was taken: the state object's `ice` (the selected candidate pair, from the
browser's `getStats()` once the data channel is open): `ice.local === "relay"` means the
session goes through a TURN server, `ice.relayProtocol` is the leg browser -> relay
(`udp`, `tcp`, `tls`). Stdout gets `BROWSER: ice route local=relay/tcp remote=host
protocol=udp.` when it is known (again if the pair changes). It stays in the state object after the session ends
(until the next `connect`), for telemetry.

TURN credentials are short-lived (the platform's last an hour): fetch fresh ones for every
connect and keep them only for that connect. A session that is already relayed keeps
running after its credential expired (coturn checks it when the allocation is made).

## WebSocket transport (pages that may not use WebRTC)

Some frames forbid WebRTC outright; a Discord Activity is one (its proxy carries HTTP and
WebSockets only). `connect(url, {transport: "websocket"})` keeps the same signalling URL
but creates no `RTCPeerConnection`: the glue sends `{"type":"websocket"}` on the signalling
socket, and once the server answers `{"type":"websocket"}` the game's packets travel as
binary frames on that socket, both ways. ICE servers are ignored. The server must have
`WEBRTC_WEBSOCKET_ENABLE 1`; otherwise it answers `{"type":"refused","reason":...}` and
closes, and the session ends at once (the console gets `rtc: the server refused game
packets over WebSocket: <reason>`). A server older than this feature ignores the request,
so the connect runs into the game's login timeout.

The state object's `ice` is `{"local": "websocket", "remote": "websocket", "protocol":
"tcp", "relayProtocol": null}` for such a session; stdout says `BROWSER: signalling <url>,
..., game packets over the signalling WebSocket.` The test page takes
`client.html?transport=websocket#ws://...`.

TCP delivers everything, in order: one lost segment holds back the packets behind it
(jitter on lossy links) where a data channel would just lose that one packet. Both ends drop
outgoing packets while more than about four packets are waiting (`WEBRTC_WEBSOCKET_SEND_CAP`
on the server, in bytes), as a congested UDP link would.

## Platform login (Retrocycles platform)

A game server built with `--enable-platform` and `PLATFORM_LOGIN 1` accepts a *login
token* instead of a password (`platform-contract/README.md` at the repository root, the
contract): a signed, 60 s, single use statement of the platform that this browser is
`gid` (`g-7f3a9c01@retrocycles` for a guest), minted for one server (`aud`). The page
gets it from its platform (`POST /v1/auth/game-token {serverId}` with the session cookie)
right before connecting and hands it over with `connect(url, {loginToken})` or, for a
session already running, `platformLogin(token)`. The client queues it and sends it as
network message 380 (`platform_login_token`, `ePlatform::SendLoginToken()`) as soon as
the connection is logged in, once per connection. The server verifies it offline
(signature, audience, expiry, `jti` unused) and logs the player in as `gid` exactly as a
successful armathentication login would: the console shows `... has been logged in as
g-7f3a9c01@retrocycles ...`, no `authRequest` event, no password. A refused token (expired,
replayed, for another server, unknown key) is logged on the server with the reason and
the player simply plays unauthenticated; the page learns nothing beyond the missing login
line.

Rules for pages:
- Only send tokens to servers that accept them: the `platformLogin: true` + `serverId`
  fields of `servers.json` (above) or the platform's own server list. Stock servers do
  not know message 380.
- Fetch a fresh token per connection; do not store tokens (60 s lifetime, single use). The
  platform session cookie (httpOnly) is the only credential, nothing login related belongs
  in IndexedDB or `localStorage`.
- A failed token fetch is not a reason to block the player: connect without it.
- The token never appears in any event, console line, log or export of the client; only
  `BROWSER: platform login token queued.` is printed (stdout).

`web/page/src/platform/` is the play page's side of it.

## Moves (Retrocycles platform)

A platform room can send a group of players to another room (rooms merged, a server
emptied, a closer server; `platform-contract/README.md` section 3, "Moving players"). The
classic redirect (`login_deny` with a `host:port`) does not work here, a `host:port` is no
signalling URL, so the browser client announces itself instead: once per connection, right
after the login, it sends network message 381 (`platform_client_caps`) with the word `move`
(servers that do not know the message ignore it). A room with a move for this player then
sends message 382 (`platform_move`) and the client raises the `move` event with the target.
Nothing else happens on its own: the session goes on until the page ends it. The room waits
about 10 s, then disconnects a client that is still there (reason `Moved to <name>.`).

What a page does with it: note that a move is
under way, `disconnect()`, wait for the `disconnected` state (not inside the `state`
handler), ask its platform for the seat in the target (`POST /v1/quickplay` with `moveId`),
then `connect()` there with a fresh login token and ICE servers, exactly as Quick Play does.
`signalUrl` lets a page without a platform follow too, without a login. A page that ignores
`move` loses the session after the grace with that reason, as if the server had ended it.

## What the browser build cannot do

- No in-game menus: the page builds them from the API (teams, spectate, login,
  settings, bindings, votes); `menuOpen` is the cue.
- Only one of network session / local game / attract scene runs at a time; there
  is no way to keep the scene rendering behind a game.
- The local game is the desktop one minus the menus: no way to change the map,
  the AI count or the game type mid-game other than through `console()` (the
  `SP_*` items, applied at the next round), and no pause call: the pause is the
  `PAUSE_GAME` action's key (`Pause`, default.cfg), a toggle; the play page's
  Training screen presses it with a synthetic `keydown`/`keyup` on `window` (the
  focus outside `data-aa-keys="page"`) and reads `gametime` to see it held
  (`web/page/src/components/Training.tsx`).
- The attract scene's reverse transitions are not a rewind: the cycles that
  leave drive on and are then removed, and coming back to a state re-spawns its
  cycles at their entry point.
- The scene draws no text at all (the page owns any wording over it); there is no
  hook to place a cycle or camera per frame from JS, only the `SCENE_*` settings.
- No server list / master server queries: the page must know the signalling URL. The
  game's `nServerInfo` polling uses UDP and is not reachable from a browser; a web
  lobby would be the replacement.
- Resource downloads go through the deployment's CORS proxy (see `fetching`); only maps and
  their DTDs are fetched, not textures/models/sounds referenced by moviepacks.
- Sound is the game's own mixer through Web Audio (sound effects only, no music); a
  page that never receives a user gesture stays silent (`getAudioState()`
  `suspended`). No fullscreen switching through the game (`TOGGLE_FULLSCREEN` is
  inert; use the Fullscreen API on the canvas), no screenshots (`SCREENSHOT` is
  inert; read the canvas).
- The key table is the US layout by physical code; `keyName` labels are US symbols.
- The reserved browser shortcuts (above) cannot be bound.
- Only player 1 is meaningful (one local player per browser); the key template
  (`browser_keys.cfg`) binds player 1 only. Browsers seeded before it may still hold
  `keys_cursor.cfg`'s player 2 bindings until the page moves them (`web/page/src/keys.ts`).
- Mouse motion is not bound in the browser: `default.cfg` puts the camera on it (keysyms
  324-327, `SDLK_MOUSE_*`; `getBindings()` with `code` null); `browser_user.cfg` unbinds them
  (`KEYBOARD 324 PLAYER_BIND NONE 1`: an unknown action clears a key) and the page drops them
  from older saved maps once (`keys.ts` `dropMouseCamera`). The wheel arrives as mouse buttons
  4/5 (333/334, the shim's `OnWheel`), bound to nothing; button 3 (332, the right button) keeps
  default.cfg's `ZOOM_IN` while held.
