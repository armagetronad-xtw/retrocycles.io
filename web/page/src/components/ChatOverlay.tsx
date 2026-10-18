// The chat overlay: at the top left over the running game, where the
// game's own console stood, and in its place (TEXT_OUT is off while it is
// up); clear of the centred match summary and of the offer lower in the left column. At rest it is lines
// only, each on a near-black strip, the last few of the chosen chat tab, gone LINE_MS after
// they came; nothing takes the pointer or the focus, and nothing hides them. The game's CHAT key (the API's `chatOpen`) opens
// it: the lines become a panel (the cell line, a head with the three tabs, every line kept,
// scrolling) with the chat line as a well at its foot, which takes the focus, so the client
// leaves the keys alone (API.md "Setup"). The game's CONSOLE_INPUT key (` , the API's
// `consoleOpen`; the page's own key handler on the home screen) opens the same panel on its
// third view, CONSOLE: the raw log of this session and the one before (chat/log.ts) in the
// system monospace, colour codes kept, and the line in command mode: plain text is a setting
// run here (aa.console), /... goes to the room (aa.chat), never chat.
// In the line: Enter sends and closes, back on ALL so the line just sent shows (CONSOLE: runs
// and stays open), Esc closes (a filter first), Tab completes a name of this room (CONSOLE: a setting's name, its value shown as a
// ghost after it), Shift+Tab goes to the next view, Up/Down go through what you sent (chat
// and commands apart), PageUp/PageDown scroll, Ctrl+F filters the lines. Leaving the panel
// any other way (a click on the game) closes it too. Out of a game it is CONSOLE alone.
// On a touch device (the touch controls' chat cell opens it, the keyboard already
// up: softKeyboard.ts) the panel stands in what the phone's keyboard leaves of the screen
// (visualViewport), ALL and EVENTS only (no Console), a close cell in its head, and the keyboard's
// own key says send.
import { forwardRef, useEffect, useImperativeHandle, useLayoutEffect, useRef, useState, type FocusEvent, type KeyboardEvent } from "react";
import type { AaPlayer } from "../aa/types";
import { isEvent, isMe, stripCodes, toRuns, type ChatEntry, type Run } from "../chat/sort";
import { cjkLang, linkCjkFaces } from "../chat/text";
import { logText, type LogLine } from "../chat/log";
import { t, type PlainKey } from "../i18n";

export type ChatTab = "all" | "events";
// what the open panel shows: a chat tab, or the raw log
export type PanelView = ChatTab | "console";

const VIEWS: PanelView[] = ["all", "events", "console"];

const VIEW_WORDS: Record<PanelView, PlainKey> = { all: "social.chat.tab.all", events: "social.chat.tab.events", console: "social.chat.tab.console" };

// how long a line stays on screen while the overlay is closed, and how many show at once
export const LINE_MS = 12000;
const REST_LINES = 8;
// the compact view (compact.ts): fewer lines at rest, and none of the room's own long lines
// (its welcome, its ladder notes), which the open panel still has
const REST_LINES_COMPACT = 4;
// a command's answer: the lines that come within this long are read out
const ANSWER_MS = 2000;

export interface ChatOverlayHandle {
  // the chat line; a prefill (an instant chat) replaces what was typed, null keeps it
  open(prefill: string | null): void;
  openConsole(): void;
}

// the settings the Console line completes and shows (aa.listConfig / aa.getConfig)
export interface Settings {
  names(prefix: string): string[];
  value(name: string): string | null;
}

interface ChatOverlayProps {
  entries: ChatEntry[];
  log: LogLine[];
  players: AaPlayer[];
  tab: ChatTab;
  // a game runs: the chat tabs are there (out of a game the panel is the Console alone)
  inGame: boolean;
  settings: Settings | null;
  // a small frame or screen (compact.ts): the lines at rest are fewer and shorter
  compact?: boolean;
  // a touch device: the panel fits above the keyboard, no Console, a close cell
  touch?: boolean;
  onTab(tab: ChatTab): void;
  onSend(text: string): boolean;
  onRun(text: string): void;
  onOpenChange(open: boolean, view: PanelView): void;
}

// what the line will do, from what is typed so far: the well's key (to: a whisper's addressee)
type LineKey = { kind: "server" | "setting" | "say" | "team" | "to" | "whisper" | "reply" | "mute" | "unmute" | "command"; to?: string };

function lineKey(draft: string, view: PanelView): LineKey {
  if (view === "console") return { kind: draft.startsWith("/") ? "server" : "setting" };
  const m = /^\/(\S+)(?:\s+(\S+))?/.exec(draft);
  if (!m) return { kind: "say" };
  const cmd = m[1].toLowerCase();
  if (cmd === "t" || cmd === "team") return { kind: "team" };
  if ((cmd === "w" || cmd === "msg") && m[2]) return { kind: "to", to: m[2].replace(/_/g, " ") };
  if (cmd === "w" || cmd === "msg") return { kind: "whisper" };
  if (cmd === "r") return { kind: "reply" };
  if (cmd === "mute" || cmd === "unmute") return { kind: cmd };
  return { kind: "command" };
}

const KEY_WORDS: Record<Exclude<LineKey["kind"], "to">, PlainKey> = {
  server: "social.chat.key.server",
  setting: "social.chat.key.setting",
  say: "social.chat.key.say",
  team: "social.chat.key.team",
  whisper: "social.chat.key.whisper",
  reply: "social.chat.key.reply",
  mute: "social.chat.key.mute",
  unmute: "social.chat.key.unmute",
  command: "social.chat.key.command"
};

function keyText(k: LineKey): string {
  return k.kind === "to" ? t("social.chat.key.to", { name: k.to ?? "" }) : t(KEY_WORDS[k.kind]);
}

// a colour from the game as the overlay draws it: a near-black name or word (0x000000 and
// the like, common in the game) is lifted toward ink just enough to be read on the strip;
// everything else is the colour as written
const READ_LUMA = 96;
function readable(hex: string): string {
  const n = parseInt(hex.slice(1), 16);
  const c = [(n >> 16) & 255, (n >> 8) & 255, n & 255];
  const luma = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
  if (luma >= READ_LUMA) return hex;
  const ink = [242, 244, 247];
  const t = (READ_LUMA - luma) / (0.2126 * ink[0] + 0.7152 * ink[1] + 0.0722 * ink[2] - luma);
  const mixed = c.map((v, i) => Math.round(v + (ink[i] - v) * t));
  return `rgb(${mixed[0]}, ${mixed[1]}, ${mixed[2]})`;
}

// runs in their own colours; a run without a code takes the channel's (the line's colour)
function Runs({ runs }: { runs: Run[] }) {
  return (
    <>
      {runs.map((r, i) =>
        r.color ? (
          <span key={i} style={{ color: readable(r.color) }}>
            {r.text}
          </span>
        ) : (
          <span key={i}>{r.text}</span>
        )
      )}
    </>
  );
}

// the channel's label, the way MMO chat marks it: [team], [spectators], [to Team blue], and
// for a whisper [from name] / [to name] (the name plain: a whisper is one colour)
function label(e: ChatEntry): string | null {
  if (e.kind === "team") return e.to === "team" ? t("social.chat.tag.team") : e.to === "spectators" ? t("social.chat.tag.spectators") : t("social.chat.tag.to", { name: e.to ?? "" });
  if (e.kind === "whisper") return (e.from?.local ? t("social.chat.tag.to", { name: e.to ?? "" }) : t("social.chat.tag.from", { name: e.from?.name ?? "" })) + (e.dm?.where ? " @ " + e.dm.where : "");
  return null;
}

// the channel the line will go to, for its colour: the well takes it as you type
function channelOf(key: LineKey): "say" | "team" | "whisper" | "command" {
  if (key.kind === "team") return "team";
  if (key.kind === "whisper" || key.kind === "reply" || key.kind === "to") return "whisper";
  return key.kind === "say" ? "say" : "command";
}

// the points a scoring line granted, signed: +3, −2 (a true minus)
function pointsText(n: number): string {
  return (n < 0 ? "−" : "+") + Math.abs(n);
}

function Points({ n }: { n: number }) {
  return (
    <span className={"chat-points" + (n < 0 ? " is-loss" : "")}>
      {" "}
      {pointsText(n)}
    </span>
  );
}

function Line({ e }: { e: ChatEntry }) {
  if (e.kind === "event" || e.kind === "presence") {
    const parts = e.parts ?? [{ text: e.text }];
    // a kill with the victim's loss folded in: each number right after its player ("Gdb +3 core
    // dumped Nanu −2"); any other line: its points at the end
    const both = e.victimPoints !== undefined;
    const first = parts.findIndex((p) => p.name);
    const last = parts.length - 1 - [...parts].reverse().findIndex((p) => p.name);
    return (
      <>
        {parts.map((p, i) => (
          <span key={i}>
            {p.name ? (
              <b className="chat-who">
                <Runs runs={p.name} />
              </b>
            ) : (
              p.text
            )}
            {both && i === first && e.points !== undefined && <Points n={e.points} />}
            {both && i === last && i !== first && <Points n={e.victimPoints!} />}
          </span>
        ))}
        {!both && e.points !== undefined && <Points n={e.points} />}
      </>
    );
  }
  if (e.kind === "note") return <>{e.text}</>;
  if (e.kind === "server") return <Runs runs={e.runs ?? [{ text: e.text, color: null }]} />;
  const tag = label(e);
  const message = <Runs runs={e.runs ?? [{ text: e.text, color: null }]} />;
  if (e.kind === "whisper") {
    return (
      <>
        <span className="chat-tag">[{tag}]</span> {message}
      </>
    );
  }
  return (
    <>
      {tag && <span className="chat-tag">[{tag}] </span>}
      <b className="chat-name">
        {/* in team chat the whole line is cyan, the name too; elsewhere it keeps its codes */}
        {e.kind === "team" ? e.from?.name : <Runs runs={e.from?.runs ?? []} />}
      </b>
      : {message}
    </>
  );
}

// one line of the raw log: the game's text in its colours, a session's divider, what was run
function LogRow({ l }: { l: LogLine }) {
  if (l.kind === "line") return <Runs runs={toRuns(l.raw)} />;
  if (l.kind === "input") return <>{"> " + l.text}</>;
  return <>{l.text}</>;
}

// a chat entry as plain words, for the filter
function entryText(e: ChatEntry): string {
  return (
    (e.from ? e.from.name + " " : "") +
    (e.parts ? e.parts.map((p) => p.text).join("") : e.text) +
    (e.points !== undefined ? " " + pointsText(e.points) : "") +
    (e.victimPoints !== undefined ? " " + pointsText(e.victimPoints) : "")
  );
}

export const ChatOverlay = forwardRef<ChatOverlayHandle, ChatOverlayProps>(function ChatOverlay(
  { entries, log, players, tab, inGame, settings, compact = false, touch = false, onTab, onSend, onRun, onOpenChange },
  ref
) {
  const [open, setOpen] = useState(false);
  const [view, setView] = useState<PanelView>(tab);
  const [draft, setDraft] = useState("");
  const [now, setNow] = useState(() => performance.now());
  // the filter (Ctrl+F): null while it is not up
  const [find, setFind] = useState<string | null>(null);
  // the lines are followed (the newest in view) unless scrolled up; then the last one seen
  const [follow, setFollow] = useState(true);
  const following = useRef(true);
  following.current = follow;
  const [seen, setSeen] = useState(0);
  const [copied, setCopied] = useState<"copied" | "failed" | null>(null);
  // a command's answer, read out: the newest log id before it ran, and when
  const [asked, setAsked] = useState<{ after: number; at: number } | null>(null);
  const root = useRef<HTMLElement>(null);
  const input = useRef<HTMLInputElement>(null);
  const findInput = useRef<HTMLInputElement>(null);
  const list = useRef<HTMLOListElement>(null);
  // what this player sent (chat) and ran (console), newest last, and where Up/Down stand
  // in it (-1: the draft); the other mode's draft while one is shown
  const sent = useRef<{ chat: string[]; console: string[] }>({ chat: [], console: [] });
  const browse = useRef(-1);
  const parked = useRef<{ chat: string; console: string }>({ chat: "", console: "" });
  // Tab completion: the word being completed, the names that fit it, the one shown
  const completing = useRef<{ stem: string; before: string; names: string[]; i: number; setting: boolean } | null>(null);

  const isConsole = view === "console";
  const mode = (v: PanelView) => (v === "console" ? "console" : "chat");

  const show = (next: PanelView, prefill: string | null) => {
    // the line keeps what was typed in each mode; a prefill (an instant chat) replaces it
    if (open) parked.current[mode(view)] = draft;
    setDraft(prefill ?? parked.current[mode(next)]);
    parked.current[mode(next)] = "";
    browse.current = -1;
    completing.current = null;
    setView(next);
    setOpen(true);
    setFollow(true);
    // already open (the other key pressed): the focus goes back to the line
    requestAnimationFrame(() => input.current?.focus({ preventScroll: true }));
  };

  useImperativeHandle(ref, () => ({
    open: (prefill: string | null) => show(tab, prefill),
    openConsole: () => show("console", null)
  }));

  useEffect(() => onOpenChange(open, view), [open, view, onOpenChange]);

  // out of a game the panel is the Console alone
  useEffect(() => {
    if (open && !inGame && view !== "console") setView("console");
  }, [open, inGame, view]);

  // the line takes the focus as soon as it is drawn (the keys leave the game)
  useEffect(() => {
    if (!open) return;
    const el = input.current;
    if (!el) return;
    el.focus({ preventScroll: true });
    el.setSelectionRange(el.value.length, el.value.length);
  }, [open]);

  // a touch device: the open panel stands in what the keyboard leaves (the visual viewport)
  useEffect(() => {
    const vv = window.visualViewport;
    const el = root.current;
    if (!open || !touch || !vv || !el) return;
    const fit = () => {
      el.style.setProperty("--vv-top", Math.round(vv.offsetTop) + "px");
      el.style.setProperty("--vv-h", Math.round(vv.height) + "px");
      // the keyboard coming up shrinks the lines: the newest stays in view
      const l = list.current;
      if (l && following.current) requestAnimationFrame(() => (l.scrollTop = l.scrollHeight));
    };
    fit();
    vv.addEventListener("resize", fit);
    vv.addEventListener("scroll", fit);
    return () => {
      vv.removeEventListener("resize", fit);
      vv.removeEventListener("scroll", fit);
      el.style.removeProperty("--vv-top");
      el.style.removeProperty("--vv-h");
    };
  }, [open, touch]);

  // the fade: a tick while lines are on screen and the overlay is closed
  const newest = entries.length ? entries[entries.length - 1].at : 0;
  useEffect(() => {
    if (open) return;
    setNow(performance.now());
    const t = window.setInterval(() => setNow(performance.now()), 1000);
    return () => window.clearInterval(t);
  }, [open, newest]);

  // the CJK faces, the first time one is needed
  const needsCjk = entries.some((e) => !!cjkLang(e.text)) || !!cjkLang(draft);
  useEffect(() => {
    if (needsCjk) linkCjkFaces();
  }, [needsCjk]);

  // the lines of the view, filtered; at rest the chosen chat tab's newest
  const needle = (find ?? "").trim().toLowerCase();
  const chatLines = view === "events" || (!open && tab === "events") ? entries.filter(isEvent) : entries;
  const logShown = isConsole && open ? (needle ? log.filter((l) => l.text.toLowerCase().includes(needle)) : log) : [];
  const chatShown = !open
    ? (tab === "events" ? entries.filter(isEvent) : entries)
        .filter((e) => now - e.at < LINE_MS && !(compact && e.kind === "server"))
        .slice(compact ? -REST_LINES_COMPACT : -REST_LINES)
    : isConsole
      ? []
      : needle
        ? chatLines.filter((e) => entryText(e).toLowerCase().includes(needle))
        : chatLines;
  const total = isConsole ? log.length : chatLines.length;
  const count = isConsole ? logShown.length : chatShown.length;
  const lastId = isConsole ? (logShown.length ? logShown[logShown.length - 1].id : 0) : chatShown.length ? chatShown[chatShown.length - 1].id : 0;
  const unseen = !open || follow ? 0 : isConsole ? logShown.filter((l) => l.id > seen).length : chatShown.filter((e) => e.id > seen).length;

  // open, the newest line is in view and stays there as lines come in, unless scrolled up
  useLayoutEffect(() => {
    if (!open || !follow || !list.current) return;
    list.current.scrollTop = list.current.scrollHeight;
    setSeen(lastId);
  }, [open, view, follow, lastId, needle]);

  const onScroll = () => {
    const l = list.current;
    if (!l) return;
    const bottom = l.scrollTop + l.clientHeight >= l.scrollHeight - 4;
    if (bottom !== follow) setFollow(bottom);
    if (!bottom && follow) setSeen(lastId);
  };

  const toNewest = () => {
    setFollow(true);
    input.current?.focus({ preventScroll: true });
  };

  // the answer to the last command, for screen readers: the lines that came right after it
  const answer = asked ? log.filter((l) => l.id > asked.after && (l.kind === "line" || l.kind === "note") && l.at - asked.at < ANSWER_MS).slice(0, 3) : [];

  const close = () => {
    if (open) parked.current[mode(view)] = "";
    setOpen(false);
    setDraft("");
    setFind(null);
    setCopied(null);
    setAsked(null);
  };

  const scroll = (key: string) => {
    const l = list.current;
    if (l) l.scrollTop += (key === "PageUp" ? -1 : 1) * l.clientHeight * 0.8;
  };

  const go = (next: PanelView) => {
    if (next === view) return;
    if (next !== "console") onTab(next);
    show(next, null);
  };

  const nextView = () => go(VIEWS[(VIEWS.indexOf(view) + 1) % VIEWS.length]);

  const complete = () => {
    const el = input.current;
    if (!el) return;
    let c = completing.current;
    if (!c) {
      const caret = el.selectionStart ?? draft.length;
      const before = draft.slice(0, caret);
      const m = /(\S*)$/.exec(before);
      const stem = m ? m[1] : "";
      const lead = before.slice(0, before.length - stem.length);
      // CONSOLE: the first word is a setting's name; anything else is a player of the room
      const setting = isConsole && lead.trim() === "" && !stem.startsWith("/");
      const names = setting
        ? settings?.names(stem.toUpperCase()) ?? []
        : players
            .filter((p) => !isMe(p) && p.name.toLowerCase().startsWith(stem.toLowerCase()))
            .map((p) => p.name)
            .sort((a, b) => a.localeCompare(b));
      if (!names.length) return;
      c = { stem, before: lead, names, i: 0, setting };
    } else {
      c.i = (c.i + 1) % c.names.length;
    }
    completing.current = c;
    // a name in a command is one word (the server's filtered form); in a sentence it is as written
    const word = c.setting ? c.names[c.i] : c.before.startsWith("/") || isConsole ? c.names[c.i].replace(/\s+/g, "_") : c.names[c.i];
    const next = c.before + word + (c.before === "" && !c.setting && !isConsole && !draft.startsWith("/") ? ": " : " ");
    setDraft(next);
    requestAnimationFrame(() => el.setSelectionRange(next.length, next.length));
  };

  // CONSOLE: the setting being typed, before its value: its current value as a ghost
  const ghost = (() => {
    if (!isConsole || !settings) return "";
    const m = /^([A-Za-z0-9_]+)( ?)$/.exec(draft);
    if (!m) return "";
    const value = settings.value(m[1]);
    if (value === null) return "";
    const c = completing.current;
    const of = c && c.setting && c.names.length > 1 ? "  " + t("social.chat.completionOf", { i: c.i + 1, n: c.names.length }) : "";
    return (m[2] ? "" : " ") + "= " + value + of;
  })();

  const copyLog = () => {
    const text = logText(logShown);
    try {
      navigator.clipboard.writeText(text).then(
        () => setCopied("copied"),
        () => setCopied("failed")
      );
    } catch {
      setCopied("failed");
    }
    input.current?.focus({ preventScroll: true });
  };

  const openFind = () => {
    setFind((f) => f ?? "");
    requestAnimationFrame(() => {
      findInput.current?.focus({ preventScroll: true });
      findInput.current?.select();
    });
  };

  const clearFind = () => {
    setFind(null);
    setFollow(true);
    input.current?.focus({ preventScroll: true });
  };

  const onKey = (e: KeyboardEvent<HTMLInputElement>) => {
    // an input method composing (Korean, Japanese, Chinese): Enter, Esc and the arrows are its
    // own, to confirm or move within what is being composed
    if (e.nativeEvent.isComposing || e.keyCode === 229) return;
    if (e.key !== "Tab") completing.current = null;
    const history = sent.current[mode(view)];
    if (e.key === "Enter") {
      e.preventDefault();
      const text = draft.trim();
      if (text) sent.current[mode(view)] = [...history.filter((s) => s !== text), text].slice(-30);
      browse.current = -1;
      if (isConsole) {
        // an admin runs several in a row: the line stays, empty, and the answer lands above
        if (!text) return;
        setAsked({ after: log.length ? log[log.length - 1].id : 0, at: Date.now() });
        setFollow(true);
        setDraft("");
        onRun(text);
        return;
      }
      if (text) {
        onSend(text);
        // what you said is on ALL, not EVENTS: sent from there, the chat comes back to ALL
        if (tab === "events") onTab("all");
      }
      close();
    } else if (e.key === "Escape") {
      e.preventDefault();
      if (find !== null) clearFind();
      else close();
    } else if (e.key === "Tab") {
      e.preventDefault();
      if (e.shiftKey) {
        if (inGame) nextView();
      } else complete();
    } else if ((e.key === "f" || e.key === "F") && (e.ctrlKey || e.metaKey) && !e.altKey) {
      e.preventDefault();
      openFind();
    } else if (e.key === "ArrowUp" || e.key === "ArrowDown") {
      if (!history.length) return;
      e.preventDefault();
      let i = browse.current;
      if (e.key === "ArrowUp") i = i < 0 ? history.length - 1 : Math.max(0, i - 1);
      else i = i < 0 ? -1 : i + 1 >= history.length ? -1 : i + 1;
      browse.current = i;
      setDraft(i < 0 ? "" : history[i]);
    } else if (e.key === "PageUp" || e.key === "PageDown") {
      e.preventDefault();
      scroll(e.key);
    }
  };

  const onFindKey = (e: KeyboardEvent<HTMLInputElement>) => {
    if (e.nativeEvent.isComposing || e.keyCode === 229) return;
    if (e.key === "Escape") {
      e.preventDefault();
      clearFind();
    } else if (e.key === "Enter" || e.key === "Tab" || e.key === "ArrowDown") {
      // back to the line, the filter kept
      e.preventDefault();
      input.current?.focus({ preventScroll: true });
    } else if ((e.key === "f" || e.key === "F") && (e.ctrlKey || e.metaKey)) {
      e.preventDefault();
      findInput.current?.select();
    } else if (e.key === "PageUp" || e.key === "PageDown") {
      e.preventDefault();
      scroll(e.key);
    }
  };

  // the focus never leaves the open panel: leaving it any other way (a click on the game)
  // closes it
  const onBlur = (e: FocusEvent<HTMLElement>) => {
    if (!open) return;
    const to = e.relatedTarget as Node | null;
    if (to && root.current?.contains(to)) return;
    close();
  };

  // a whisper to you, for screen readers (not every chat line)
  const lastWhisper = [...entries].reverse().find((e) => e.kind === "whisper" && e.toYou);
  const key = lineKey(draft, view);
  const tabs: PanelView[] = inGame ? (touch ? ["all", "events"] : VIEWS) : ["console"];
  const shownCount = isConsole ? logShown.length : chatShown.length;

  return (
    <section
      ref={root}
      className={"chat" + (open ? " is-open" : "") + (open && isConsole ? " is-console" : "") + (touch ? " is-touch" : "")}
      id="chat"
      data-open={open ? "1" : ""}
      data-tab={open ? view : tab}
      data-lines={shownCount}
      aria-label={open && isConsole ? t("social.chat.consoleLabel") : t("social.chat.chatLabel")}
      hidden={!open && !chatShown.length}
      onBlur={onBlur}
    >
      {open && (
        <div className="chat-head">
          <div className="chat-tabs" role="tablist" aria-label={t("social.chat.tabsLabel")}>
            {tabs.map((v) => (
              <button
                key={v}
                type="button"
                role="tab"
                className={"chat-tab" + (view === v ? " is-on" : "")}
                aria-selected={view === v}
                tabIndex={-1}
                // the line keeps the focus (a click would close the overlay)
                onMouseDown={(e) => e.preventDefault()}
                onClick={() => go(v)}
              >
                {t(VIEW_WORDS[v])}
              </button>
            ))}
          </div>
          {find !== null && (
            <label className="chat-find">
              <span className="chat-find-key">{t("social.chat.find")}</span>
              <input
                ref={findInput}
                className="chat-find-input"
                id="chat-find"
                type="text"
                value={find}
                autoComplete="off"
                spellCheck={false}
                aria-label={t("social.chat.findLabel")}
                aria-describedby="chat-find-count"
                onChange={(e) => {
                  setFind(e.target.value);
                  setFollow(true);
                }}
                onKeyDown={onFindKey}
              />
              <span className="chat-find-count" id="chat-find-count">
                {needle ? t("social.chat.findCount", { count, total }) : ""}
              </span>
            </label>
          )}
          {touch && (
            <button type="button" className="chat-close" id="chat-close" aria-label={t("social.chat.close")} tabIndex={-1} onMouseDown={(e) => e.preventDefault()} onClick={close}>
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="square" aria-hidden="true">
                <path d="M6 6l12 12M18 6L6 18" />
              </svg>
            </button>
          )}
        </div>
      )}
      <ol
        className="chat-lines"
        ref={list}
        onScroll={open ? onScroll : undefined}
        {...(open ? { role: "region", "aria-label": isConsole ? t("social.chat.logLabel") : t("social.chat.linesLabel"), tabIndex: -1 } : {})}
      >
        {isConsole
          ? logShown.map((l) => (
              <li key={l.id} className={"chat-line is-log is-" + l.kind}>
                <span className="chat-strip">
                  <LogRow l={l} />
                </span>
              </li>
            ))
          : chatShown.map((e) => (
              <li key={e.id} className={"chat-line is-" + e.kind}>
                <span className="chat-strip" lang={cjkLang(e.text)}>
                  <Line e={e} />
                </span>
              </li>
            ))}
        {open && !shownCount && (
          <li className="chat-line is-empty">
            {needle ? t("social.chat.emptyFind") : isConsole ? t("social.chat.emptyLog") : view === "events" ? t("social.chat.emptyEvents") : t("social.chat.emptyChat")}
          </li>
        )}
      </ol>
      {open && unseen > 0 && (
        <button type="button" className="chat-unseen" id="chat-unseen" tabIndex={-1} onMouseDown={(e) => e.preventDefault()} onClick={toNewest}>
          {t("social.chat.newLines", { n: unseen })} ↓
        </button>
      )}
      {open && (
        <>
          <label className={"chat-well is-" + (isConsole ? "command" : channelOf(key))}>
            <span className="chat-key" id="chat-key">
              {keyText(key)}
            </span>
            <span className="chat-field">
              <input
                ref={input}
                className="chat-input"
                lang={isConsole ? undefined : cjkLang(draft)}
                id="chat-input"
                type="text"
                value={draft}
                maxLength={isConsole ? 1000 : 240}
                autoComplete="off"
                spellCheck={false}
                enterKeyHint={isConsole ? "go" : "send"}
                aria-label={isConsole ? t("social.chat.commandLabel") : t("social.chat.chatLabel")}
                aria-describedby="chat-key"
                onChange={(e) => {
                  completing.current = null;
                  setDraft(e.target.value);
                }}
                onKeyDown={onKey}
              />
              {ghost && (
                <span className="chat-ghost" aria-hidden="true">
                  <span className="chat-ghost-typed">{draft}</span>
                  {ghost}
                </span>
              )}
            </span>
          </label>
          {isConsole && (
            <div className="chat-foot">
              <button type="button" className="chat-copy" id="chat-copy" tabIndex={-1} onMouseDown={(e) => e.preventDefault()} onClick={copyLog}>
                {copied === "copied" ? t("social.chat.copied") : copied === "failed" ? t("social.chat.copyRefused") : t("social.chat.copyLog")}
              </button>
            </div>
          )}
        </>
      )}
      <p className="chat-live" aria-live="polite">
        {open && isConsole
          ? answer.map((l) => stripCodes(l.text)).join(". ")
          : lastWhisper
            ? t("social.chat.whispers", { name: lastWhisper.from?.name ?? "", text: lastWhisper.text })
            : ""}
      </p>
    </section>
  );
});
