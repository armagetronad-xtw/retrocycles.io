// The chat dock: direct messages in a window docked at the
// bottom right of the home screen, outside the Friends panel, so a conversation stays open while
// the player does anything else there.
//   window   one conversation at a time: the friend as the window's heading (swatch, name, level,
//            where they are; RECONNECTING while the stream is down), JOIN when their room may be
//            joined, MINIMIZE and CLOSE; the messages oldest first in runs (theirs at the left,
//            mine at the right, a time over each run, a day line when the day changes, NEW over
//            the first unread one until the player answers); a line being sent shows faded at
//            once; the well under it grows with the text up to five lines (Enter sends,
//            Shift+Enter breaks the line; past the limit it says how many characters are too
//            many instead of cutting them, and Enter says why it did not send)
//   tabs     along the bottom edge: every other conversation open here, and one for each friend
//            whose unread messages wait (their count; past five the rest fold into +N). A
//            message that arrives while its conversation is not in view lights its tab with the
//            line itself for a few seconds (held while pointed at), then the tab settles to the
//            name and the count. Nothing takes the keyboard; a screen reader hears it through one
//            polite status line. Closing a tab only tidies the dock: its messages stay unread.
// A conversation is read only when it is seen: the window open, the page in front, no game, the
// list at its foot; until then the count stays (the tab, the messages cell, the page's title).
// Opened from the Friends panel's MESSAGE (the panel stays: the dock sits at its left; between
// 761 and 1000px wide it stands over the panel and folds to its tab when the panel is used),
// from the status bar's messages cell (the newest unread conversation, else the newest; again,
// it folds the window), or from a tab. Esc in the dock minimizes the window to its tab. On a
// phone a press above the window folds it, and a long press on a tab closes it. In a game the
// dock is hidden (the game's chat carries direct messages there as whispers) and comes back with
// the counts when the game ends. The open conversation follows the event stream; where there is
// none (a Discord Activity's bearer session, the stream down) it is asked every few seconds
// while it is in view, and the conversations' polled last message says when to ask it at once.
import { forwardRef, useEffect, useImperativeHandle, useLayoutEffect, useMemo, useRef, useState, type FormEvent, type KeyboardEvent as ReactKeyboardEvent, type PointerEvent as ReactPointerEvent } from "react";
import * as api from "../platform/client";
import { socialText } from "../platform/friends";
import { markRead, onMessage, useConversations, useStreamState } from "../platform/messages";
import type { FriendInfo, FriendsList, MessageInfo, Person, PlatformState } from "../platform/types";
import { whereText, online } from "./Friends";
import { Icon } from "./Icons";
import { currentLang, t } from "../i18n";

// the server's limit on a message
const MAX = 240;
// the count of characters left shows from here
const LEFT_FROM = 40;
// how long an arrived line shows on its tab, and how long it stays after the pointer leaves it
const PREVIEW_MS = 6000;
const PREVIEW_AFTER_MS = 3000;
// tabs kept: past this the oldest without unread messages goes (never the one being opened)
const MAX_TABS = 5;
// tabs shown: past this the rest fold into +N
const SHOWN_TABS = 5;
// a run: the same author, each message within this of the last
const RUN_S = 300;
// the open conversation asked this often while no stream brings its lines
const POLL_MS = 5000;
// a press held this long on a tab (a phone) closes it
const LONG_PRESS_MS = 550;
// the widths where the dock stands over a sheet instead of beside it, and a phone's
const MID_WIDTH = "(min-width: 761px) and (max-width: 1000px)";
const PHONE_WIDTH = "(max-width: 760px)";
// the sheets at the right edge
const SHEETS = "#friends, #profile, #settings";

export interface ChatDockHandle {
  // a friend's conversation in the window (focus: the well takes the keyboard)
  open(userId: string, focus?: boolean): void;
  // the messages cell: the window folded when one is open, else the newest unread conversation,
  // else the newest one; false when there is none
  toggle(): boolean;
  // Esc inside the dock: the window to its tab; false when nothing was open
  back(): boolean;
}


interface ChatDockProps {
  platform: PlatformState;
  friends: FriendsList | null;
  hidden: boolean; // a game runs, or another page covers the home screen
  beside: boolean; // a sheet holds the right edge: the dock sits at its left
  onJoin?(friend: FriendInfo): void;
}

interface Tab {
  userId: string;
  person: Person;
}

// a line on its way: shown faded until the platform has it (or back in the well when it refused)
interface Pending {
  text: string;
}

const pendingId = Number.MAX_SAFE_INTEGER;

export const ChatDock = forwardRef<ChatDockHandle, ChatDockProps>(function ChatDock({ platform, friends, hidden, beside, onJoin }, ref) {
  const conversations = useConversations();
  const stream = useStreamState();
  const apiUrl = platform.mode === "platform" ? platform.apiUrl : null;
  const me = platform.mode === "platform" ? platform.user?.id ?? "" : "";

  const [tabs, setTabs] = useState<Tab[]>([]);
  const [openId, setOpenId] = useState<string | null>(null);
  const [previews, setPreviews] = useState<Record<string, string>>({});
  const [messages, setMessages] = useState<MessageInfo[] | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [loadTick, setLoadTick] = useState(0);
  // the first message unread when the conversation opened (NEW goes over it until the player answers)
  const [firstNew, setFirstNew] = useState<number | null>(null);
  const [drafts, setDrafts] = useState<Record<string, string>>({});
  const [pending, setPending] = useState<Record<string, Pending>>({});
  const [sendError, setSendError] = useState<string | null>(null);
  // why Enter did not send (too long, the last line still on its way)
  const [sendHint, setSendHint] = useState<string | null>(null);
  // the first line sent from the dock on this page load: until then the well says how Enter works
  const [sentOnce, setSentOnce] = useState(false);
  const [focusWell, setFocusWell] = useState(0);
  const [pageVisible, setPageVisible] = useState(() => typeof document === "undefined" || document.visibilityState === "visible");
  // what a screen reader hears: an arrived line (the polite status line; n makes a repeat news)
  const [heard, setHeard] = useState({ n: 0, text: "" });
  const say = (text: string) => setHeard((h) => ({ n: h.n + 1, text }));

  const listRef = useRef<HTMLOListElement>(null);
  const wellRef = useRef<HTMLTextAreaElement>(null);
  const dockRef = useRef<HTMLDivElement>(null);
  const seen = useRef<Record<string, number> | null>(null);
  // conversations closed with unread messages: no tab again until a new one comes
  const dismissed = useRef(new Set<string>());
  const openRef = useRef(openId);
  openRef.current = openId;
  const hiddenRef = useRef(hidden);
  hiddenRef.current = hidden;
  const visibleRef = useRef(pageVisible);
  visibleRef.current = pageVisible;
  const besideRef = useRef(beside);
  besideRef.current = beside;
  // the newest message id shown in the window (the polled last message is compared with it)
  const newestShown = useRef(0);
  // the conversation whose first placement was done (NEW, else the foot), and whether the list
  // sits at its foot (new lines and a growing well keep the newest in view only then; reading
  // waits for it)
  const placedFor = useRef<string | null>(null);
  const atFoot = useRef(true);
  const timers = useRef<Record<string, number>>({});
  const longPress = useRef<{ timer: number; fired: boolean } | null>(null);

  const friendOf = (userId: string): FriendInfo | null => friends?.friends.find((f) => f.user.userId === userId) ?? null;
  const unreadOf = (userId: string): number => conversations.find((c) => c.user.userId === userId)?.unread ?? 0;

  // the open conversation read, when it is seen: the page in front, no game, the list at its foot
  const readIfSeen = (userId: string) => {
    if (openRef.current !== userId || hiddenRef.current || !visibleRef.current || !atFoot.current) return;
    if (unreadOf(userId) > 0) markRead(userId);
  };

  const addTab = (list: Tab[], tab: Tab, keep: string | null): Tab[] => {
    if (list.some((x) => x.userId === tab.userId)) return list;
    const next = [...list, tab];
    while (next.length > MAX_TABS) {
      const drop = next.findIndex((x) => x.userId !== keep && x.userId !== tab.userId && x.userId !== openRef.current && unreadOf(x.userId) === 0);
      if (drop < 0) break;
      next.splice(drop, 1);
    }
    return next;
  };

  const personOf = (userId: string): Person | null =>
    friendOf(userId)?.user ?? conversations.find((c) => c.user.userId === userId)?.user ?? tabs.find((x) => x.userId === userId)?.person ?? null;

  const show = (userId: string, focus: boolean) => {
    const person = personOf(userId);
    if (!person) return;
    dismissed.current.delete(userId);
    setTabs((list) => addTab(list, { userId, person }, userId));
    setPreviews(({ [userId]: _, ...rest }) => rest);
    setSendError(null);
    setSendHint(null);
    setOpenId(userId);
    if (focus) setFocusWell((n) => n + 1);
  };

  // the window folded to its tab; the focus to that tab (else the messages cell, else the chip)
  // when it was in the dock
  const minimize = (moveFocus = true) => {
    const was = openRef.current;
    if (!was) return;
    setOpenId(null);
    if (!moveFocus) return;
    window.setTimeout(() => {
      const to = document.getElementById("dm-tab-" + was) ?? document.getElementById("messages-cell") ?? document.getElementById("chip");
      to?.focus({ preventScroll: true });
    }, 0);
  };

  useImperativeHandle(ref, () => ({
    open: (userId, focus = true) => show(userId, focus),
    toggle() {
      if (openId) {
        minimize(false);
        return true;
      }
      const c = conversations.find((x) => x.unread > 0) ?? conversations[0];
      if (!c) return false;
      show(c.user.userId, true);
      return true;
    },
    back() {
      if (!openId) return false;
      minimize();
      return true;
    }
  }));

  // the documents line at the bottom right gives way while the window stands over it,
  // and steps up over the tabs while they stand under it
  const tabsUp = !hidden && tabs.some((x) => x.userId !== openId);
  useEffect(() => {
    const up = !!openId && !hidden;
    document.body.classList.toggle("has-dm-window", up);
    document.body.classList.toggle("has-dm-tabs", tabsUp);
    return () => document.body.classList.remove("has-dm-window", "has-dm-tabs");
  }, [openId, hidden, tabsUp]);

  // the page in front or not (a background tab reads nothing)
  useEffect(() => {
    const changed = () => setPageVisible(document.visibilityState === "visible");
    document.addEventListener("visibilitychange", changed);
    return () => document.removeEventListener("visibilitychange", changed);
  }, []);

  // a phone's keyboard: the window stands on what is left of the screen above it
  useEffect(() => {
    const vv = window.visualViewport;
    if (!vv) return;
    const fit = () => {
      const covered = Math.max(0, window.innerHeight - vv.height - vv.offsetTop);
      document.documentElement.style.setProperty("--dm-kb", covered + "px");
      document.documentElement.style.setProperty("--dm-vh", vv.height + "px");
    };
    fit();
    vv.addEventListener("resize", fit);
    vv.addEventListener("scroll", fit);
    return () => {
      vv.removeEventListener("resize", fit);
      vv.removeEventListener("scroll", fit);
    };
  }, []);

  // the window folds when it would stand in the way: between 761 and 1000px wide a sheet used
  // under it (a press or the focus in the sheet), on a phone a press above it
  useEffect(() => {
    const mid = window.matchMedia(MID_WIDTH);
    const phone = window.matchMedia(PHONE_WIDTH);
    const away = (e: Event) => {
      if (!openRef.current) return;
      const target = e.target as Element | null;
      if (!target || !(target instanceof Element) || dockRef.current?.contains(target)) return;
      if (mid.matches && besideRef.current && target.closest(SHEETS)) minimize(false);
      else if (phone.matches && e.type === "pointerdown" && !target.closest("#messages-cell")) minimize(false);
    };
    document.addEventListener("pointerdown", away, true);
    document.addEventListener("focusin", away, true);
    return () => {
      document.removeEventListener("pointerdown", away, true);
      document.removeEventListener("focusin", away, true);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);


  // the conversations' counts: a tab for every unread one (not one closed since, until a new
  // message); a count that grew while its conversation was not in view lights its tab with the
  // line and is said to a screen reader. The open one: a last message newer than the window
  // shows is asked for first, then it is read once seen
  useEffect(() => {
    const before = seen.current;
    seen.current = Object.fromEntries(conversations.map((c) => [c.user.userId, c.unread]));
    for (const c of conversations) if (before && c.unread > (before[c.user.userId] ?? 0)) dismissed.current.delete(c.user.userId);
    const waiting = conversations.filter((c) => c.unread > 0 && !dismissed.current.has(c.user.userId));
    if (waiting.length) setTabs((list) => waiting.reduce((acc, c) => addTab(acc, { userId: c.user.userId, person: c.user }, null), list));
    const open = openId && conversations.find((c) => c.user.userId === openId);
    if (open && messages !== null && open.last.id > newestShown.current) setLoadTick((n) => n + 1);
    else if (open && messages !== null && open.unread > 0) readIfSeen(open.user.userId);
    for (const c of waiting) {
      const id = c.user.userId;
      if (id === openId && !hidden) continue;
      // the first count after the page opened is not news: those tabs come quietly
      if (!before || c.unread <= (before[id] ?? 0) || c.last.from === me) continue;
      setPreviews((p) => ({ ...p, [id]: c.last.text }));
      say(t("social.dm.arrived", { name: c.user.name, text: c.last.text }));
      window.clearTimeout(timers.current[id]);
      timers.current[id] = window.setTimeout(() => setPreviews(({ [id]: _, ...rest }) => rest), PREVIEW_MS);
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [conversations]);

  // back from a game, or the page back in front, with the window open: what came meanwhile is
  // asked for (NEW over it), and read once seen
  useEffect(() => {
    if (!hidden && pageVisible && openId) setLoadTick((n) => n + 1);
    else if (!hidden && pageVisible && openId) readIfSeen(openId);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [hidden, pageVisible]);

  // no stream (a Discord Activity, the stream down): the open conversation asked every few
  // seconds while it is in view
  useEffect(() => {
    if (!openId || hidden || !pageVisible || stream === "live") return;
    const timer = window.setInterval(() => setLoadTick((n) => n + 1), POLL_MS);
    return () => window.clearInterval(timer);
  }, [openId, hidden, pageVisible, stream]);

  useEffect(() => {
    const all = timers.current;
    return () => Object.values(all).forEach((x) => window.clearTimeout(x));
  }, []);

  // a new conversation in the window: nothing of the last one stays
  useEffect(() => {
    setMessages(null);
    setLoadError(null);
    setFirstNew(null);
    newestShown.current = 0;
    placedFor.current = null;
    atFoot.current = true;
  }, [openId]);

  // the open conversation: asked when it opens (and again when the counts say it moved, or the
  // poll says so), read once seen; the event stream brings the rest while it is up
  useEffect(() => {
    if (!openId) return;
    const other = openId;
    const firstUnread = (list: MessageInfo[]) => list.find((m) => m.from === other && !m.read)?.id ?? null;
    if (!apiUrl) return;
    let live = true;
    api.getConversation(apiUrl, other).then(
      (list) => {
        if (!live) return;
        setLoadError(null);
        setMessages((now) => {
          // what the stream brought while it was asked stays
          const extra = (now ?? []).filter((m) => !list.some((x) => x.id === m.id));
          const next = [...list, ...extra];
          return now && now.length === next.length && now.every((m, i) => m.id === next[i].id) ? now : next;
        });
        setFirstNew((was) => was ?? firstUnread(list));
      },
      (e) => {
        if (!live) return;
        setMessages((now) => now ?? []);
        setLoadError(socialText(e, personOf(other)?.name));
      }
    );
    return () => {
      live = false;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [openId, apiUrl, loadTick]);

  // the stream's lines for the open conversation, as they come
  useEffect(() => {
    if (!openId) return;
    const other = openId;
    return onMessage((m) => {
      if (m.from !== other && m.to !== other) return;
      setMessages((list) => (list ? (list.some((x) => x.id === m.id) ? list : [...list, m]) : list));
      if (m.from === other) say(t("social.dm.arrived", { name: personOf(other)?.name ?? "", text: m.text }));
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [openId]);

  useEffect(() => {
    newestShown.current = messages?.reduce((n, m) => Math.max(n, m.id), 0) ?? 0;
  }, [messages]);

  // where the list stands: on its first showing NEW at the top when what follows it overflows,
  // else the newest at the foot (again once the faces have loaded: the lines re-flow); after
  // that a new line keeps the foot in view only when the list was at its foot. Then what is in
  // view may be read
  useLayoutEffect(() => {
    const l = listRef.current;
    if (!l || hidden || messages === null || !openId) return;
    const id = openId;
    if (placedFor.current === id) {
      if (atFoot.current) l.scrollTop = l.scrollHeight;
      readIfSeen(id);
      return;
    }
    const place = () => {
      if (l.clientHeight === 0) return;
      const mark = firstNew !== null ? l.querySelector<HTMLElement>(".dm-new") : null;
      l.scrollTop = mark ? Math.min(mark.offsetTop - 8, l.scrollHeight - l.clientHeight) : l.scrollHeight;
      atFoot.current = l.scrollHeight - l.scrollTop - l.clientHeight < 24;
      placedFor.current = id;
      readIfSeen(id);
    };
    place();
    const frame = requestAnimationFrame(place);
    let live = true;
    void document.fonts?.ready.then(() => live && placedFor.current === id && place());
    return () => {
      live = false;
      cancelAnimationFrame(frame);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [messages, pending, firstNew, hidden, openId]);

  // the well: its height follows the text (CSS caps it at five lines)
  const draft = openId ? (drafts[openId] ?? "") : "";
  useLayoutEffect(() => {
    const w = wellRef.current;
    if (!w) return;
    w.style.height = "auto";
    w.style.height = w.scrollHeight + "px";
    const l = listRef.current;
    if (l && atFoot.current) l.scrollTop = l.scrollHeight;
  }, [draft, openId]);

  useEffect(() => {
    if (focusWell && !hidden) wellRef.current?.focus({ preventScroll: true });
  }, [focusWell, hidden, openId]);

  const friendGone = !!openId && !!friends && !friendOf(openId);
  const over = draft.length - MAX;

  // past the limit: a screen reader hears it once, when it is crossed (the count is not live)
  const wasOver = useRef(false);
  useEffect(() => {
    if (over > 0 && !wasOver.current) say(t("social.dm.over", { n: over }));
    wasOver.current = over > 0;
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [over > 0]);

  const send = async (e?: FormEvent) => {
    e?.preventDefault();
    if (!openId || friendGone) return;
    const to = openId;
    const text = draft.trim();
    if (!text) return;
    if (over > 0) {
      setSendHint(t("social.dm.tooLong", { n: over }));
      return;
    }
    if (pending[to]) {
      setSendHint(t("social.dm.stillSending"));
      return;
    }
    setSendError(null);
    setSendHint(null);
    setSentOnce(true);
    setDrafts((d) => ({ ...d, [to]: "" }));
    setFirstNew(null);
    atFoot.current = true;
    if (!apiUrl) return;
    setPending((p) => ({ ...p, [to]: { text } }));
    try {
      const m = await api.sendMessage(apiUrl, to, text);
      if (openRef.current === to) setMessages((list) => (list && !list.some((x) => x.id === m.id) ? [...list, m] : list));
    } catch (err) {
      // the line goes back into the well, unless something was typed meanwhile
      setDrafts((d) => ({ ...d, [to]: d[to] ? d[to] : text }));
      if (openRef.current === to) setSendError(t("social.friends.notSent", { why: socialText(err, personOf(to)?.name) }));
    } finally {
      setPending(({ [to]: _, ...rest }) => rest);
      // the focus back to the well only when it is still in the dock (the player may have gone on)
      const at = document.activeElement;
      if (openRef.current === to && (!at || at === document.body || dockRef.current?.contains(at))) wellRef.current?.focus({ preventScroll: true });
    }
  };

  const wellKey = (e: ReactKeyboardEvent<HTMLTextAreaElement>) => {
    if (e.key === "Enter" && !e.shiftKey && !e.nativeEvent.isComposing) {
      e.preventDefault();
      void send();
    }
  };

  // CLOSE (the window's or a tab's): the conversation leaves the dock; its unread messages stay
  // unread (the messages cell keeps the count), and its tab comes back with the next one
  const close = (userId: string) => {
    if (unreadOf(userId) > 0) dismissed.current.add(userId);
    setTabs((list) => list.filter((x) => x.userId !== userId));
    setPreviews(({ [userId]: _, ...rest }) => rest);
    if (openRef.current === userId) {
      setOpenId(null);
      window.setTimeout(() => (document.getElementById("messages-cell") ?? document.getElementById("chip"))?.focus({ preventScroll: true }), 0);
    }
  };

  // a lit tab's line holds while it is pointed at, and goes a little after the pointer leaves
  const holdPreview = (userId: string) => window.clearTimeout(timers.current[userId]);
  const releasePreview = (userId: string) => {
    if (!previews[userId]) return;
    window.clearTimeout(timers.current[userId]);
    timers.current[userId] = window.setTimeout(() => setPreviews(({ [userId]: _, ...rest }) => rest), PREVIEW_AFTER_MS);
  };

  // a long press on a tab (a touch) closes it
  const pressStart = (userId: string, e: ReactPointerEvent) => {
    if (e.pointerType !== "touch") return;
    const timer = window.setTimeout(() => {
      if (longPress.current) longPress.current.fired = true;
      navigator.vibrate?.(15);
      close(userId);
    }, LONG_PRESS_MS);
    longPress.current = { timer, fired: false };
  };
  const pressEnd = () => {
    if (longPress.current && !longPress.current.fired) window.clearTimeout(longPress.current.timer);
  };

  const pendingHere = openId ? pending[openId] : undefined;
  const shown = useMemo(() => {
    const list = messages ?? [];
    return pendingHere && openId ? [...list, { id: pendingId, from: me, to: openId, text: pendingHere.text, createdAt: Math.round(Date.now() / 1000), read: true }] : list;
  }, [messages, pendingHere, openId, me]);
  const runs = useMemo(() => toRuns(shown, openId ?? ""), [shown, openId]);

  const status = (
    <p className="vh" role="status" aria-live="polite">
      <span key={heard.n}>{heard.text}</span>
    </p>
  );

  if (!me || (!tabs.length && !openId)) return status;

  const person = openId ? personOf(openId) : null;
  const friend = openId ? friendOf(openId) : null;
  const where = friend ? whereText(friend.presence) || t("social.where.offline") : "";
  const left = MAX - draft.length;
  // the nearest the corner first: the row reads right to left on screen, so it is laid out in
  // that order and the Tab order follows what the eye sees; past five the farthest fold into +N
  const waiting = tabs.filter((x) => x.userId !== openId).reverse();
  const shownTabs = waiting.length > SHOWN_TABS ? waiting.slice(waiting.length - (SHOWN_TABS - 1)) : waiting;
  const folded = waiting.length > SHOWN_TABS ? waiting.slice(0, waiting.length - (SHOWN_TABS - 1)) : [];
  const reach = friendGone
    ? null
    : friend && !online(friend)
      ? t("social.dm.offline", { name: person?.name ?? "" })
      : friend?.presence?.state === "playing" || friend?.presence?.state === "spectating"
        ? t("social.dm.inGame", { name: person?.name ?? "" })
        : null;
  const hint = sendHint ?? (!sentOnce && draft.trim() && left > LEFT_FROM ? t("social.dm.enterHint") : null);
  const describedBy = [reach && "dm-note", hint && "dm-hint", left <= LEFT_FROM && "dm-left"].filter(Boolean).join(" ") || undefined;

  return (
    <div className={"dm-dock" + (beside ? " is-beside" : "")} id="dm-dock" ref={dockRef} hidden={hidden}>
      {status}
      {openId && person && (
        <section className="dm-window" id="dm-window" data-with={openId} aria-labelledby="dm-who">
          <header className="dm-head">
            <i className="dm-swatch" aria-hidden="true" style={friend?.presence?.color ? { background: friend.presence.color } : undefined} />
            <h2 className="dm-who" id="dm-who">
              <span className="dm-name">
                {person.name}
                {person.level != null && <span className="dm-level">{t("social.friends.lv", { level: person.level })}</span>}
              </span>
              {where && <span className={"dm-where" + (friend && online(friend) ? " is-online" : "")}>{where}</span>}
              {stream === "down" && <span className="dm-reconnecting">{t("social.dm.reconnecting")}</span>}
            </h2>
            {onJoin && friend?.presence?.joinable && (
              <button className="dm-join" id="dm-join" type="button" aria-label={t("social.dm.joinWith", { name: person.name })} onClick={() => onJoin(friend)}>
                {t("social.friends.join")}
              </button>
            )}
            <button className="dm-icon" id="dm-minimize" type="button" aria-label={t("social.dm.minimizeWith", { name: person.name })} title={t("social.dm.minimize")} onClick={() => minimize()}>
              <Icon name="minus" />
            </button>
            <button className="dm-icon dm-close" id="dm-close" type="button" aria-label={t("social.dm.closeWith", { name: person.name })} title={t("social.dm.close")} onClick={() => close(openId)}>
              <Icon name="x" />
            </button>
          </header>

          <ol
            className="dm-messages"
            id="dm-messages"
            ref={listRef}
            tabIndex={0}
            onScroll={(e) => {
              const l = e.currentTarget;
              const foot = l.scrollHeight - l.scrollTop - l.clientHeight < 24;
              atFoot.current = foot;
              if (foot) readIfSeen(openId);
            }}
            aria-label={t("social.friends.messagesWith", { name: person.name })}
          >
            {messages === null ? (
              <li className="dm-say">{t("social.dm.loading")}</li>
            ) : loadError && messages.length === 0 ? (
              <li className="dm-say dm-failed">
                <span className="dm-error">{t("social.dm.loadFailed", { why: loadError })}</span>
                <button className="dm-retry" type="button" onClick={() => setLoadTick((n) => n + 1)}>
                  {t("social.dm.retry")}
                </button>
              </li>
            ) : shown.length === 0 ? (
              <li className="dm-say">{t("social.friends.noMessages")}</li>
            ) : (
              runs.map((r) => (
                <li className={"dm-run" + (r.mine ? " is-mine" : "")} key={r.messages[0].id}>
                  {r.day && <p className="dm-day">{r.day}</p>}
                  {r.messages[0].id === firstNew && <p className="dm-new">{t("social.dm.new")}</p>}
                  <p className="dm-at">
                    <span className="vh">{r.mine ? t("social.dm.authorYou") : t("social.dm.authorThem", { name: person.name })} </span>
                    {clock(r.messages[0].createdAt)}
                  </p>
                  {r.messages.map((m) => {
                    const sending = m.id === pendingId;
                    return (
                      <p className={"dm-line" + (sending ? " is-sending" : "")} key={m.id} id={sending ? "dm-sending" : "dm-" + m.id}>
                        {m.text}
                        {sending && <span className="dm-sending-note">{t("social.dm.sending")}</span>}
                      </p>
                    );
                  })}
                </li>
              ))
            )}
          </ol>

          <form className="dm-write" onSubmit={(e) => void send(e)} noValidate>
            {friendGone ? (
              <p className="dm-note">{t("social.dm.notFriends", { name: person.name })}</p>
            ) : (
              <>
                {reach && (
                  <p className="dm-note" id="dm-note">
                    {reach}
                  </p>
                )}
                {sendError && (
                  <p className="dm-error" role="alert">
                    {sendError}
                  </p>
                )}
                <div className="dm-well-row">
                  <textarea
                    ref={wellRef}
                    className="dm-well"
                    id="dm-well"
                    rows={1}
                    value={draft}
                    placeholder={t("social.friends.messageLabel", { name: person.name })}
                    aria-label={t("social.friends.messageLabel", { name: person.name })}
                    aria-invalid={over > 0 || undefined}
                    aria-describedby={describedBy}
                    autoComplete="off"
                    spellCheck
                    onChange={(e) => {
                      const v = e.target.value;
                      setDrafts((d) => ({ ...d, [openId]: v }));
                      if (sendError) setSendError(null);
                      if (sendHint) setSendHint(null);
                    }}
                    onKeyDown={wellKey}
                  />
                  <button
                    className="dm-send"
                    id="dm-send"
                    type="submit"
                    aria-disabled={!!pendingHere || !draft.trim() || over > 0}
                    aria-label={t("social.dm.send")}
                    title={t("social.dm.send")}
                  >
                    <Icon name="arrow-up" />
                  </button>
                </div>
                {(hint || left <= LEFT_FROM) && (
                  <div className="dm-under">
                    {hint && (
                      <p className={"dm-hint" + (sendHint ? " is-said" : "")} id="dm-hint">
                        {hint}
                      </p>
                    )}
                    {left <= LEFT_FROM && (
                      <p className={"dm-left" + (over > 0 ? " is-over" : left <= 10 ? " is-low" : "")} id="dm-left">
                        {over > 0 ? t("social.dm.over", { n: over }) : t("social.dm.left", { n: left })}
                      </p>
                    )}
                  </div>
                )}
              </>
            )}
          </form>
        </section>
      )}

      {waiting.length > 0 && (
        <ul className="dm-tabs" id="dm-tabs" aria-label={t("social.dm.tabs")}>
          {folded.length > 0 && (
            <li className={"dm-tab dm-more" + (folded.some((x) => unreadOf(x.userId) > 0) ? " has-new" : "")}>
              <button
                className="dm-tab-open"
                id="dm-tab-more"
                type="button"
                aria-label={t("social.dm.more", { n: folded.length })}
                title={folded.map((x) => x.person.name).join(", ")}
                onClick={() => show(folded[folded.length - 1].userId, true)}
              >
                <span className="dm-tab-name">{t("social.dm.moreShort", { n: folded.length })}</span>
              </button>
            </li>
          )}
          {shownTabs.map((x) => {
            const n = unreadOf(x.userId);
            const f = friendOf(x.userId);
            const line = previews[x.userId];
            return (
              <li
                className={"dm-tab" + (n ? " has-new" : "") + (line ? " is-lit" : "")}
                key={x.userId}
                onMouseEnter={() => holdPreview(x.userId)}
                onMouseLeave={() => releasePreview(x.userId)}
              >
                <button
                  className="dm-tab-open"
                  id={"dm-tab-" + x.userId}
                  type="button"
                  aria-label={n ? t("social.dm.tabNew", { name: x.person.name, n }) : t("social.dm.label", { name: x.person.name })}
                  onPointerDown={(e) => pressStart(x.userId, e)}
                  onPointerUp={pressEnd}
                  onPointerCancel={pressEnd}
                  onPointerLeave={pressEnd}
                  onContextMenu={(e) => longPress.current && e.preventDefault()}
                  onClick={() => {
                    if (longPress.current?.fired) {
                      longPress.current = null;
                      return;
                    }
                    show(x.userId, true);
                  }}
                >
                  <i className="dm-swatch" aria-hidden="true" style={f?.presence?.color ? { background: f.presence.color } : undefined} />
                  <span className="dm-tab-name">{x.person.name}</span>
                  {n > 0 && <span className="dm-tab-n">{n}</span>}
                  {line && <span className="dm-tab-line">{line}</span>}
                </button>
                <button className="dm-tab-x" type="button" aria-label={t("social.dm.closeWith", { name: x.person.name })} title={t("social.dm.close")} onClick={() => close(x.userId)}>
                  <Icon name="x" />
                </button>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
});

interface Run {
  mine: boolean;
  day: string | null; // the day's line, over the first run of a day
  messages: MessageInfo[];
}

function toRuns(list: MessageInfo[], other: string): Run[] {
  const runs: Run[] = [];
  let lastDay = "";
  for (const m of list) {
    const mine = m.from !== other;
    const dayKey = new Date(m.createdAt * 1000).toDateString();
    const prev = runs[runs.length - 1];
    const last = prev?.messages[prev.messages.length - 1];
    if (prev && prev.mine === mine && dayKey === lastDay && last && m.createdAt - last.createdAt <= RUN_S && !(m.from === other && !m.read && last.read)) {
      prev.messages.push(m);
      continue;
    }
    runs.push({ mine, day: dayKey !== lastDay ? dayLine(m.createdAt) : null, messages: [m] });
    lastDay = dayKey;
  }
  return runs;
}

// "Today", "Yesterday", else the date, in the page's language
function dayLine(unixSeconds: number): string {
  const d = new Date(unixSeconds * 1000);
  const today = new Date();
  today.setHours(0, 0, 0, 0);
  const that = new Date(d);
  that.setHours(0, 0, 0, 0);
  const days = Math.round((that.getTime() - today.getTime()) / 86400000);
  if (days === 0 || days === -1) return new Intl.RelativeTimeFormat(currentLang(), { numeric: "auto" }).format(days, "day");
  return new Intl.DateTimeFormat(currentLang(), { month: "short", day: "numeric", ...(d.getFullYear() !== today.getFullYear() ? { year: "numeric" } : {}) }).format(d);
}

function clock(unixSeconds: number): string {
  return new Intl.DateTimeFormat(currentLang(), { hour: "numeric", minute: "2-digit" }).format(new Date(unixSeconds * 1000));
}
