// The chat overlay's sorter: what the game prints, split into what a player
// reads while playing and what they do not. Two sources, both the API's (web/wasm/API.md
// "Events"): the `chat` event (a chat line, tagged with its sender) and the `console` event
// (every console line: chat again, round results, deaths, joins, setting echoes, errors).
// A chat line arrives as `chat` first and then once more as `console` with the same text;
// the hook (useChat.ts) drops that copy. Everything here is pure: lines in, entries out.
//
// The console lines are the server's, in the server's words (0.2.9 sends finished strings,
// usually English: language/english_base.txt). Deaths, conquests and round results are
// recognised from those templates and said again tersely; joins and leaves become
// presence lines; setting echoes and the game's bookkeeping are dropped; anything else is
// kept as a server line, so nothing a room says on purpose is lost. A room in another
// language or with its own messages (sty+ct+ap's Fortress text) falls through to server
// lines:'s structured `gameEvent` from the wasm is the cure for that.
//
// Colours are the game's: names keep their 0xRRGGBB codes (cut out of the raw line as runs),
// and so does what a player wrote. A run without a code of its own takes the channel's
// colour when drawn (say yellow, team cyan, a whisper pink: ChatOverlay.tsx, chat.css).
import type { AaPlayer } from "../aa/types";
import { t, type MsgKey } from "../i18n";

// say: public chat; team: to the sender's team (or the spectators, or another team);
// whisper: /msg between two players in this room; event: deaths, bases, rounds, the match
// (the Events tab); presence: joins, leaves, renames; server: any other line the room
// printed; note: the page's own answer to a command typed in the chat line
export type ChatKind = "say" | "team" | "whisper" | "event" | "presence" | "server" | "note";

// a stretch of text in one colour: "#rrggbb" from a code, or null for the channel's colour
export interface Run {
  text: string;
  color: string | null;
}

// a piece of an event or presence line: a player's or team's name (with its colours), or
// the words between
export interface Part {
  text: string;
  name?: Run[];
}

export interface ChatEntry {
  id: number;
  at: number; // performance.now() when it arrived (the fade counts from here)
  kind: ChatKind;
  // the speaker of a say/team/whisper line
  from?: Speaker;
  // team: "team", "spectators" or another team's name; whisper: the other player's name
  to?: string;
  // a whisper to the local player (the speaker is someone else)
  toYou?: boolean;
  // say/team/whisper: the message; server/note: the line (plain)
  text: string;
  // say/team/whisper/server: the same with the colours it was written in
  runs?: Run[];
  // event/presence: the line as parts, names apart from the words
  parts?: Part[];
  // event: the points it granted (negative when lost), absent when it moved no score
  points?: number;
  // a kill: the player it core dumped (plain), and the points that player lost for it, folded in
  // from the game's next line (appendEntry): "Gdb +3 core dumped Nanu −2"
  victim?: string;
  victimPoints?: number;
  // the victim's side of a kill ("Nanu lost 2 points since it caused a general protection
  // fault"): who lost them (plain); appendEntry folds it into that kill
  fault?: string;
  // a direct message through the platform, drawn as a whisper: the other
  // player's user id, and the room they play in when it is not this one
  dm?: { userId: string; where: string | null };
  // a line the page's own filter masked (a server that is not ours): the message as
  // the server sent it, colour codes left out. Never drawn; a report carries it, so moderators
  // read what was said rather than the stars
  raw?: string;
}

export interface Speaker {
  name: string;
  runs: Run[]; // the name as the game colours it
  local: boolean;
  id: number | null; // the network id (the API's players[].id), null when the event had none
}

// the `chat` event (API.md): the full "name: text" line (`line` with colour codes) and its sender
export interface ChatEvent {
  line: string;
  text: string;
  playerId: number | null;
  name: string | null;
  local: boolean;
}

let nextId = 1;

function entry(kind: ChatKind, fields: Partial<ChatEntry>, at: number): ChatEntry {
  return { id: nextId++, at, kind, text: "", ...fields };
}

// the game's colour codes (aa_api.js colorSpans; 0xRESETT back to the default)
const CODE = /0x(?:[0-9a-fA-F]{6}|RESETT)/g;

export function stripCodes(raw: string): string {
  return raw.replace(CODE, "");
}

// a raw line as runs; text before any code has no colour (the channel's)
export function toRuns(raw: string): Run[] {
  const runs: Run[] = [];
  let color: string | null = null;
  let last = 0;
  CODE.lastIndex = 0;
  for (let m = CODE.exec(raw); m; m = CODE.exec(raw)) {
    if (m.index > last) runs.push({ text: raw.slice(last, m.index), color });
    color = m[0] === "0xRESETT" ? null : "#" + m[0].slice(2).toLowerCase();
    last = m.index + m[0].length;
  }
  if (last < raw.length) runs.push({ text: raw.slice(last), color });
  return runs;
}

// the raw stretch of a line that covers plain characters [start, end): the codes inside it
// and those right at its start come along, the ones before it (which coloured what came
// earlier, like the ": " after a name) do not, so its first run has the channel's colour
// unless the writer coloured it
export function rawRange(raw: string, start: number, end = Infinity): string {
  let plain = 0;
  let from = -1;
  let i = 0;
  while (i < raw.length) {
    CODE.lastIndex = i;
    const m = CODE.exec(raw);
    if (m && m.index === i) {
      if (from < 0 && plain === start) from = i;
      i += m[0].length;
      continue;
    }
    if (plain === start && from < 0) from = i;
    if (plain === end) return raw.slice(Math.max(0, from), i);
    plain++;
    i++;
  }
  return from < 0 ? "" : raw.slice(from);
}

// the player at this keyboard: local and not one of the AIs (a local game's AIs are local too)
export function isMe(p: Pick<AaPlayer, "local" | "bot">): boolean {
  return p.local && !p.bot;
}

// the server's own words for the team and the spectators (language "player_team_message",
// "player_spectator_message")
const TEAMMATES = "Teammates";
const SPECTATORS = "Spectators";

// a chat line, from the `chat` event: the text after the sender's name says what kind
// (ePlayer.cpp se_BuildChatString): "name: msg" public, "name --> Teammates: msg" team,
// "name --> Spectators: msg", "name (Team gold) --> Team blue: msg" another team, "name -->
// other: msg" a /msg whisper
export function sortChat(e: ChatEvent, players: AaPlayer[], at: number): ChatEntry {
  const raw = typeof e.line === "string" ? e.line : e.text;
  const text = stripCodes(raw);
  const sender = e.playerId !== null ? players.find((p) => p.id === e.playerId) : undefined;
  const name = e.name ?? sender?.name ?? "";
  const named = !!name && text.startsWith(name);
  const from: Speaker = { name, runs: named ? toRuns(rawRange(raw, 0, name.length)) : [{ text: name, color: null }], local: e.local, id: e.playerId };
  // the message from plain offset `at`: as written, colours included
  const msg = (start: number) => ({ text: text.slice(start), runs: toRuns(rawRange(raw, start)) });
  if (!named) return entry("say", { from, ...msg(0) }, at);
  const rest = text.slice(name.length);
  if (rest.startsWith(": ")) return entry("say", { from, ...msg(name.length + 2) }, at);
  const arrow = rest.indexOf(" --> ");
  if (arrow >= 0) {
    const after = rest.slice(arrow + 5);
    const colon = after.indexOf(": ");
    if (colon >= 0) {
      const target = after.slice(0, colon);
      const body = msg(name.length + arrow + 5 + colon + 2);
      if (target === TEAMMATES) return entry("team", { from, to: "team", ...body }, at);
      if (target === SPECTATORS) return entry("team", { from, to: "spectators", ...body }, at);
      if (arrow > 0) return entry("team", { from, to: target, ...body }, at); // "name (Team) --> Team"
      const me = players.find(isMe);
      return entry("whisper", { from, to: target, toYou: !e.local && !!me && me.name === target, ...body }, at);
    }
  }
  const skip = rest.match(/^:?\s*/)?.[0].length ?? 0;
  return entry("say", { from, ...msg(name.length + skip) }, at);
}

// an event's words, in the page's language when the line arrived: a text of the catalogue whose
// {a} {b} {c} are names cut from the line (regexp groups 1, 2, 3, in their colours) and whose
// other vars are plain words (the round's number, the game's own words for an unknown score)
interface Said {
  text: string;
}

// a catalogue text with its name slots left in place ({a} stays "{a}"), the plain vars filled
function say(key: MsgKey, vars: Record<string, string | number> = {}): Said {
  const fill = t as (k: MsgKey, v?: Record<string, string | number>) => string;
  return { text: fill(key, { a: "{a}", b: "{b}", c: "{c}", ...vars }) };
}

// the slots and the regexp group each names
const SLOT: Record<string, number> = { a: 1, b: 2, c: 3 };

const ZONES: Record<string, MsgKey> = {
  death: "social.event.zoneDeath",
  "team death": "social.event.zoneTeamDeath",
  rubber: "social.event.zoneRubber",
  blast: "social.event.zoneBlast"
};

const TIME_LEFT: Record<string, MsgKey> = {
  "Ten seconds": "social.event.left10s",
  "30 seconds": "social.event.left30s",
  "One minute": "social.event.left1m",
  "Two minutes": "social.event.left2m",
  "Five minutes": "social.event.left5m",
  "Ten minutes": "social.event.left10m"
};

// the points a scoring line granted: the regexp group that holds the number, and its sign
// ("lost 2 points" is -2)
type Points = { group: number; sign: 1 | -1 };

// a kill names its victim in group 2; a fault line names who lost points for being killed
type Role = "kill" | "fault";

type Rule = [RegExp, ((m: RegExpExecArray) => Said) | null, Points?, Role?];

const GAIN = (group: number): Points => ({ group, sign: 1 });
const LOSS = (group: number): Points => ({ group, sign: -1 });

// the important things that happen in a round, in the game's own words (it "core dumped" a
// player), shorter, and with the points each one granted: the Events tab, and
// the All tab with the chat. Scoring is the point of the room, so every line that moves a score
// is one (language/english_base.txt "player_win_*", "player_lose_*"). The patterns read the
// game's English (the client stays English); the words said are the page's (i18n "social.event.*")
const EVENTS: Rule[] = [
  [/^(.+) core dumped teammate (.+)! Boo!/, () => say("social.event.teamKill"), undefined, "kill"],
  [/^(.+) core dumped (.+) for (-?\d+) points?\.$/, () => say("social.event.kill"), GAIN(3), "kill"],
  [/^(.+) core dumped (.+)\.$/, () => say("social.event.kill"), undefined, "kill"],
  [/^(.+) got (-?\d+) points? for core dumping an AI player\.$/, () => say("social.event.killBot"), GAIN(2)],
  // the victim's side of a kill: folded into the kill (appendEntry), a line of its own only
  // when no kill of that player came just before
  [/^(.+) lost (-?\d+) points? since it caused a general protection fault\.$/, () => say("social.event.killed"), LOSS(2), "fault"],
  [/^(.+) shot (.+) for (-?\d+) points?\.$/, () => say("social.event.shot"), GAIN(3), "kill"],
  [/^(.+) got (.+) with a death shot for (-?\d+) points?\.$/, () => say("social.event.deathShot"), GAIN(3), "kill"],
  [/^(.+) self destructed and blasted (.+) for (-?\d+) points?\.$/, () => say("social.event.blasted"), GAIN(3), "kill"],
  [/^(.+) shot itself in the foot and lost (-?\d+) points?\.$/, () => say("social.event.shotSelf"), LOSS(2)],
  [/^(.+) committed suicide and lost (-?\d+) points?\.$/, () => say("social.event.crashed"), LOSS(2)],
  [/^(.+) committed suicide\.$/, () => say("social.event.crashed")],
  [/^(.+) lost (-?\d+) points? for trying to escape from the game grid\.$/, () => say("social.event.rim"), LOSS(2)],
  [/^(.+) exploded on an? (death|team death|rubber|blast)zone and lost (-?\d+) points?\.$/, (m) => say(ZONES[m[2]]), LOSS(3)],
  [/^(.+) was smitten by an administrator\.$/, () => say("social.event.smitten")],
  [/^(.+) has been hugged by an administrator and got (-?\d+) points?\.$/, () => say("social.event.hugged"), GAIN(2)],
  [/^(.+) has been slapped by an administrator and lost (-?\d+) points?\.$/, () => say("social.event.slapped"), LOSS(2)],
  [/^(.+) (?:was|were) awarded (-?\d+) points? for conquering (.+)'s base\.$/, () => say("social.event.conquered"), GAIN(2)],
  [/^(.+) was awarded (-?\d+) points? for conquering the enemy base\.$/, () => say("social.event.conqueredBase"), GAIN(2)],
  [/^(.+) was awarded (-?\d+) points? for holding the base\.$/, () => say("social.event.heldBase"), GAIN(2)],
  [/^(.+) lost (-?\d+) points? for being too defensive\.$/, () => say("social.event.tooDefensive"), LOSS(2)],
  [/^(.+) was awarded (-?\d+) points? for capturing the enemies flag\.$/, () => say("social.event.flag"), GAIN(2)],
  [/^(.+) was eradicated by its collapsing zone\.$/, () => say("social.event.zoneDown")],
  [/^(.+)'s zone collapses harmlessly/, () => say("social.event.zoneCollapsed")],
  [/^(.+) was awarded (-?\d+) points? for hitting the instant win zone\.$/, () => say("social.event.winZone"), GAIN(2)],
  [/^(.+) was awarded (-?\d+) points? for winning the race\.$/, () => say("social.event.race"), GAIN(2)],
  [/^(.+) was awarded (-?\d+) points? for being last active team\.$/, () => say("social.event.wonRound"), GAIN(2)],
  [/^(.+) was awarded (-?\d+) points? for winning the round\.$/, () => say("social.event.wonRound"), GAIN(2)],
  [/^(.+) got (-?\d+) points? for surviving\.$/, () => say("social.event.survived"), GAIN(2)],
  [/^(.+) Wins (-?\d+) points? for being The King of the Hill/, () => say("social.event.hill"), GAIN(2)],
  // any other score the room gives or takes, in its own words after the name
  [/^(.+?) (?:got|was awarded|were awarded) (-?\d+) points? for (.+?)\.?$/, (m) => say("social.event.other", { what: m[3] }), GAIN(2)],
  [/^(.+?) lost (-?\d+) points? for (.+?)\.?$/, (m) => say("social.event.other", { what: m[3] }), LOSS(2)],
  [/^Go \(round (\d+) of (\d+)\)!$/, (m) => say("social.event.round", { round: Number(m[1]), total: Number(m[2]) })],
  [/^Go \(extra round/, () => say("social.event.extraRound")],
  // a match total, not points granted: no points
  [/^Overall Winner: (.+?) (with -?\d+ points?|after .+)\.?$/, () => say("social.event.wonMatch")],
  [/^Resetting scores\.\.\.$/, () => say("social.event.newMatch")],
  [/^(Ten seconds|30 seconds|One minute|Two minutes|Five minutes|Ten minutes) left!$/, (m) => say(TIME_LEFT[m[1]])]
];

// who came and went
const PRESENCE: Rule[] = [
  [/^(.+) entered the game\.$/, () => say("social.presence.joined")],
  [/^(.+) entered as spectator\.$/, () => say("social.presence.joinedToWatch")],
  [/^Spectator (.+) left\.$/, () => say("social.presence.left")],
  [/^(.+) left the game\.$/, () => say("social.presence.left")],
  [/^(.+) will be renamed to (.+)\.$/, null],
  [/^(.+) (?:has been )?renamed to (.+)\.$/, () => say("social.presence.renamed")]
];

// the game's bookkeeping, dropped: setting echoes, the grid being
// made, team wishes, the chat wait, a leaver's score; the round's centre text is the game's
// HUD. Every line that grants points is an event (EVENTS)
const NOISE: RegExp[] = [
  / changed from .* to .*\.$/, // config_value_changed, nconfig_value_changed (+ " on server order"), not changed
  /^Unknown command/i,
  /^(Creating|Deleting) (grid|objects)\.\.\.$/,
  /^done!$/,
  /^Resetting scores and starting new match after this round\.$/,
  /^(.+) was discarded with -?\d+ points?\.$/,
  /^(.+) (still )?wants to (play for|leave) .+/,
  /^(.+) does not want to play next round\.$/,
  /^(.+) plays for .+\.$/,
  /^(.+) switches from .+ to .+\.$/,
  /^(.+) founded .+\.$/,
  /^(.+) left (Team .+|.+ team)\.$/, // "left Team blue.", "left AI team."
  /^Waiting up to \d+ seconds for .+ to finish chatting\.$/,
  /^Final Scores:$/,
  /^Timestamp: /, // a server's stamp at each round (sty+ct+ap)
  /^Clearing delayed commands/,
  /^Watching .+$/, // the spectator camera following a player (the HUD says so)
  // the client's network statistics at a new map, three lines ("network_statistics1-3",
  // padded with blanks)
  /^Time:\s+[0-9.]+ seconds/,
  /^(Sent|Received):\s+[0-9]+ bytes in [0-9]+ packets/,
  /^(Team|Player):\s/
];

// with group offsets (the `d` flag), to cut each name out of the raw line in its colours
const withIndices = (rules: Rule[]): Rule[] => rules.map(([re, make, points, role]) => [new RegExp(re.source, re.flags + "d"), make, points, role]);
const EVENTS_D = withIndices(EVENTS);
const PRESENCE_D = withIndices(PRESENCE);

// the said text as parts: each {a} {b} {c} the regexp group's name in its colours, the words between
function pieces(m: RegExpExecArray, said: Said, raw: string, lead: number): Part[] {
  const out: Part[] = [];
  let last = 0;
  for (const x of said.text.matchAll(/\{([abc])\}/g)) {
    if (x.index > last) out.push({ text: said.text.slice(last, x.index) });
    const g = SLOT[x[1]];
    const [start, end] = m.indices?.[g] ?? [0, 0];
    out.push({ text: m[g] ?? "", name: toRuns(rawRange(raw, lead + start, lead + end)) });
    last = x.index + x[0].length;
  }
  if (last < said.text.length) out.push({ text: said.text.slice(last) });
  return out;
}

// a console line (`line` with its colour codes; the event's `text` is the same without), or
// null for a line the overlay does not show
export function sortConsole(line: string, at: number): ChatEntry | null {
  const raw = line.replace(/\s+$/, "");
  const full = stripCodes(raw);
  const t = full.trimStart();
  const lead = full.length - t.length;
  if (!t) return null;
  for (const [kind, rules] of [["event", EVENTS_D], ["presence", PRESENCE_D]] as const) {
    for (const [re, make, points, role] of rules) {
      const m = re.exec(t);
      if (!m) continue;
      if (!make) return null;
      const pts = points ? Number(m[points.group]) * points.sign : NaN;
      return entry(
        kind,
        {
          text: t,
          parts: pieces(m, make(m), raw, lead),
          ...(Number.isFinite(pts) ? { points: pts } : {}),
          ...(role === "kill" ? { victim: m[2] } : role === "fault" ? { fault: m[1] } : {})
        },
        at
      );
    }
  }
  if (NOISE.some((re) => re.test(t))) return null;
  return entry("server", { text: t, runs: toRuns(rawRange(raw, lead)) }, at);
}

// how far back, and how soon after, a victim's line finds its kill (the game prints the two
// one after the other)
const FOLD_BACK = 4;
const FOLD_MS = 2000;

// the room's lines with one more, at most `keep` of them: the victim's side of a kill goes into
// that kill as its victimPoints instead of a line of its own
export function appendEntry(list: ChatEntry[], e: ChatEntry, keep: number): ChatEntry[] {
  if (e.fault !== undefined && e.points !== undefined) {
    for (let i = list.length - 1; i >= Math.max(0, list.length - FOLD_BACK); i--) {
      const k = list[i];
      if (k.victim === e.fault && k.victimPoints === undefined && e.at - k.at < FOLD_MS) {
        return [...list.slice(0, i), { ...k, victimPoints: e.points }, ...list.slice(i + 1)];
      }
    }
  }
  return list.length >= keep ? [...list.slice(list.length - keep + 1), e] : [...list, e];
}

export function note(text: string, at: number): ChatEntry {
  return entry("note", { text }, at);
}

// the Events tab: deaths, bases, rounds, the match
export function isEvent(e: ChatEntry): boolean {
  return e.kind === "event";
}

// a direct message as a whisper line: `mine` when this player sent it; `name` the other
// player's; `where` their room when it is not this one
export function directEntry(text: string, mine: boolean, myName: string, name: string, userId: string, where: string | null, at: number): ChatEntry {
  const other: Speaker = { name, runs: [{ text: name, color: null }], local: false, id: null };
  const me: Speaker = { name: myName, runs: [{ text: myName, color: null }], local: true, id: null };
  return entry(
    "whisper",
    { from: mine ? me : other, to: mine ? name : myName, toYou: !mine, text, runs: [{ text, color: null }], dm: { userId, where } },
    at
  );
}
