// English, the source: the settings texts (see ../index.ts). Keys "settings.<what>".
// The Settings sheet (components/Settings.tsx, components/settings/*, settings/schema.ts): most
// labels show in caps by CSS (row keys, tabs, heads, buttons); hints and notices are sentences.
export const settings = {
  // the status bar's gear (an aria-label)
  "settings.cell": "Settings",
  // the sheet's title, and its tab row's aria-label
  "settings.title": "settings",
  // the plate's find field: its key, its placeholder (game words players type)
  "settings.find": "find",
  "settings.find.placeholder": "fov, brake, ping…",
  "settings.find.none": "Nothing called that. ADVANCED lists every setting by its game name.",
  // FIND's raw config names under the curated rows: the tab and group they are in
  "settings.find.every": "advanced · every setting",
  // the plate's button: opens the paste review
  "settings.import": "import",

  // ---- the tabs
  "settings.tab.player": "player",
  "settings.tab.controls": "controls",
  "settings.tab.camera": "camera",
  "settings.tab.graphics": "graphics",
  "settings.tab.hud": "hud & chat",
  "settings.tab.sound": "sound",
  "settings.tab.advanced": "advanced",

  // ---- group heads
  "settings.group.colour": "colour",
  "settings.group.browser": "this browser",
  // CONTROLS: driving keys, view keys, chat keys, the rest
  "settings.group.drive": "drive",
  "settings.group.look": "look",
  "settings.group.talk": "talk",
  "settings.group.game": "game",
  "settings.group.custom": "custom camera",
  // the camera's glance (looking left, right or back for a moment)
  "settings.group.glance": "glance",
  // a fold of lesser camera rows
  "settings.group.more": "more",
  // which cameras the switch-view key cycles through
  "settings.group.cycle": "switch view cycles through",
  "settings.group.arena": "arena",
  "settings.group.performance": "performance",
  "settings.group.effects": "effects",
  "settings.group.textures": "textures",
  "settings.group.display": "display",
  // the game's head-up display
  "settings.group.hud": "hud",
  // things drawn on the arena floor
  "settings.group.grid": "on the grid",
  "settings.group.instant": "instant chats",
  "settings.group.connection": "connection",
  "settings.group.every": "every setting",
  "settings.group.file": "config file",
  // the fold's lane: open it / close it
  "settings.more.less": "less",
  "settings.more.count": "{n} more",

  // ---- PLAYER
  "settings.row.name": "name",
  "settings.row.name.refused": "\"{name}\" is not allowed in games, so your name stays as it was.",
  "settings.row.colour": "cycle colour",
  "settings.row.language": "language",
  "settings.row.language.hint": "The page's words. The game's own text stays English.",
  "settings.row.shared": "this is a shared computer",
  "settings.row.shared.hint": "Sign-ins here end when the browser closes, and after a short idle time. Applies from the next sign-in.",
  "settings.row.account": "account",
  "settings.row.account.hint": "Sign-in methods, sessions, game passwords and Linked IDs are in your Profile.",
  // who may see you online, message you, join you
  "settings.row.privacy": "privacy",
  "settings.row.privacy.hint": "Appear offline, who may message you and join you: your Friends panel.",
  // buttons that open the Profile / the Friends panel
  "settings.act.profile": "profile",
  "settings.group.duels": "1v1 challenges",
  "settings.row.challenges": "challenges",
  "settings.row.challenges.hint": "Players in your room can challenge you to a 1v1 from your card. Off, nobody can; you can still challenge them.",
  "settings.row.duelBlocks": "blocked",
  "settings.act.unblock": "unblock",
  "settings.act.friends": "friends",
  // the colour's three channels (sliders 0-15)
  "settings.colour.red": "red",
  "settings.colour.green": "green",
  "settings.colour.blue": "blue",
  // the swatches (the desktop game's First Setup colours)
  "settings.colour.yellow": "yellow",
  "settings.colour.orange": "orange",
  "settings.colour.purple": "purple",
  "settings.colour.magenta": "magenta",
  "settings.colour.cyan": "cyan",
  "settings.colour.white": "white",
  "settings.colour.dark": "dark",

  // ---- CONTROLS
  // a phone's on-screen buttons: their layout
  "settings.row.touch-layout": "touch buttons",
  "settings.touch.standard": "standard",
  "settings.touch.keyboard": "keyboard",
  "settings.touch.alternating": "alternating",
  "settings.touch.keyboard.hint": "Two turn-left keys, a wider brake and two turn-right keys in one row at the foot, like d f, space, j k: each turn a single one.",
  "settings.touch.alternating.hint": "Left, right, a wider brake, left, right in one row at the foot, like the alternating keys: each turn a single one.",
  "settings.touch.standard.hint": "A turn and a double turn at each side, under the thumbs.",
  // the key layout presets
  "settings.row.layout": "layout",
  "settings.layout.standard": "standard",
  "settings.layout.alternating": "alternating",
  "settings.layout.standard.say": "D and F turn left, J and K turn right.",
  "settings.layout.alternating.say": "D and J turn left, F and K turn right.",
  "settings.layout.pick.hint": "Enter replaces your turn, brake, glance and view keys with these.",
  "settings.layout.own.hint": "Your own keys. Choosing a layout replaces the turn, brake, glance and view keys.",
  // applies the layout picked
  "settings.layout.use": "use it",
  // a notice: the layout's name, then its sentence
  "settings.notice.keys": "Keys: {layout}. {say}",
  // key actions (the game's CYCLE_TURN_LEFT etc.)
  "settings.row.k-left": "turn left",
  "settings.row.k-right": "turn right",
  "settings.row.k-brake": "brake",
  "settings.row.k-brake-toggle": "toggle brake",
  "settings.row.k-view": "switch view",
  "settings.row.k-glance-left": "glance left",
  "settings.row.k-glance-right": "glance right",
  "settings.row.k-glance-back": "glance back",
  "settings.row.k-zoom-in": "zoom in",
  "settings.row.k-zoom-out": "zoom out",
  "settings.row.k-look-up": "look up",
  "settings.row.k-look-down": "look down",
  "settings.row.k-look-left": "look left",
  "settings.row.k-look-right": "look right",
  "settings.row.k-move-back": "free camera back",
  "settings.row.k-move-forward": "free camera forward",
  "settings.row.k-move-up": "free camera up",
  "settings.row.k-move-down": "free camera down",
  "settings.row.k-move-left": "free camera left",
  "settings.row.k-move-right": "free camera right",
  "settings.row.k-chat": "chat",
  "settings.row.k-console": "console",
  "settings.row.k-instant": "instant chats 1–25",
  // the scoreboard key
  "settings.row.k-score": "score",
  "settings.row.k-menu": "menu",
  "settings.row.k-spectate": "toggle spectator",
  "settings.row.k-fullscreen": "toggle fullscreen",
  "settings.row.k-pause": "pause (training)",
  "settings.row.k-mess-up": "scroll messages up",
  "settings.row.k-mess-down": "scroll messages down",
  "settings.row.k-mess-end": "scroll messages to the end",
  // an action's words for INSTANT_CHAT_n
  "settings.action.instant": "instant chat {n}",
  // instant chats: the row's toggle, the text, its explanation
  "settings.instant.hide": "hide",
  "settings.instant.show": "show {n}",
  "settings.instant.empty": "empty",
  "settings.instant.say": "A text ending in \\ opens the chat line with it instead of sending it: /team \\ starts a team message.",
  // key cells (components/settings/keys.tsx)
  "settings.keys.reserved": "the browser keeps this key",
  "settings.keys.noKey": "the game has no key for this one",
  "settings.keys.cantBind": "the game can't bind this key",
  // a key moved here from another action: what it was (with UNDO)
  "settings.keys.was": "was {action}",
  "settings.keys.undo": "undo",
  "settings.keys.remove.title": "Remove {key}",
  "settings.keys.remove.aria": "{key}, remove",
  "settings.keys.capture.aria": "press a key, Esc cancels",
  "settings.keys.capture": "press a key",
  "settings.keys.escCancels": "esc cancels",
  "settings.keys.add.title": "Add a key for {action}",
  "settings.keys.add.aria": "add a key for {action}",
  "settings.keys.add": "add",
  "settings.keys.addFirst": "add a key",

  // ---- CAMERA
  "settings.row.camera-preset": "camera preset",
  // the stock camera preset (the others are players' names)
  "settings.camera.browser": "browser",
  "settings.row.start-cam": "starting camera",
  // the game's camera modes
  "settings.cam.custom": "custom",
  "settings.cam.server": "server custom",
  "settings.cam.internal": "internal",
  "settings.cam.smart": "smart",
  "settings.cam.external": "external",
  "settings.cam.free": "free",
  "settings.row.fov": "field of view",
  // the camera drawing (rig)
  "settings.row.rig-custom": "position",
  "settings.row.back": "distance",
  "settings.row.rise": "height",
  "settings.row.back-speed": "pulls back at speed",
  "settings.row.rise-speed": "rises at speed",
  "settings.row.pitch": "tilt",
  "settings.row.turn": "follows turns",
  // how fast the camera swings round after a 180° turn
  "settings.row.turn-180": "recovers from a reverse",
  "settings.row.rig-glance": "position",
  "settings.row.rig-glance.hint": "Hold a glance key and the camera swings round your cycle to look left, right or back, from this distance, height and tilt.",
  "settings.row.g-back": "distance",
  "settings.row.g-rise": "height",
  "settings.row.g-pitch": "tilt",
  "settings.row.glance-copy": "same as the custom camera",
  "settings.row.smart-glance": "smart camera glances like this",
  "settings.row.g-back-speed": "glance pulls back at speed",
  "settings.row.g-rise-speed": "glance rises at speed",
  "settings.row.zoom": "round-start zoom",
  "settings.row.incam-centre": "internal camera centred",
  "settings.row.incam-wobble": "internal camera movement",
  "settings.row.incam-turn": "internal camera turn speed",
  "settings.row.auto-incam": "switch to internal automatically",
  // "switch view cycles through": each camera mode on or off
  "settings.row.allow-custom": "custom",
  "settings.row.allow-server": "server custom",
  "settings.row.allow-in": "internal",
  "settings.row.allow-smart": "smart",
  "settings.row.allow-follow": "external",
  "settings.row.allow-free": "free",
  // copies the custom camera into the glance
  "settings.glance.copy": "copy",
  "settings.notice.glanceCopied": "The glance has the custom camera's distance, height and tilt.",
  // value words: the tilt
  "settings.tilt.level": "level",
  "settings.tilt.down": "{v}° down",
  "settings.tilt.up": "{v}° up",
  // value words: how quickly the camera follows a turn
  "settings.follow.fixed": "{v} · fixed",
  "settings.follow.smooth": "{v} · smooth",
  "settings.follow.steady": "{v} · steady",
  "settings.follow.quick": "{v} · quick",
  "settings.follow.snap": "{v} · snap",
  // value words: how far the camera moves at the stock speed
  "settings.speed.at": "+{v} m at speed",
  "settings.speed.none": "none",
  // the rig drawing
  "settings.rig.fromBehind": "from behind",
  "settings.rig.fromSide": "from the side",
  "settings.rig.fromAbove": "from above",
  "settings.rig.title.custom": "custom camera, seen from the side: {back} m back, {rise} m up, tilted {tilt} degrees down",
  "settings.rig.title.glance": "glance, seen from behind: {back} m back, {rise} m up, tilted {tilt} degrees down",
  // appended to the title above
  "settings.rig.title.fast": "; at speed {back} m back, {rise} m up",
  "settings.rig.drag.fast": "drag where the camera goes at speed",
  "settings.rig.drag.tilt": "drag to tilt",
  "settings.rig.drag.cam": "drag the camera",
  "settings.rig.atSpeed": "at speed",
  // the legend: the camera standing still, at speed, the tilt knob, the stock camera
  "settings.rig.standing": "standing",
  "settings.rig.tilt": "tilt",
  "settings.rig.stock": "stock",
  "settings.rig.drag": "drag any of them",
  // the glance keys' directions
  "settings.rig.left": "left",
  "settings.rig.right": "right",
  "settings.rig.back": "back",

  // ---- GRAPHICS
  "settings.row.fps-now": "frame rate now",
  "settings.row.fps-now.hint": "Measured on the arena behind the sheet; a game with more cycles can run slower.",
  // classic or modern look
  "settings.row.look": "look",
  "settings.row.look.hint": "Classic is the original game's look: no bloom or glow, the stock trails and cycle shading, the high rim.",
  "settings.row.look.hintArena": "Classic is the original game's look: no bloom or glow, the stock trails and cycle shading, the high rim, the classic arena.",
  "settings.row.quality": "quality",
  // the Auto quality's note: what tier it draws now; a CUSTOM quality's: the tier it came from
  "settings.quality.now": "now {tier}",
  "settings.quality.measuring": "measuring",
  "settings.quality.from": "from {tier}",
  "settings.row.arena": "arena",
  "settings.row.arena.hint": "Stadium plays in the home screen's arena; classic is the stock rim and sky.",
  "settings.row.arena-lights": "arena lights",
  "settings.row.arena-lights.hint": "The stadium's floodlights, light cones and crowd lights, here and in games.",
  "settings.row.render-scale": "resolution",
  "settings.row.render-scale.hint": "Of your screen's own. Lower draws fewer pixels: faster, but text and cycles get softer. Quality never changes it.",
  "settings.row.frame-rate": "frame rate",
  "settings.row.max-fps": "frame cap",
  "settings.row.show-fps": "frame rate counter",
  "settings.row.show-fps.hint": "The game's frames per second, in a corner of the screen.",
  "settings.row.msaa": "antialiasing",
  "settings.row.bloom": "bloom",
  "settings.row.bloom-intensity": "bloom intensity",
  "settings.row.glass": "trails",
  "settings.row.floor": "floor",
  "settings.row.mirror": "floor mirror",
  "settings.row.sparks": "sparks",
  "settings.row.explosions": "explosions",
  "settings.row.lower-sky": "lower sky",
  "settings.row.upper-sky": "upper sky",
  "settings.row.sky-wobble": "sky movement",
  "settings.row.high-rim": "high rim",
  "settings.row.alpha": "alpha blending",
  "settings.row.tex-floor": "floor filtering",
  "settings.row.tex-walls": "wall filtering",
  "settings.row.tex-models": "model filtering",
  "settings.row.tex-font": "font filtering",
  "settings.row.tex-font.hint": "The game's own text: names over the cycles, the HUD, the console.",
  "settings.row.fullscreen": "fullscreen",
  "settings.notice.fullscreen": "The browser kept the window as it is.",

  // ---- HUD & CHAT
  "settings.row.hud-style": "hud style",
  "settings.row.hud-style.hint": "Modern: the page's rubber, brake and speed meters at the foot. Classic: the game's own gauges and readouts, set below.",
  "settings.row.show-hud": "show hud",
  // the HUD's readouts (toggles)
  "settings.row.show-speed": "speed",
  "settings.row.show-rubber": "rubber",
  "settings.row.show-brake": "brake",
  "settings.row.show-ping": "ping",
  "settings.row.show-score": "score",
  "settings.row.show-alive": "players alive",
  "settings.row.show-fastest": "fastest",
  "settings.row.show-time": "time",
  "settings.row.show-time-24": "24-hour clock",
  "settings.row.lag-o-meter": "lag-o-meter",
  "settings.row.axes": "axes indicator",
  "settings.row.instant": "instant chats",

  // ---- SOUND
  // the row shown while the browser holds the sound back
  "settings.row.audio": "sound",
  "settings.audio.waiting": "sound is waiting",
  "settings.audio.none.hint": "This browser gave the game no sound device.",
  "settings.audio.suspended.hint": "Browsers start sound at your first key or click in the page.",
  "settings.audio.noDevice": "no device",
  "settings.audio.on": "turn sound on",
  "settings.row.sound-quality": "sound",
  "settings.row.sources": "sound sources",
  "settings.row.channels": "output",

  // ---- ADVANCED
  "settings.row.predict": "prediction",
  "settings.row.predict.hint": "Draws other cycles where they most likely are now, not where the last packet said.",
  "settings.row.ping-charity": "ping charity",
  "settings.row.ping-charity.hint": "How much of a slower player's lag you take on, so the round is fair to both.",
  "settings.row.in-rate": "input rate",
  "settings.row.out-rate": "output rate",
  "settings.row.every": "every setting",
  "settings.row.file": "config file",
  "settings.file.say": "The whole config as the desktop game's user.cfg: to keep, or to bring to another browser. An imported file is reviewed before anything changes.",
  "settings.file.export": "export",
  "settings.file.import": "import",
  "settings.file.resetAll": "reset every tab",
  "settings.file.resetAllSure": "sure? reset every tab",
  // every setting: the find's key, the count, a raw setting's note
  "settings.every.name": "name",
  "settings.every.count": "{shown} of {all}",
  "settings.every.byRoom": "set by the room",
  "settings.every.untilReload": "until reload",

  // ---- option words (segments and choices)
  "settings.opt.off": "off",
  "settings.opt.auto": "auto",
  "settings.opt.low": "low",
  "settings.opt.medium": "medium",
  "settings.opt.high": "high",
  "settings.opt.ultra": "ultra",
  "settings.opt.custom": "custom",
  "settings.opt.classic": "classic",
  "settings.opt.modern": "modern",
  "settings.opt.stadium": "stadium",
  // frame rate: synced to the display, or not
  "settings.opt.matchDisplay": "match display",
  "settings.opt.uncapped": "uncapped",
  "settings.opt.unlimited": "unlimited",
  // the floor's detail
  "settings.opt.none": "none",
  "settings.opt.grid": "grid",
  "settings.opt.textured": "textured",
  "settings.opt.dual": "dual",
  // what the floor mirrors
  "settings.opt.objects": "objects",
  "settings.opt.walls": "walls",
  "settings.opt.everything": "everything",
  // texture filtering (GL filter names)
  "settings.opt.nearest": "nearest",
  "settings.opt.bilinear": "bilinear",
  "settings.opt.mipmapNearest": "mipmap nearest",
  "settings.opt.mipmapBilinear": "mipmap bilinear",
  "settings.opt.trilinear": "trilinear",
  // the font: pixels sharp or smoothed
  "settings.opt.sharp": "sharp",
  "settings.opt.smooth": "smooth",
  "settings.opt.mono": "mono",
  "settings.opt.stereo": "stereo",

  // ---- a row's state (the quiet caps note at its right end)
  "settings.note.notInBuild": "not in this build",
  "settings.note.serverSets": "this server sets it",
  "settings.note.hudOff": "hud is off",
  "settings.note.bloomOff": "bloom is off",
  // CHANGED (off its stock value), and RESET on hover
  "settings.changed": "changed",
  "settings.reset": "reset",
  "settings.resetStock": "reset to stock",
  // the slider's mark of the stock value; its typed value's aria-label
  "settings.stock": "stock",
  "settings.value": "value",

  // ---- the foot
  "settings.foot.reset": "reset {tab}",
  "settings.foot.resetSure": "sure? reset {tab}",
  "settings.foot.resetAll": "yes, reset every tab",
  "settings.foot.copy": "copy",
  "settings.foot.copied": "copied",
  "settings.foot.undoImport": "undo import",
  "settings.notice.backToStock": "{tab} is back to stock.",
  "settings.notice.allStock": "Every tab is back to stock (your name and colour stay).",
  "settings.notice.applied": { one: "Applied {n} change. UNDO IMPORT puts them back.", other: "Applied {n} changes. UNDO IMPORT puts them back." },
  "settings.notice.undone": "The import is undone.",
  "settings.notice.tooBig": "That file is over 64 KB; a user.cfg is a few KB.",
  "settings.notice.clipboard": "The browser refused the clipboard. EXPORT, in ADVANCED, saves the whole config as a file.",

  // ---- the paste review
  "settings.paste.title": "import settings",
  "settings.paste.say": "Lines from a user.cfg: yours from another browser, or a friend's. Nothing changes until you apply it.",
  "settings.paste.aria": "settings to import",
  "settings.paste.none": "Nothing here would change a setting.",
  "settings.paste.noneSame": { one: "Nothing here would change a setting: {n} line is what you have already.", other: "Nothing here would change a setting: {n} lines are what you have already." },
  "settings.paste.changes": { one: "{n} change", other: "{n} changes" },
  // a group's switch: leave it out, or apply it
  "settings.paste.skip": "skip",
  "settings.paste.apply": "apply",
  // the arrow between the old and the new value (aria-label)
  "settings.paste.to": "to",
  "settings.paste.refused": { one: "{n} line not taken", other: "{n} lines not taken" },
  "settings.paste.nothing": "nothing to apply",
  "settings.paste.applyN": { one: "apply {n} change", other: "apply {n} changes" },
  // groups beside the tabs
  "settings.paste.identity": "name and colour",
  "settings.paste.keys": "keys",
  // a pasted colour line: which channel (r, g or b)
  "settings.paste.colourChannel": "cycle colour {c}",
  // a key bound to no action before
  "settings.paste.nothingBound": "nothing",
  // why a line is not taken
  "settings.paste.why.kb": "{n} KB of text",
  "settings.paste.why.tooLong": "too long: 64 KB at most",
  "settings.paste.why.notKeyLine": "not a key line the game reads",
  "settings.paste.why.unknownAction": "unknown action",
  "settings.paste.why.player": "player {n}: split screen is not in the browser yet",
  "settings.paste.why.password": "a password",
  "settings.paste.why.login": "a login",
  "settings.paste.why.splitScreen": "split screen is not in the browser yet",
  "settings.paste.why.notForPaste": "not for a paste",
  "settings.paste.why.unknown": "unknown here, or a command",
  "settings.paste.why.room": "set by the room you are in",
  "settings.paste.why.command": "a command",
  "settings.paste.why.server": "a server setting"
} as const;
