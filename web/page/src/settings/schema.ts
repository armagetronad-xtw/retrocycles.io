// What the Settings sheet shows: seven tabs by task, each a few groups of
// rows, every row one game setting (or a page one) with the control that suits its value
//. Pure data: the sheet renders it, FIND searches it, COPY and the paste
// review sort a user.cfg line into its tab with it, and a row whose value is not `def` says
// CHANGED. `def` is the stock value a fresh browser has (the game's default, or what the
// browser's first-run config sets; web/wasm/browser_user.cfg).
// Every word here is a key of the catalogue (i18n/en/settings.ts), rendered with t() by the sheet:
// a label is "settings.row.<id>", a hint "settings.row.<id>.hint".
import { ARENA_TEST } from "../arenaTest";
import { num, t, type PlainKey } from "../i18n";

export type TabId = "player" | "controls" | "camera" | "graphics" | "hud" | "sound" | "advanced";

export const TABS: Array<{ id: TabId; title: PlainKey }> = [
  { id: "player", title: "settings.tab.player" },
  { id: "controls", title: "settings.tab.controls" },
  { id: "camera", title: "settings.tab.camera" },
  { id: "graphics", title: "settings.tab.graphics" },
  { id: "hud", title: "settings.tab.hud" },
  { id: "sound", title: "settings.tab.sound" },
  { id: "advanced", title: "settings.tab.advanced" }
];

// an option's words: a key, or text no language changes ("2×", "60 fps")
export type Option = { value: string; label: PlainKey } | { value: string; raw: string };

export function optionText(o: Option): string {
  return "raw" in o ? o.raw : t(o.label);
}

// the row ids that have a label key (yes() makes its label from the id)
type RowKeyId<K = PlainKey> = K extends `settings.row.${infer R}` ? R : never;

interface RowBase {
  id: string;
  // a setting the graphics quality tiers write (gBrowserQuality.cpp): changing it here makes
  // the quality CUSTOM first, or the tier would write it back
  tier?: boolean;
  label: PlainKey;
  // a quiet sentence under the row (only where the words alone don't say it)
  hint?: PlainKey;
  // words FIND also matches (what a veteran might type: "fov", "vsync", "enemies")
  keywords?: string[];
  // greyed (with this note) while another row says so: SHOW_HUD off greys the meters
  needs?: { name: string; value: string; note: PlainKey };
  // shown at all only while another row says so (the frame cap, with the frame rate uncapped)
  showWhen?: { name: string; value: string };
  // the game's own HUD: shown only while the HUD style is CLASSIC (the page draws those; settings/hudStyle.ts)
  classicHud?: boolean;
}

export interface ToggleRow extends RowBase {
  kind: "toggle";
  name: string;
  def: string; // "1" / "0"
}

export interface SegmentRow extends RowBase {
  kind: "segment";
  name: string;
  def: string;
  options: Option[];
}

export interface ChoiceRow extends RowBase {
  kind: "choice";
  name: string;
  def: string;
  options: Option[];
}

export interface SliderRow extends RowBase {
  kind: "slider";
  name: string;
  def: number; // in the row's own unit (degrees for the tilt, not the game's slope)
  min: number;
  max: number;
  step: number;
  unit?: string; // "m", "°", "%", "ms"
  // the value as the row says it: "35° down", "40 · quick"
  words?(v: number): string;
  // the game's text <-> the row's number (the tilt is a slope in the game)
  fromGame?(text: string): number;
  toGame?(v: number): string;
}

export interface TextRow extends RowBase {
  kind: "text";
  name: string;
  def: string;
  max?: number;
}

// rows that are more than one setting, or none of the game's
export interface SpecialRow extends RowBase {
  kind: "fps-now" | "name" | "colour" | "rig-custom" | "rig-glance" | "glance-copy" | "layout" | "touch-layout" | "camera-preset" | "hud-style" | "graphics-preset" | "fullscreen" | "shared" | "link-account" | "link-privacy" | "duels" | "duel-blocks" | "audio" | "every-setting" | "config-file" | "instant-chats" | "language";
}

export interface KeyRow extends RowBase {
  kind: "keys";
  action: string;
  player: number; // 1 for the player's actions, 0 for global ones (API.md getBindings)
}

export type Row = ToggleRow | SegmentRow | ChoiceRow | SliderRow | TextRow | SpecialRow | KeyRow;

export interface Group {
  id: string;
  head?: PlainKey;
  // folded under a MORE lane until opened (the camera's lesser rows)
  more?: boolean;
  rows: Row[];
}

export interface Tab {
  id: TabId;
  groups: Group[];
}

// ---- value words ----------------------------------------------------------------------------

const RAD = Math.PI / 180;

// the custom camera's pitch is a slope (how far it looks down per unit forward); the row says
// degrees and writes the slope back
export function tiltFromPitch(text: string): number {
  const p = Number(text);
  if (!Number.isFinite(p)) return 35;
  return Math.round(Math.atan(-p) / RAD);
}

export function pitchFromTilt(deg: number): string {
  return String(Math.round(-Math.tan(deg * RAD) * 1000) / 1000);
}

function tiltWords(v: number): string {
  if (v === 0) return t("settings.tilt.level");
  return v > 0 ? t("settings.tilt.down", { v }) : t("settings.tilt.up", { v: -v });
}

// CAMERA_CUSTOM_TURN_SPEED: how fast the view swings round after a turn (0: never; the
// browser starts at 4, the desktop game at 40)
export function followWords(v: number): string {
  if (v <= 0) return t("settings.follow.fixed", { v: 0 });
  const n = trim(v);
  if (v <= 2) return t("settings.follow.smooth", { v: n });
  if (v <= 8) return t("settings.follow.steady", { v: n });
  if (v <= 30) return t("settings.follow.quick", { v: n });
  return t("settings.follow.snap", { v: n });
}

// a number as the language writes it (3,5 in German), to two places
function trim(n: number): string {
  return num(Math.round(n * 100) / 100 + 0); // + 0: never "-0"
}

const metres = (v: number) => trim(v) + " m";

// the stock cycle speed (config/settings.cfg CYCLE_SPEED 30, m/s; rooms set their own): the custom
// camera adds _FROMSPEED times the speed to its distance and height, so at speed the browser's
// 6 m / 4 m camera sits 21 m back and 16 m up. The rig and these rows say where it is at this speed
export const REFERENCE_SPEED = 30;
const atSpeed = (v: number) => (v > 0 ? t("settings.speed.at", { v: trim(v * REFERENCE_SPEED) }) : t("settings.speed.none"));

// ---- the tabs ------------------------------------------------------------------------------

// a texture group's filtering (TEXTURE_MODE_n: the GL filter enums; -1 draws the group untextured)
const TEXTURE_MODES: Option[] = [
  { value: "-1", label: "settings.opt.off" },
  { value: "9728", label: "settings.opt.nearest" },
  { value: "9729", label: "settings.opt.bilinear" },
  { value: "9984", label: "settings.opt.mipmapNearest" },
  { value: "9985", label: "settings.opt.mipmapBilinear" },
  { value: "9987", label: "settings.opt.trilinear" }
];

const yes = (id: RowKeyId, name: string, def = "1", extra: Partial<ToggleRow> = {}): ToggleRow => ({ kind: "toggle", id, label: `settings.row.${id}`, name, def, ...extra });

const hudNeeds = { name: "SHOW_HUD", value: "1", note: "settings.note.hudOff" } as const;

export const TAB_ROWS: Record<TabId, Tab> = {
  player: {
    id: "player",
    groups: [
      { id: "you", rows: [{ kind: "name", id: "name", label: "settings.row.name", keywords: ["player_1", "nick"] }] },
      { id: "colour", head: "settings.group.colour", rows: [{ kind: "colour", id: "colour", label: "settings.row.colour", keywords: ["color", "colour_r", "red", "green", "blue", "swatch"] }] },
      {
        id: "browser",
        head: "settings.group.browser",
        rows: [
          { kind: "language", id: "language", label: "settings.row.language", hint: "settings.row.language.hint", keywords: ["language", "lang", "sprache", "langue", "idioma", "język", "translation"] },
          { kind: "shared", id: "shared", label: "settings.row.shared", hint: "settings.row.shared.hint", keywords: ["public computer", "library"] },
          { kind: "link-account", id: "account", label: "settings.row.account", keywords: ["sign in", "password", "sessions", "linked ids"] },
          { kind: "link-privacy", id: "privacy", label: "settings.row.privacy", keywords: ["appear offline", "messages", "friends join"] }
        ]
      },
      {
        // 1v1 challenges: the platform keeps them; shown where it has duels
        id: "duels",
        head: "settings.group.duels",
        rows: [
          { kind: "duels", id: "challenges", label: "settings.row.challenges", hint: "settings.row.challenges.hint", keywords: ["1v1", "duel", "challenge", "invite"] },
          { kind: "duel-blocks", id: "duel-blocks", label: "settings.row.duelBlocks", keywords: ["1v1", "duel", "challenge", "block", "unblock"] }
        ]
      }
    ]
  },

  controls: {
    id: "controls",
    groups: [
      // a touch device's buttons (touchLayout.ts); shown there alone (TOUCH_ROWS), never on a desktop
      { id: "touch", rows: [{ kind: "touch-layout", id: "touch-layout", label: "settings.row.touch-layout", keywords: ["touch", "buttons", "mobile", "brake"] }] },
      { id: "layout", rows: [{ kind: "layout", id: "layout", label: "settings.row.layout", keywords: ["wasd", "arrows", "zqsd", "preset", "keys"] }] },
      {
        id: "drive",
        head: "settings.group.drive",
        rows: [
          { kind: "keys", id: "k-left", label: "settings.row.k-left", action: "CYCLE_TURN_LEFT", player: 1 },
          { kind: "keys", id: "k-right", label: "settings.row.k-right", action: "CYCLE_TURN_RIGHT", player: 1 },
          { kind: "keys", id: "k-brake", label: "settings.row.k-brake", action: "CYCLE_BRAKE", player: 1 },
          { kind: "keys", id: "k-brake-toggle", label: "settings.row.k-brake-toggle", action: "CYCLE_BRAKE_TOGGLE", player: 1 }
        ]
      },
      {
        id: "look",
        head: "settings.group.look",
        rows: [
          { kind: "keys", id: "k-view", label: "settings.row.k-view", action: "SWITCH_VIEW", player: 1, keywords: ["camera"] },
          { kind: "keys", id: "k-glance-left", label: "settings.row.k-glance-left", action: "GLANCE_LEFT", player: 1 },
          { kind: "keys", id: "k-glance-right", label: "settings.row.k-glance-right", action: "GLANCE_RIGHT", player: 1 },
          { kind: "keys", id: "k-glance-back", label: "settings.row.k-glance-back", action: "GLANCE_BACK", player: 1 },
          { kind: "keys", id: "k-zoom-in", label: "settings.row.k-zoom-in", action: "ZOOM_IN", player: 1 },
          { kind: "keys", id: "k-zoom-out", label: "settings.row.k-zoom-out", action: "ZOOM_OUT", player: 1 },
          { kind: "keys", id: "k-look-up", label: "settings.row.k-look-up", action: "BANK_UP", player: 1 },
          { kind: "keys", id: "k-look-down", label: "settings.row.k-look-down", action: "BANK_DOWN", player: 1 },
          { kind: "keys", id: "k-look-left", label: "settings.row.k-look-left", action: "LOOK_LEFT", player: 1 },
          { kind: "keys", id: "k-look-right", label: "settings.row.k-look-right", action: "LOOK_RIGHT", player: 1 },
          { kind: "keys", id: "k-move-back", label: "settings.row.k-move-back", action: "MOVE_BACK", player: 1 },
          { kind: "keys", id: "k-move-forward", label: "settings.row.k-move-forward", action: "MOVE_FORWARD", player: 1 },
          { kind: "keys", id: "k-move-up", label: "settings.row.k-move-up", action: "MOVE_UP", player: 1 },
          { kind: "keys", id: "k-move-down", label: "settings.row.k-move-down", action: "MOVE_DOWN", player: 1 },
          { kind: "keys", id: "k-move-left", label: "settings.row.k-move-left", action: "MOVE_LEFT", player: 1 },
          { kind: "keys", id: "k-move-right", label: "settings.row.k-move-right", action: "MOVE_RIGHT", player: 1 }
        ]
      },
      {
        id: "talk",
        head: "settings.group.talk",
        rows: [
          { kind: "keys", id: "k-chat", label: "settings.row.k-chat", action: "CHAT", player: 1 },
          { kind: "keys", id: "k-console", label: "settings.row.k-console", action: "CONSOLE_INPUT", player: 0 },
          { kind: "instant-chats", id: "k-instant", label: "settings.row.k-instant", keywords: ["instant chat", "macro", "bind text"] }
        ]
      },
      {
        id: "game",
        head: "settings.group.game",
        rows: [
          { kind: "keys", id: "k-score", label: "settings.row.k-score", action: "SCORE", player: 0, keywords: ["scoreboard", "tab"] },
          { kind: "keys", id: "k-menu", label: "settings.row.k-menu", action: "INGAME_MENU", player: 0, keywords: ["escape"] },
          { kind: "keys", id: "k-spectate", label: "settings.row.k-spectate", action: "TOGGLE_SPECTATOR", player: 1 },
          { kind: "keys", id: "k-fullscreen", label: "settings.row.k-fullscreen", action: "TOGGLE_FULLSCREEN", player: 0 },
          { kind: "keys", id: "k-pause", label: "settings.row.k-pause", action: "PAUSE_GAME", player: 0 },
          { kind: "keys", id: "k-mess-up", label: "settings.row.k-mess-up", action: "MESS_UP", player: 0 },
          { kind: "keys", id: "k-mess-down", label: "settings.row.k-mess-down", action: "MESS_DOWN", player: 0 },
          { kind: "keys", id: "k-mess-end", label: "settings.row.k-mess-end", action: "MESS_END", player: 0 }
        ]
      }
    ]
  },

  camera: {
    id: "camera",
    groups: [
      {
        id: "start",
        rows: [
          { kind: "camera-preset", id: "camera-preset", label: "settings.row.camera-preset", keywords: ["preset"] },
          {
            kind: "choice",
            id: "start-cam",
            label: "settings.row.start-cam",
            name: "START_CAM_1",
            def: "1",
            options: [
              { value: "1", label: "settings.cam.custom" },
              { value: "5", label: "settings.cam.server" },
              { value: "0", label: "settings.cam.internal" },
              { value: "4", label: "settings.cam.smart" },
              { value: "3", label: "settings.cam.external" },
              { value: "2", label: "settings.cam.free" }
            ],
            keywords: ["incam", "smartcam", "start_cam"]
          },
          { kind: "slider", id: "fov", label: "settings.row.fov", name: "START_FOV_1", def: 90, min: 30, max: 160, step: 5, unit: "°", keywords: ["fov", "zoom"] }
        ]
      },
      {
        id: "custom",
        head: "settings.group.custom",
        rows: [
          { kind: "rig-custom", id: "rig-custom", label: "settings.row.rig-custom", keywords: ["drag", "rig", "camera position"] },
          { kind: "slider", id: "back", label: "settings.row.back", name: "CAMERA_CUSTOM_BACK", def: 6, min: 0, max: 40, step: 0.25, unit: "m", words: metres, keywords: ["back", "camera_custom_back"] },
          { kind: "slider", id: "rise", label: "settings.row.rise", name: "CAMERA_CUSTOM_RISE", def: 4, min: 0, max: 30, step: 0.25, unit: "m", words: metres, keywords: ["rise", "camera_custom_rise"] },
          { kind: "slider", id: "back-speed", label: "settings.row.back-speed", name: "CAMERA_CUSTOM_BACK_FROMSPEED", def: 0.5, min: 0, max: 1.5, step: 0.05, words: atSpeed, keywords: ["fromspeed", "back_fromspeed", "speed"] },
          { kind: "slider", id: "rise-speed", label: "settings.row.rise-speed", name: "CAMERA_CUSTOM_RISE_FROMSPEED", def: 0.4, min: 0, max: 1.5, step: 0.05, words: atSpeed, keywords: ["fromspeed", "rise_fromspeed", "speed"] },
          {
            kind: "slider",
            id: "pitch",
            label: "settings.row.pitch",
            name: "CAMERA_CUSTOM_PITCH",
            def: 30,
            min: -10,
            max: 80,
            step: 1,
            words: tiltWords,
            fromGame: tiltFromPitch,
            toGame: pitchFromTilt,
            keywords: ["pitch", "angle", "camera_custom_pitch"]
          },
          { kind: "slider", id: "turn", label: "settings.row.turn", name: "CAMERA_CUSTOM_TURN_SPEED", def: 4, min: 0, max: 50, step: 0.5, words: followWords, keywords: ["turn speed", "turn_speed", "rotation"] },
          { kind: "slider", id: "turn-180", label: "settings.row.turn-180", name: "CAMERA_CUSTOM_TURN_SPEED_180", def: 4, min: 0, max: 10, step: 0.5, words: (v) => "× " + trim(v), keywords: ["turn_speed_180", "180"] }
        ]
      },
      {
        id: "glance",
        head: "settings.group.glance",
        rows: [
          { kind: "rig-glance", id: "rig-glance", label: "settings.row.rig-glance", keywords: ["drag", "rig", "glance position", "look left", "look back"] },
          { kind: "slider", id: "g-back", label: "settings.row.g-back", name: "CAMERA_GLANCE_BACK", def: 6, min: 0, max: 40, step: 0.25, unit: "m", words: metres, keywords: ["glance back", "camera_glance_back"] },
          { kind: "slider", id: "g-rise", label: "settings.row.g-rise", name: "CAMERA_GLANCE_RISE", def: 4, min: 0, max: 30, step: 0.25, unit: "m", words: metres, keywords: ["glance rise", "camera_glance_rise"] },
          { kind: "slider", id: "g-pitch", label: "settings.row.g-pitch", name: "CAMERA_GLANCE_PITCH", def: 30, min: -10, max: 80, step: 1, words: tiltWords, fromGame: tiltFromPitch, toGame: pitchFromTilt, keywords: ["glance pitch", "camera_glance_pitch"] },
          { kind: "glance-copy", id: "glance-copy", label: "settings.row.glance-copy" },
          yes("smart-glance", "SMART_GLANCE_CUSTOM_1", "1")
        ]
      },
      {
        id: "more",
        head: "settings.group.more",
        more: true,
        rows: [
          { kind: "slider", id: "g-back-speed", label: "settings.row.g-back-speed", name: "CAMERA_GLANCE_BACK_FROMSPEED", def: 0.5, min: 0, max: 1.5, step: 0.05, words: atSpeed, keywords: ["fromspeed", "glance"] },
          { kind: "slider", id: "g-rise-speed", label: "settings.row.g-rise-speed", name: "CAMERA_GLANCE_RISE_FROMSPEED", def: 0.4, min: 0, max: 1.5, step: 0.05, words: atSpeed, keywords: ["fromspeed", "glance"] },
          { kind: "slider", id: "zoom", label: "settings.row.zoom", name: "CAMERA_CUSTOM_ZOOM", def: 0, min: 0, max: 2, step: 0.05, words: trim, keywords: ["camera_custom_zoom"] },
          yes("incam-centre", "CAMCENTER_1", "1", { keywords: ["camcenter"] }),
          yes("incam-wobble", "CAMWOBBLE_1", "0", { keywords: ["wobble", "camwobble"] }),
          { kind: "slider", id: "incam-turn", label: "settings.row.incam-turn", name: "CAMERA_IN_TURN_SPEED", def: 40, min: 0, max: 100, step: 1, words: followWords },
          yes("auto-incam", "AUTO_INCAM_1", "0", { keywords: ["auto_incam"] })
        ]
      },
      {
        id: "cycle",
        head: "settings.group.cycle",
        rows: [
          yes("allow-custom", "ALLOW_CAM_1_1"),
          yes("allow-server", "ALLOW_CAM_1_5"),
          yes("allow-in", "ALLOW_CAM_1_0"),
          yes("allow-smart", "ALLOW_CAM_1_4"),
          yes("allow-follow", "ALLOW_CAM_1_3"),
          yes("allow-free", "ALLOW_CAM_1_2")
        ]
      }
    ]
  },

  graphics: {
    id: "graphics",
    groups: [
      {
        id: "quality",
        rows: [
          { kind: "fps-now", id: "fps-now", label: "settings.row.fps-now", keywords: ["fps", "framerate", "performance"] },
          {
            kind: "graphics-preset",
            id: "look",
            label: "settings.row.look",
            hint: ARENA_TEST ? "settings.row.look.hintArena" : "settings.row.look.hint",
            keywords: ["classic", "old", "original", "stock", "preset", "modern", "retro"]
          },
          {
            kind: "segment",
            id: "quality",
            label: "settings.row.quality",
            name: "BROWSER_QUALITY",
            def: "0",
            options: [
              { value: "0", label: "settings.opt.auto" },
              { value: "1", label: "settings.opt.low" },
              { value: "2", label: "settings.opt.medium" },
              { value: "3", label: "settings.opt.high" },
              { value: "4", label: "settings.opt.ultra" },
              { value: "-1", label: "settings.opt.custom" }
            ],
            keywords: ["tier", "preset", "detail"]
          }
        ]
      },
      // the arena (arenaTest.ts): which arena games are played in, and the
      // stadium's lights; the page keeps both (the game saves neither)
      ...(ARENA_TEST
        ? [
            {
              id: "arena",
              head: "settings.group.arena",
              rows: [
                {
                  kind: "segment",
                  id: "arena",
                  label: "settings.row.arena",
                  name: "STADIUM",
                  def: "1",
                  options: [
                    { value: "0", label: "settings.opt.classic" },
                    { value: "1", label: "settings.opt.stadium" }
                  ],
                  hint: "settings.row.arena.hint",
                  keywords: ["stadium", "crowd", "stands", "rim", "map look"]
                },
                yes("arena-lights", "STADIUM_INTENSITY", "1", {
                  hint: "settings.row.arena-lights.hint",
                  keywords: ["stadium", "floodlights", "lights", "glare"]
                })
              ]
            } satisfies Group
          ]
        : []),
      {
        id: "performance",
        head: "settings.group.performance",
        rows: [
          { kind: "slider", id: "render-scale", label: "settings.row.render-scale", name: "BROWSER_RENDER_SCALE", def: 100, min: 25, max: 100, step: 5, unit: "%", fromGame: (t) => Math.round(Number(t) * 100), toGame: (v) => String(v / 100), hint: "settings.row.render-scale.hint", keywords: ["render scale", "render_scale", "density", "pixels", "sharp", "blurry"] },
          {
            kind: "segment",
            id: "frame-rate",
            label: "settings.row.frame-rate",
            name: "UNCAPPED_FPS",
            def: "0",
            options: [
              { value: "0", label: "settings.opt.matchDisplay" },
              { value: "1", label: "settings.opt.uncapped" }
            ],
            keywords: ["vsync", "fps", "uncapped_fps"]
          },
          {
            kind: "choice",
            id: "max-fps",
            label: "settings.row.max-fps",
            name: "MAX_FPS",
            def: "360",
            showWhen: { name: "UNCAPPED_FPS", value: "1" },
            options: ["60", "120", "144", "180", "240", "300", "360", "480", "600", "720", "900"].map((v): Option => ({ value: v, raw: v + " fps" })).concat([{ value: "0", label: "settings.opt.unlimited" }]),
            keywords: ["max_fps", "limit"]
          },
          // the page keeps it (settings/hudStyle.ts rc.showFps) and sets it on every load: off unless turned on here
          yes("show-fps", "SHOW_FPS", "0", { hint: "settings.row.show-fps.hint", keywords: ["show fps", "show_fps", "framerate", "counter"] })
        ]
      },
      {
        id: "effects",
        head: "settings.group.effects",
        rows: [
          {
            kind: "segment",
            id: "msaa",
            label: "settings.row.msaa",
            name: "BROWSER_MSAA",
            def: "0",
            options: [
              { value: "0", label: "settings.opt.off" },
              { value: "2", raw: "2×" },
              { value: "4", raw: "4×" },
              { value: "8", raw: "8×" }
            ],
            keywords: ["msaa", "aa", "jaggies"],
            tier: true
          },
          yes("bloom", "BROWSER_BLOOM", "0", { keywords: ["glow"], tier: true }),
          { kind: "slider", id: "bloom-intensity", label: "settings.row.bloom-intensity", name: "BROWSER_BLOOM_INTENSITY", def: 35, min: 0, max: 100, step: 5, unit: "%", fromGame: (t) => Math.round(Number(t) * 100), toGame: (v) => String(v / 100), needs: { name: "BROWSER_BLOOM", value: "1", note: "settings.note.bloomOff" }, tier: true },
          // TRAIL_GLASS: Classic lets the trail walls glow, Modern does not; a look, not a quality level
          {
            kind: "segment",
            id: "glass",
            label: "settings.row.glass",
            name: "TRAIL_GLASS",
            def: "0",
            options: [
              { value: "0", label: "settings.opt.classic" },
              { value: "1", label: "settings.opt.modern" }
            ],
            keywords: ["trail_glass", "glass", "walls", "trail style", "glow"],
            tier: true
          },
          {
            kind: "segment",
            id: "floor",
            label: "settings.row.floor",
            name: "FLOOR_DETAIL",
            def: "2",
            options: [
              { value: "0", label: "settings.opt.none" },
              { value: "1", label: "settings.opt.grid" },
              { value: "2", label: "settings.opt.textured" },
              { value: "3", label: "settings.opt.dual" }
            ],
            keywords: ["floor_detail", "grid"]
          },
          {
            kind: "segment",
            id: "mirror",
            label: "settings.row.mirror",
            name: "FLOOR_MIRROR",
            def: "0",
            options: [
              { value: "0", label: "settings.opt.off" },
              { value: "1", label: "settings.opt.objects" },
              { value: "2", label: "settings.opt.walls" },
              { value: "10", label: "settings.opt.everything" }
            ],
            keywords: ["reflection", "floor_mirror"]
          },
          yes("sparks", "SPARKS", "1"),
          yes("explosions", "EXPLOSION", "1", { keywords: ["explosion"] }),
          yes("lower-sky", "LOWER_SKY", "0"),
          yes("upper-sky", "UPPER_SKY", "0"),
          yes("sky-wobble", "SKY_WOBBLE", "1", { keywords: ["sky_wobble"] }),
          yes("high-rim", "HIGH_RIM", "1", { keywords: ["rim wall"] }),
          yes("alpha", "ALPHA_BLEND", "1", { keywords: ["alpha_blend", "transparency"] })
        ]
      },
      // the game's own texture filtering, one row per texture group (the desktop Detail Settings
      // menu, gMenus.cpp; rTexture.h's groups). Stock: trilinear, the font bilinear. The browser
      // always uploads full colour, so the desktop's texture quality (TEXTURES_HI) has no row
      {
        id: "textures",
        head: "settings.group.textures",
        rows: [
          { kind: "choice", id: "tex-floor", label: "settings.row.tex-floor", name: "TEXTURE_MODE_0", def: "9987", options: TEXTURE_MODES, keywords: ["texture_mode_0", "texture", "mipmap", "trilinear", "blurry"] },
          { kind: "choice", id: "tex-walls", label: "settings.row.tex-walls", name: "TEXTURE_MODE_1", def: "9987", options: TEXTURE_MODES, keywords: ["texture_mode_1", "texture", "mipmap", "trilinear", "rim"] },
          { kind: "choice", id: "tex-models", label: "settings.row.tex-models", name: "TEXTURE_MODE_2", def: "9987", options: TEXTURE_MODES, keywords: ["texture_mode_2", "texture", "mipmap", "trilinear", "cycle"] },
          {
            kind: "segment",
            id: "tex-font",
            label: "settings.row.tex-font",
            name: "TEXTURE_MODE_3",
            def: "9729",
            options: [
              { value: "9728", label: "settings.opt.sharp" },
              { value: "9729", label: "settings.opt.smooth" }
            ],
            hint: "settings.row.tex-font.hint",
            keywords: ["texture_mode_3", "font", "text", "blurry", "nearest", "bilinear"]
          }
        ]
      },
      { id: "display", head: "settings.group.display", rows: [{ kind: "fullscreen", id: "fullscreen", label: "settings.row.fullscreen", keywords: ["full screen", "f11"] }] }
    ]
  },

  hud: {
    id: "hud",
    groups: [
      // MODERN (the page's meters, the default) or CLASSIC (the game's own HUD, its switches below)
      { id: "style", rows: [{ kind: "hud-style", id: "hud-style", label: "settings.row.hud-style", hint: "settings.row.hud-style.hint", keywords: ["hud preset", "gauges", "meters", "modern", "classic", "rubber", "brake"] }] },
      {
        id: "hud",
        head: "settings.group.hud",
        rows: [
          yes("show-hud", "SHOW_HUD", "1", { keywords: ["show_hud", "heads up"] }),
          yes("show-speed", "SHOW_SPEED", "1", { needs: hudNeeds, classicHud: true, keywords: ["show_speed", "meter"] }),
          yes("show-rubber", "SHOW_RUBBER", "1", { needs: hudNeeds, classicHud: true, keywords: ["show_rubber", "meter"] }),
          yes("show-brake", "SHOW_BRAKE", "1", { needs: hudNeeds, classicHud: true, keywords: ["show_brake", "meter"] }),
          yes("show-ping", "SHOW_PING", "1", { needs: hudNeeds, classicHud: true, keywords: ["show_ping", "latency"] }),
          yes("show-score", "SHOW_SCORE", "1", { needs: hudNeeds, classicHud: true, keywords: ["show_score"] }),
          yes("show-alive", "SHOW_ALIVE", "1", { needs: hudNeeds, classicHud: true, keywords: ["enemies", "show_alive"] }),
          yes("show-fastest", "SHOW_FASTEST", "1", { needs: hudNeeds, classicHud: true, keywords: ["show_fastest"] }),
          yes("show-time", "SHOW_TIME", "0", { needs: hudNeeds, keywords: ["clock", "show_time"] }),
          yes("show-time-24", "SHOW_TIME_24", "0", { needs: hudNeeds, keywords: ["clock"] })
        ]
      },
      {
        id: "grid",
        head: "settings.group.grid",
        rows: [
          yes("lag-o-meter", "LAG_O_METER", "1", { keywords: ["lag", "lag_o_meter"] }),
          yes("axes", "AXES_INDICATOR", "0", { keywords: ["axes_indicator", "direction"] })
        ]
      },
      { id: "instant", head: "settings.group.instant", rows: [{ kind: "instant-chats", id: "instant", label: "settings.row.instant", keywords: ["instant_chat_string", "macro", "binds"] }] }
    ]
  },

  sound: {
    id: "sound",
    groups: [
      {
        id: "sound",
        rows: [
          { kind: "audio", id: "audio", label: "settings.row.audio" },
          {
            kind: "segment",
            id: "sound-quality",
            label: "settings.row.sound-quality",
            name: "SOUND_QUALITY",
            def: "0", // a new player's (browser_user.cfg); the game's own is 2
            options: [
              { value: "0", label: "settings.opt.off" },
              { value: "1", label: "settings.opt.low" },
              { value: "2", label: "settings.opt.medium" },
              { value: "3", label: "settings.opt.high" }
            ],
            keywords: ["sound_quality", "mute"]
          },
          { kind: "slider", id: "sources", label: "settings.row.sources", name: "SOUND_SOURCES", def: 10, min: 2, max: 20, step: 2, keywords: ["sound_sources", "channels"] },
          {
            kind: "segment",
            id: "channels",
            label: "settings.row.channels",
            name: "SOUND_CHANNELS",
            def: "2",
            options: [
              { value: "1", label: "settings.opt.mono" },
              { value: "2", label: "settings.opt.stereo" }
            ],
            keywords: ["sound_channels", "stereo"]
          }
        ]
      }
    ]
  },

  advanced: {
    id: "advanced",
    groups: [
      {
        id: "connection",
        head: "settings.group.connection",
        rows: [
          yes("predict", "PREDICT_OBJECTS", "0", { hint: "settings.row.predict.hint", keywords: ["predict_objects"] }),
          { kind: "slider", id: "ping-charity", label: "settings.row.ping-charity", name: "PING_CHARITY", def: 100, min: 0, max: 300, step: 10, unit: "ms", hint: "settings.row.ping-charity.hint", keywords: ["ping_charity", "lag"] },
          { kind: "slider", id: "in-rate", label: "settings.row.in-rate", name: "MAX_IN_RATE", def: 32, min: 3, max: 64, step: 1, words: (v) => num(v) + " kB/s", keywords: ["max_in_rate", "bandwidth"] },
          { kind: "slider", id: "out-rate", label: "settings.row.out-rate", name: "MAX_OUT_RATE", def: 16, min: 2, max: 32, step: 1, words: (v) => num(v) + " kB/s", keywords: ["max_out_rate", "bandwidth"] }
        ]
      },
      { id: "every", head: "settings.group.every", rows: [{ kind: "every-setting", id: "every", label: "settings.row.every", keywords: ["all", "raw", "cfg", "console"] }] },
      { id: "file", head: "settings.group.file", rows: [{ kind: "config-file", id: "file", label: "settings.row.file", keywords: ["export", "import", "user.cfg", "reset"] }] }
    ]
  }
};

// every row with its tab and group, in order (FIND, the paste review)
export interface Placed {
  tab: TabId;
  group: Group;
  row: Row;
}

export const ALL_ROWS: Placed[] = TABS.flatMap((t) => TAB_ROWS[t.id].groups.flatMap((g) => g.rows.map((row) => ({ tab: t.id, group: g, row }))));

// the config names a row writes (most rows one; the colour three; the name one)
export function rowNames(row: Row): string[] {
  if ("name" in row && typeof row.name === "string") return [row.name];
  if (row.kind === "name") return ["PLAYER_1"];
  if (row.kind === "colour") return ["COLOR_R_1", "COLOR_G_1", "COLOR_B_1"];
  if (row.kind === "instant-chats") return Array.from({ length: INSTANT_CHATS }, (_, i) => "INSTANT_CHAT_STRING_1_" + (i + 1));
  return [];
}

export const INSTANT_CHATS = 25;

// which tab a config name belongs to (anything not curated: advanced)
const NAME_TAB = new Map<string, TabId>();
for (const p of ALL_ROWS) for (const n of rowNames(p.row)) if (!NAME_TAB.has(n)) NAME_TAB.set(n, p.tab);
for (const n of ["CAMERA_CUSTOM_BACK", "CAMERA_CUSTOM_RISE", "CAMERA_CUSTOM_PITCH"]) NAME_TAB.set(n, "camera");

export function tabOf(name: string): TabId {
  return NAME_TAB.get(name.toUpperCase()) ?? "advanced";
}

// the row that edits a config name, for the paste review's words
const NAME_ROW = new Map<string, Placed>();
for (const p of ALL_ROWS) for (const n of rowNames(p.row)) if (!NAME_ROW.has(n)) NAME_ROW.set(n, p);

export function rowOf(name: string): Placed | null {
  return NAME_ROW.get(name.toUpperCase()) ?? null;
}

// the actions CONTROLS lists, with their words, and which group they are in
export const KEY_ROWS: Array<KeyRow & { group: string }> = TAB_ROWS.controls.groups.flatMap((g) => g.rows.filter((r): r is KeyRow => r.kind === "keys").map((r) => ({ ...r, group: g.id })));

export function actionWords(action: string): string {
  const m = /^INSTANT_CHAT_(\d+)$/.exec(action);
  if (m) return t("settings.action.instant", { n: m[1] });
  const row = KEY_ROWS.find((k) => k.action === action);
  return row ? t(row.label) : action.toLowerCase().replace(/_/g, " ");
}

// ---- on a touch device ------------------------------------------------------------------------
// A phone or tablet (compact.ts touchPrimary: "trim it down to things that would
// actually be used") has no keyboard and a small screen. The sheet there keeps six tabs and these
// rows: CONTROLS is the touch buttons' layout alone (no key bindings), nothing only a key drives (the glance, the switch-view cycle, the
// internal camera's keys, instant chats), of the HUD tab the HUD style alone, no
// finer camera tuning than distance, height, tilt and how it follows turns, graphics down to the
// quality tier, the render scale and the trails' look, sound down to on and its quality, and no
// connection tuning, raw settings or config files. What is left out keeps its value (and stays on a
// desktop); FIND, PASTE and COPY go too.
export const TOUCH_TABS: TabId[] = ["player", "controls", "camera", "graphics", "hud", "sound"];
export const TOUCH_ROWS: ReadonlySet<string> = new Set([
  "name", "colour", "language", "account", "privacy", "challenges", "duel-blocks",
  "touch-layout",
  "camera-preset", "start-cam", "fov", "rig-custom", "back", "rise", "pitch", "turn", "turn-180",
  "fps-now", "quality", "render-scale", "glass",
  "hud-style",
  "audio", "sound-quality"
]);
