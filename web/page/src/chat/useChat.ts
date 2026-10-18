// The chat overlay's hook: while a game runs it listens to the API's `chat`
// and `console` events, sorts them (sort.ts) into the room's lines, and sends what the
// player types. Commands the page answers itself: /t (team), /w and /msg (a whisper in this
// room, the server's /msg), /r (reply to the last whisper), /mute and /unmute, and /f (a
// friend request). With the platform's mutes, /mute goes to the platform for a
// player the room verified (App's `social`: the player's account or guest, found by network
// id, muted in every room); anyone else is muted by name for this room's session, as before
// (the 0.2.9 protocol carries no global ids to clients). Every other line, commands
// included, goes to the room as typed.
import { useCallback, useEffect, useRef, useState } from "react";
import type { Aa, AaConsoleEvent, AaPlayer } from "../aa/types";
import { commandName } from "../platform/reports";
import { t } from "../i18n";
import { decodeGameText, hasWide } from "./text";
import { appendEntry, isMe, note, sortChat, stripCodes, sortConsole, toRuns, type ChatEntry, type ChatEvent, type Run } from "./sort";
import { filterChat } from "./filter";

// how many lines the room keeps (the open overlay scrolls through them)
const KEEP = 200;

// what the platform does for the chat line (App.tsx, platform/friends.ts); absent without one
export interface ChatSocial {
  // mutes or unmutes a player of the room through the platform: the line to say, or null when
  // the platform cannot (mutes off, the player not verified): the page mutes by name then
  mute(player: AaPlayer, on: boolean): Promise<string | null>;
  // /f: asks a player of the room (when found) or a typed handle to be friends; the line to say
  friend(typed: string, player: AaPlayer | null): Promise<string>;
  // /w to someone not in this room: a direct message to the friend of that name or handle; the
  // line to say on failure, "" when sent (the stream brings it back as a line),
  // or null when no friend has that name (the room's own /msg is tried)
  message(typed: string, text: string): Promise<string | null>;
  // /r to the last direct message: by user id
  messageTo(userId: string, text: string): Promise<string>;
}

export interface ChatView {
  entries: ChatEntry[];
  // muted names, lower case (lines from them are left out, their events stay)
  mutes: ReadonlySet<string>;
  // sends a typed line; false when the client refused it (not in the game yet)
  send(text: string): boolean;
  // a direct message (sent or received) as a line of this room's chat
  addDirect(entry: ChatEntry): void;
  // mutes or unmutes one player of the room (the card's MUTE, /mute): through the platform
  // when it can, else by name for this room; the line it said
  mute(player: AaPlayer, on: boolean): Promise<string>;
}

function lower(s: string): string {
  return s.trim().toLowerCase();
}

// one player in the room by a typed name: exact (case aside), else the only one it begins
function findPlayer(players: AaPlayer[], typed: string): AaPlayer | null {
  const t = lower(typed).replace(/_/g, " ");
  if (!t) return null;
  const exact = players.find((p) => lower(p.name) === t);
  if (exact) return exact;
  const starts = players.filter((p) => lower(p.name).startsWith(t));
  return starts.length === 1 ? starts[0] : null;
}

// a line from a server that is not ours, through the page's own filter:
// the message and its colours read as the raw line they came from, masked where the filter
// says, and cut into runs again. Our rooms filter on the platform: their lines are left alone
function runsRaw(runs: Run[]): string {
  return runs.map((r, i) => (r.color ? "0x" + r.color.slice(1) : i > 0 ? "0xRESETT" : "") + r.text).join("");
}

export function filterEntry(e: ChatEntry): ChatEntry {
  if (e.kind !== "say" && e.kind !== "team" && e.kind !== "whisper" && e.kind !== "server") return e;
  const raw = e.runs ? runsRaw(e.runs) : e.text;
  const out = filterChat(raw);
  if (!out.masked) return e;
  return { ...e, text: stripCodes(out.text), runs: e.runs ? toRuns(out.text) : undefined, raw: e.text };
}

// utf8: the room carries UTF-8 chat (an official room: the client was told setChatUtf8); in
// any other room what Latin-1 cannot hold goes out as '?', which the page says once.
// filtered: a server that is not ours (community, Direct Connect): its lines go through the
// page's own chat filter
export function useChat(aa: Aa | null, active: boolean, players: AaPlayer[], utf8: boolean, social: ChatSocial | null = null, filtered = false): ChatView {
  const [entries, setEntries] = useState<ChatEntry[]>([]);
  const [mutes, setMutes] = useState<ReadonlySet<string>>(() => new Set());
  const playersRef = useRef(players);
  playersRef.current = players;
  const socialRef = useRef(social);
  socialRef.current = social;
  const filteredRef = useRef(filtered);
  filteredRef.current = filtered;
  // a chat line comes as `chat` and then as `console` with the same text: the copies to drop
  const pending = useRef<string[]>([]);
  // who whispered to the local player last (/r answers them): a name in this room, or a direct
  // message's sender (by user id)
  const lastWhisper = useRef<{ name: string; userId: string | null } | null>(null);
  // the room cannot carry some characters: said once per room
  const foldSaid = useRef(false);

  const add = useCallback((e: ChatEntry) => {
    setEntries((list) => appendEntry(list, e, KEEP));
  }, []);

  // a new game is a new room: its own lines and its own mutes
  useEffect(() => {
    if (!active) return;
    setEntries([]);
    setMutes(new Set());
    pending.current = [];
    lastWhisper.current = null;
    foldSaid.current = false;
  }, [active]);

  useEffect(() => {
    if (!aa || !active) return;
    const offChat = aa.on("chat", (raw: ChatEvent) => {
      if (!raw || typeof raw.text !== "string") return;
      // the bytes as the characters they spell (UTF-8 where it is, else Latin-1: text.ts)
      const e: ChatEvent = { ...raw, line: decodeGameText(raw.line ?? raw.text), text: decodeGameText(raw.text), name: raw.name === null ? null : decodeGameText(raw.name) };
      pending.current.push(e.text.trim());
      if (pending.current.length > 8) pending.current.shift();
      const sorted = sortChat(e, playersRef.current, performance.now());
      if (sorted.kind === "whisper" && sorted.toYou && sorted.from) lastWhisper.current = { name: sorted.from.name, userId: null };
      add(filteredRef.current ? filterEntry(sorted) : sorted);
    });
    const offConsole = aa.on("console", (e: AaConsoleEvent) => {
      if (!e || typeof e.text !== "string") return;
      // in a network game one event can carry several lines (the server sends its console
      // output in batches): each is sorted on its own
      const raw = decodeGameText(typeof e.line === "string" ? e.line : e.text);
      for (const line of raw.split(/\r?\n/)) {
        const text = stripCodes(line).trim();
        if (!text) continue;
        const i = pending.current.indexOf(text);
        if (i >= 0) {
          pending.current.splice(i, 1);
          continue;
        }
        const sorted = sortConsole(line, performance.now());
        if (sorted) add(filteredRef.current ? filterEntry(sorted) : sorted);
      }
    });
    return () => {
      offChat();
      offConsole();
    };
  }, [aa, active, add]);

  const say = useCallback((line: string) => add(note(line, performance.now())), [add]);

  const mute = useCallback(
    async (p: AaPlayer, on: boolean): Promise<string> => {
      const key = lower(p.name);
      const line = socialRef.current ? await socialRef.current.mute(p, on) : null;
      // unmuted everywhere, or muted by name: the room's own list follows
      if (line === null || !on)
        setMutes((m) => {
          const next = new Set(m);
          if (on) next.add(key);
          else next.delete(key);
          return next;
        });
      const said = line ?? (on ? t("social.chat.mutedHere", { name: p.name }) : t("social.chat.unmuted", { name: p.name }));
      say(said);
      return said;
    },
    [say]
  );

  const send = useCallback(
    (typed: string): boolean => {
      const text = typed.trim();
      if (!text || !aa) return !!text;
      // to the room; refused before the local player exists (API.md: from the first round
      // start after the join)
      const out = (line: string): boolean => {
        const ok = aa.chat(line);
        if (!ok) say(aa.getState().state === "local" ? t("social.chat.notSentTraining") : t("social.chat.notSentYet"));
        else if (!utf8 && hasWide(line) && !foldSaid.current) {
          foldSaid.current = true;
          say(t("social.chat.folded"));
        }
        return ok;
      };
      const [word, ...rest] = text.split(/\s+/);
      const cmd = word.toLowerCase();
      const arg = rest.join(" ");
      if (cmd === "/t") return arg ? out("/team " + arg) : true;
      if (cmd === "/w" || cmd === "/msg") {
        const [who, ...msg] = rest;
        if (!who || !msg.length) {
          say(t("social.chat.whisperHow"));
          return true;
        }
        // a player in this room: the room's whisper (desktop players get it); a friend elsewhere: a
        // direct message through the platform; anyone else: the room's /msg as typed
        const platform = socialRef.current;
        if (findPlayer(playersRef.current, who) || !platform) return out("/msg " + who + " " + msg.join(" "));
        void platform.message(who, msg.join(" ")).then((line) => {
          if (line === null) out("/msg " + who + " " + msg.join(" "));
          else if (line) say(line);
        });
        return true;
      }
      if (cmd === "/r") {
        const last = lastWhisper.current;
        if (!last) {
          say(t("social.chat.noWhisper"));
          return true;
        }
        if (!arg) return true;
        if (last.userId && socialRef.current) {
          void socialRef.current.messageTo(last.userId, arg).then((line) => line && say(line));
          return true;
        }
        return out("/msg " + commandName(last.name) + " " + arg);
      }
      if (cmd === "/mute" || cmd === "/unmute") {
        const p = findPlayer(playersRef.current, arg);
        if (!p) {
          say(arg ? t("social.chat.noOne", { name: arg }) : t("social.chat.needName", { cmd }));
          return true;
        }
        if (isMe(p)) {
          say(t("social.chat.muteSelf"));
          return true;
        }
        void mute(p, cmd === "/mute");
        return true;
      }
      if ((cmd === "/f" || cmd === "/friend") && socialRef.current) {
        if (!arg) {
          say(t("social.chat.friendHow"));
          return true;
        }
        const p = findPlayer(playersRef.current, arg);
        if (p && isMe(p)) {
          say(t("social.chat.thatIsYou"));
          return true;
        }
        void socialRef.current.friend(arg, p).then(say);
        return true;
      }
      return out(text);
    },
    [aa, mute, say, utf8]
  );

  const addDirect = useCallback(
    (e: ChatEntry) => {
      if (e.toYou && e.from && e.dm) lastWhisper.current = { name: e.from.name, userId: e.dm.userId };
      add(e);
    },
    [add]
  );

  return { entries, mutes, send, mute, addDirect };
}

// the lines the overlay shows: a muted player's chat is left out, their events stay. Muted by
// name for this room (`mutes`), or through the platform (`mutedIds`: the network ids of the
// room's players whose user this player muted)
export function audible(entries: ChatEntry[], mutes: ReadonlySet<string>, mutedIds: ReadonlySet<number> = new Set()): ChatEntry[] {
  if (!mutes.size && !mutedIds.size) return entries;
  return entries.filter((e) => !(e.from && !e.from.local && (mutes.has(lower(e.from.name)) || (e.from.id !== null && mutedIds.has(e.from.id)))));
}
