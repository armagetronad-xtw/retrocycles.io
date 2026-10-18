// The Friends panel: its own sheet
// from its own status cell, in the Profile sheet's grammar (profile.css: the same square sheet at
// the right edge, rows, lanes, the solid act, the ink focus bar), so it is one panel among the
// status bar's, never a view of the Profile. Laid out as social game clients lay theirs out (a +
// button for adding friends, not everything in one scrollable panel):
//   plate    FRIENDS and the counts, then + ADD (the add view) and the close cell
//   you      your swatch and name, and your status as a cell (ONLINE / APPEAR OFFLINE) that opens
//            a tray: the two statuses, then FRIENDS CAN JOIN and MESSAGES as switches
//   tabs     FRIENDS (n) · REQUESTS (the incoming count) · BLOCKED
//   home     the FRIENDS tab: ONLINE and OFFLINE, a row per friend (their swatch, name, level,
//            where they are) with its quick acts, JOIN when their room may be joined and MESSAGE
//            (the unread count); the row itself opens the friend's card
//   requests to you (ACCEPT / DECLINE) and sent (CANCEL)
//   blocked  the blocked players (UNBLOCK) and the muted ones (UNMUTE)
//   add      a handle well and the solid SEND REQUEST
//   friend   one friend's card: where they are, JOIN, SPECTATE, MESSAGE, CHALLENGE TO 1V1
//            (while their page hears it; CANCEL CHALLENGE once sent) as lanes, REMOVE and BLOCK
//            (each asks once more)
// MESSAGE (the row's and the card's) opens the friend's conversation in the chat dock
// (ChatDock.tsx), beside the panel, which stays open. The tabs are the panel's top level: Esc
// closes from any of them. add and friend are views
// inside it: Esc (and the foot's ESC BACK) steps back to where they were opened from.
// A guest sees why there are no friends yet and SAVE YOUR PROGRESS (the Profile's sign-up).
// Presence is the platform's; a
// friend's swatch is their cycle colour as their page last said. The keys are App's, as for the
// Profile (Tab and Up/Down inside the sheet, Enter/Space on buttons); Left/Right move the tabs.
import { forwardRef, useCallback, useEffect, useImperativeHandle, useRef, useState, type FormEvent, type KeyboardEvent as ReactKeyboardEvent } from "react";
import * as api from "../platform/client";
import { friendsAccount, mutesOn, refreshFriends, refreshMutes, socialText, useFriends } from "../platform/friends";
import { useConversations } from "../platform/messages";
import type { FriendInfo, ListedPerson, Person, PlatformState, PresenceInfo, PrivacyInfo } from "../platform/types";
import { stripColors } from "./Colored";
import { Icon } from "./Icons";
import { Act, Cell, Field, Lanes, RowAct, Solid, ViewHead } from "./profile/bits";
import { ago, t, type PlainKey } from "../i18n";
import { Row } from "./profile/views";
import { SheetClose } from "./SheetClose";

export type FriendsView = "home" | "requests" | "blocked" | "add" | "friend";
type Tab = "home" | "requests" | "blocked";

// title: the tab's element id too (friends-tab-friends), so English; words: what it says
const TABS: Array<{ id: Tab; title: string; words: PlainKey }> = [
  { id: "home", title: "friends", words: "social.friends.tab.friends" },
  { id: "requests", title: "requests", words: "social.friends.tab.requests" },
  { id: "blocked", title: "blocked", words: "social.friends.tab.blocked" }
];

// a tab's element id: friends-tab-friends, -requests, -blocked
function tabId(tab: Tab): string {
  return "friends-tab-" + (TABS.find((x) => x.id === tab)?.title ?? tab);
}

export interface FriendsHandle {
  // Esc: true when it stepped back inside the panel, false when the panel should close
  back(): boolean;
}

interface FriendsProps {
  shown: boolean;
  open: boolean;
  platform: PlatformState;
  me: string; // the player's name (PLAYER_1, plain), for the you line
  onSignUp(): void; // a guest's SAVE YOUR PROGRESS: the Profile on its sign-up
  onClose(): void;
  // MESSAGE: the friend's conversation in the chat dock (App.tsx)
  onChat(friend: FriendInfo): void;
  // JOIN or SPECTATE a friend's room (App.tsx: Quick Play's seat by friend); absent: not offered
  onJoin?(friend: FriendInfo, spectate: boolean): void;
  // CHALLENGE TO 1V1 (App.tsx); null: not offered
  duel?: FriendDuel | null;
}

// a friend's challenge: who may be challenged, mine waiting on them, and the acts, which
// answer the sentence to show
export interface FriendDuel {
  can(friend: FriendInfo): boolean;
  sent(friend: FriendInfo): boolean;
  onChallenge(friend: FriendInfo): Promise<string>;
  onCancel(): Promise<string>;
}

// where a friend is, in a few words
export function whereText(p: PresenceInfo | null | undefined): string {
  if (!p) return "";
  switch (p.state) {
    case "playing":
      return p.room ? t("social.where.playingIn", { room: stripColors(p.room.name) }) : t("social.where.playing");
    case "spectating":
      return p.room ? t("social.where.watchingIn", { room: stripColors(p.room.name) }) : t("social.where.watching");
    case "menu":
      return t("social.where.menu");
    case "queue":
      return t("social.where.queue");
    case "local":
      return t("social.where.training");
    case "community":
      return t("social.where.community");
  }
  return p.lastSeen ? t("social.where.seen", { ago: ago(p.lastSeen) }) : t("social.where.offline");
}

// how long two have been friends ("friends for 3 days"): nothing under 90 s
function friendsFor(unixSeconds: number): string {
  const s = Math.max(0, Math.round(Date.now() / 1000 - unixSeconds));
  if (s < 90) return "";
  const m = Math.round(s / 60);
  if (m < 90) return t("social.friends.forMinutes", { n: m });
  const h = Math.round(m / 60);
  if (h < 36) return t("social.friends.forHours", { n: h });
  return t("social.friends.forDays", { n: Math.round(h / 24) });
}

export function online(f: FriendInfo): boolean {
  return !!f.presence && f.presence.state !== "offline";
}

// ideas on one line told apart by gaps, never by a glyph between them
function Parts({ parts }: { parts: string[] }) {
  const kept = parts.filter(Boolean);
  return (
    <span className="fr-parts">
      {kept.map((x, i) => (
        <span key={i}>{x}</span>
      ))}
    </span>
  );
}

// a person's line: the name, and the handle when it says something the name does not
function note(p: Person, extra?: string): React.ReactNode {
  const parts = [handleOf(p), extra ?? ""].filter(Boolean);
  return parts.length ? <Parts parts={parts} /> : undefined;
}

function handleOf(p: Person): string {
  return p.handle.toLowerCase() !== p.name.toLowerCase() && !p.isGuest ? "@" + p.handle : "";
}

function levelKey(p: Person, fallback: string): string {
  return p.level !== null ? t("social.friends.level", { level: p.level }) : fallback;
}

export const Friends = forwardRef<FriendsHandle, FriendsProps>(function Friends({ shown, open, platform, me, onSignUp, onClose, onChat, onJoin, duel = null }, ref) {
  const [view, setView] = useState<FriendsView>("home");
  // the tab a view inside the panel was opened from, where Esc goes back to
  const [tab, setTab] = useState<Tab>("home");
  const [notice, setNotice] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [handle, setHandle] = useState("");
  const [handleError, setHandleError] = useState<string | null>(null);
  // REMOVE and BLOCK ask once more: the act waiting for the second press
  const [confirm, setConfirm] = useState<string | null>(null);
  const [blockedList, setBlockedList] = useState<ListedPerson[] | null>(null);
  const [mutedList, setMutedList] = useState<ListedPerson[] | null>(null);
  const [privacy, setPrivacyState] = useState<PrivacyInfo | null>(null);
  const [statusOpen, setStatusOpen] = useState(false);
  // the friend whose card is open (their row in the list is read each render)
  const [withId, setWithId] = useState<string | null>(null);
  // unread messages per friend (the MESSAGE act's count): the conversations' store
  const conversations = useConversations();
  const bodyRef = useRef<HTMLDivElement>(null);
  const statusRef = useRef<HTMLDivElement>(null);
  const focusTick = useRef(0);
  const [tick, setTick] = useState(0);
  const list = useFriends();
  const account = friendsAccount(platform);
  const apiUrl = platform.mode === "platform" ? platform.apiUrl : null;
  const user = platform.mode === "platform" ? platform.user : null;
  const withFriend = withId && list ? (list.friends.find((f) => f.user.userId === withId) ?? null) : null;

  const go = useCallback((next: FriendsView, say: string | null = null) => {
    setView(next);
    if (next === "home" || next === "requests" || next === "blocked") setTab(next);
    setNotice(say);
    setError(null);
    setConfirm(null);
    setHandleError(null);
    setStatusOpen(false);
    focusTick.current++;
    setTick(focusTick.current);
  }, []);

  useImperativeHandle(ref, () => ({
    back() {
      if (statusOpen) {
        setStatusOpen(false);
        return true;
      }
      if (confirm) {
        setConfirm(null);
        return true;
      }
      if (view === "add" || view === "friend") {
        go(tab);
        return true;
      }
      return false;
    }
  }));

  // opening: the FRIENDS tab, the list asked again
  useEffect(() => {
    if (!shown) return;
    go("home");
    setHandle("");
    setWithId(null);
    if (account) void refreshFriends().catch((e) => setError(socialText(e)));
    if (account && apiUrl) api.getPrivacy(apiUrl).then(setPrivacyState, () => setPrivacyState(null));
    else setPrivacyState(null);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown]);

  // the BLOCKED tab: the blocked and the muted players, asked when it opens
  useEffect(() => {
    if (!shown || view !== "blocked") return;
    setBlockedList(null);
    setMutedList(null);
    if (!apiUrl) {
      setBlockedList([]);
      setMutedList([]);
      return;
    }
    api.getBlocks(apiUrl).then(setBlockedList, (e) => {
      setBlockedList([]);
      setError(socialText(e));
    });
    if (mutesOn(platform))
      api.getMutes(apiUrl).then(setMutedList, (e) => {
        setMutedList([]);
        setError(socialText(e));
      });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown, view, apiUrl]);

  // a guest's muted players (their only list here)
  useEffect(() => {
    if (!shown || account || !apiUrl || !mutesOn(platform)) return;
    api.getMutes(apiUrl).then(setMutedList, () => setMutedList([]));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shown, account, apiUrl]);

  // a card whose friend left the list (removed, blocked): back to the tab, the
  // notice that says why kept
  useEffect(() => {
    if (!(view === "friend" && list && !withFriend)) return;
    setView(tab);
    setConfirm(null);
    focusTick.current++;
    setTick(focusTick.current);
  }, [view, list, withFriend, tab]);

  // the status tray: the focus on the status in use; a press outside it closes it
  useEffect(() => {
    if (!statusOpen) return;
    statusRef.current?.querySelector<HTMLElement>(".fr-status-opt.is-on")?.focus({ preventScroll: true });
    const away = (e: PointerEvent) => {
      if (!statusRef.current?.contains(e.target as Node)) setStatusOpen(false);
    };
    window.addEventListener("pointerdown", away, true);
    return () => window.removeEventListener("pointerdown", away, true);
  }, [statusOpen]);

  const openCard = (f: FriendInfo) => {
    setWithId(f.user.userId);
    go("friend");
  };

  const openChat = (f: FriendInfo) => onChat(f);

  // the focus follows the view: its first act (data-first), else its first field or button
  useEffect(() => {
    if (!open) return;
    const body = bodyRef.current;
    const first = body?.querySelector<HTMLElement>("[data-first]:not([disabled])") ?? body?.querySelector<HTMLElement>("input, button:not([disabled])");
    first?.focus({ preventScroll: true });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [open, tick]);

  // one call, the busy state around it, the list again after it
  const act = async (run: () => Promise<unknown>, done: string | null, name?: string) => {
    if (busy) return;
    setBusy(true);
    setError(null);
    setConfirm(null);
    try {
      await run();
      setNotice(done);
      if (account) await refreshFriends();
    } catch (e) {
      setError(socialText(e, name));
    } finally {
      setBusy(false);
    }
  };

  // CHALLENGE TO 1V1, or CANCEL CHALLENGE once it is out: the sentence it answers is the notice
  const challenge = async (f: FriendInfo) => {
    if (!duel || busy) return;
    setBusy(true);
    setError(null);
    setConfirm(null);
    try {
      setNotice(await (duel.sent(f) ? duel.onCancel() : duel.onChallenge(f)));
    } finally {
      setBusy(false);
    }
  };

  // a status or a switch: at once
  const setPrivacy = (change: Partial<PrivacyInfo>, done: string) => {
    if (!privacy || !apiUrl) return;
    void act(async () => setPrivacyState(await api.setPrivacy(apiUrl, change)), done);
  };

  const ask = async (e: FormEvent) => {
    e.preventDefault();
    const typed = handle.trim().replace(/^@/, "");
    if (!typed) {
      setHandleError(t("social.friends.handleEmpty"));
      return;
    }
    if (!apiUrl || busy) return;
    setBusy(true);
    setError(null);
    setHandleError(null);
    try {
      const answer = await api.askFriend(apiUrl, { handle: typed });
      const who = (answer.friend ?? answer.request)?.user.name ?? typed;
      setNotice(answer.state === "friends" ? t("social.friends.nowFriends", { name: who }) : t("social.friends.requestSent", { name: who }));
      setHandle("");
      await refreshFriends();
    } catch (err) {
      setHandleError(socialText(err, typed));
    } finally {
      setBusy(false);
    }
  };

  // the second press of REMOVE or BLOCK, or the first (which asks)
  const sure = (key: string, run: () => void) => {
    if (confirm === key) run();
    else setConfirm(key);
  };

  const hover = (e: React.MouseEvent<HTMLElement>) => {
    const el = (e.target as HTMLElement).closest<HTMLElement>(".pf-lane, .pf-solid, .pf-row-act, .fr-open, .fr-msg-act");
    if (!el || document.activeElement === el || (el as HTMLButtonElement).disabled) return;
    const active = document.activeElement as HTMLElement | null;
    if (active && active.tagName === "INPUT") return;
    el.focus({ preventScroll: true });
  };

  // Left / Right along the tabs, as on Settings' tab row
  const tabKey = (e: ReactKeyboardEvent) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    e.stopPropagation();
    const at = TABS.findIndex((x) => x.id === tab);
    const next = TABS[(at + (e.key === "ArrowRight" ? 1 : -1) + TABS.length) % TABS.length].id;
    go(next);
    window.requestAnimationFrame(() => document.getElementById(tabId(next))?.focus({ preventScroll: true }));
  };

  const onlineCount = list ? list.friends.filter(online).length : 0;
  const counts = list
    ? <Parts parts={[t("social.friends.count", { n: list.friends.length }), onlineCount ? t("social.friends.onlineCount", { n: onlineCount }) : ""]} />
    : account
      ? t("social.friends.asking")
      : t("social.friends.guestSay");
  const tabCount = (t: Tab): number | null => (!list ? null : t === "home" ? list.friends.length : t === "requests" ? list.incoming.length : null);
  const unread = (f: FriendInfo) => conversations.find((c) => c.user.userId === f.user.userId)?.unread ?? 0;

  // ---- the views ----------------------------------------------------------------------------

  let body: React.ReactNode = null;
  if (!account) {
    body = (
      <div className="pf-view" id="friends-guest">
        <ViewHead title={t("social.friends.guestTitle")} say={t("social.friends.guestNeed")} />
        {user?.isGuest !== false && (
          <Solid id="friends-save" first onClick={onSignUp}>
            {t("social.friends.save")}
          </Solid>
        )}
        {mutesOn(platform) && mutedList && mutedList.length > 0 && (
          <section className="fr-section" aria-labelledby="friends-muted-title">
            <h3 className="fr-section-head" id="friends-muted-title">
              {t("social.friends.muted")} <span className="fr-count">{mutedList.length}</span>
            </h3>
            <dl className="pf-rows" id="muted-list">
              {mutedList.map((l) => unmuteRow(l))}
            </dl>
          </section>
        )}
      </div>
    );
  } else if (view === "home") {
    let first = true;
    const isFirst = () => {
      const was = first;
      first = false;
      return was;
    };
    body = (
      <div className="pf-view" id="friends-home">
        {list && !list.friends.length && (
          <div className="fr-empty">
            <p className="fr-empty-say">{t("social.friends.noneYet")}</p>
            <p className="pf-say">{t("social.friends.noneHow")}</p>
            <Solid id="friends-add-first" first onClick={() => go("add")}>
              {t("social.friends.addFriend")}
            </Solid>
          </div>
        )}
        {list &&
          (
            [
              ["online", list.friends.filter(online)],
              ["offline", list.friends.filter((f) => !online(f))]
            ] as const
          ).map(([section, friends]) =>
            friends.length ? (
              <section className="fr-section" aria-labelledby={"friends-" + section + "-title"} key={section}>
                <h3 className="fr-section-head" id={"friends-" + section + "-title"}>
                  {section === "online" ? t("social.friends.online") : t("social.friends.offline")} <span className="fr-count">{friends.length}</span>
                </h3>
                <ul className="fr-list" id={"friends-" + section}>
                  {friends.map((f) => (
                    <li className={"fr-friend" + (online(f) ? " is-online" : "")} id={"friend-" + f.user.userId} key={f.user.userId} data-state={f.presence?.state ?? "offline"}>
                      <button className="fr-open" id={"open-" + f.user.userId} type="button" data-first={isFirst() ? "" : undefined} aria-label={t("social.friends.openLabel", { name: f.user.name, where: whereText(f.presence) || t("social.where.offline") })} onClick={() => openCard(f)}>
                        <i className="fr-swatch" aria-hidden="true" style={f.presence?.color ? { background: f.presence.color } : undefined} />
                        <span className="fr-who">
                          <span className="fr-name">
                            {f.user.name}
                            {f.user.level !== null && <span className="fr-level">{t("social.friends.lv", { level: f.user.level })}</span>}
                          </span>
                          <span className="pf-row-note fr-where">{whereText(f.presence) || t("social.where.offline")}</span>
                        </span>
                      </button>
                      <span className="fr-quick">
                        {onJoin && f.presence?.joinable && (
                          <RowAct id={"join-" + f.user.userId} disabled={busy} onClick={() => onJoin(f, false)}>
                            {t("social.friends.join")}
                          </RowAct>
                        )}
                        <button
                          className={"fr-msg-act" + (unread(f) ? " has-new" : "")}
                          id={"message-" + f.user.userId}
                          type="button"
                          disabled={busy}
                          aria-label={unread(f) ? t("social.friends.messageNewLabel", { name: f.user.name, n: unread(f) }) : t("social.friends.messageLabel", { name: f.user.name })}
                          title={t("social.friends.messageLabel", { name: f.user.name })}
                          onClick={() => openChat(f)}
                        >
                          <Icon name="message" className="fr-msg-icon" />
                          {unread(f) > 0 && <span className="fr-msg-count">{unread(f)}</span>}
                        </button>
                      </span>
                    </li>
                  ))}
                </ul>
              </section>
            ) : null
          )}
      </div>
    );
  } else if (view === "requests") {
    const none = !list || (!list.incoming.length && !list.outgoing.length);
    body = (
      <div className="pf-view" id="friends-requests-view">
        {list && list.incoming.length > 0 && (
          <section className="fr-section" aria-labelledby="friends-requests">
            <h3 className="fr-section-head" id="friends-requests">
              {t("social.friends.toYou")} <span className="fr-count">{list.incoming.length}</span>
            </h3>
            <dl className="pf-rows" id="friends-incoming">
              {list.incoming.map((r, i) => (
                <Row key={r.id} id={"incoming-" + r.id} keyText={levelKey(r.user, t("social.friends.request"))} value={r.user.name} note={note(r.user, ago(r.createdAt))}>
                  <span className="fr-acts">
                    <RowAct id={"accept-" + r.id} first={i === 0} disabled={busy} onClick={() => void act(() => api.acceptFriend(apiUrl!, r.id), t("social.friends.nowFriends", { name: r.user.name }), r.user.name)}>
                      {t("social.friends.accept")}
                    </RowAct>
                    <RowAct id={"decline-" + r.id} disabled={busy} onClick={() => void act(() => api.dropFriendRequest(apiUrl!, r.id), t("social.friends.declined"), r.user.name)}>
                      {t("social.friends.decline")}
                    </RowAct>
                  </span>
                </Row>
              ))}
            </dl>
          </section>
        )}
        {list && list.outgoing.length > 0 && (
          <section className="fr-section" aria-labelledby="friends-sent">
            <h3 className="fr-section-head" id="friends-sent">
              {t("social.friends.sent")} <span className="fr-count">{list.outgoing.length}</span>
            </h3>
            <dl className="pf-rows" id="friends-outgoing">
              {list.outgoing.map((r, i) => (
                <Row key={r.id} id={"outgoing-" + r.id} keyText={levelKey(r.user, t("social.friends.sentKey"))} value={r.user.name} note={note(r.user, ago(r.createdAt))}>
                  <RowAct id={"cancel-" + r.id} first={!list.incoming.length && i === 0} disabled={busy} onClick={() => void act(() => api.dropFriendRequest(apiUrl!, r.id), t("social.friends.cancelled"))}>
                    {t("social.friends.cancel")}
                  </RowAct>
                </Row>
              ))}
            </dl>
          </section>
        )}
        {none && (
          <div className="fr-empty">
            <p className="fr-empty-say">{t("social.friends.noRequests")}</p>
            <p className="pf-say">{t("social.friends.requestsSay")}</p>
            <Solid id="friends-add-requests" first onClick={() => go("add")}>
              {t("social.friends.addFriend")}
            </Solid>
          </div>
        )}
      </div>
    );
  } else if (view === "blocked") {
    const muting = mutesOn(platform);
    body = (
      <div className="pf-view" id="friends-blocked">
        <section className="fr-section" aria-labelledby="friends-blocked-title">
          <h3 className="fr-section-head" id="friends-blocked-title">
            {t("social.friends.blocked")} {blockedList && <span className="fr-count">{blockedList.length}</span>}
          </h3>
          <p className="pf-say fr-section-say">{t("social.friends.blockedSay")}</p>
          {blockedList === null ? (
            <p className="pf-say">{t("social.friends.asking")}</p>
          ) : (
            <dl className="pf-rows" id="blocked-list">
              {blockedList.map((l, i) => (
                <Row key={l.user.userId} id={"blocked-" + l.user.userId} keyText={t("social.friends.blockedKey")} value={l.user.name} note={note(l.user, ago(l.createdAt))}>
                  <RowAct
                    id={"unblock-" + l.user.userId}
                    first={i === 0}
                    disabled={busy}
                    onClick={() =>
                      void act(async () => {
                        await api.setBlocked(apiUrl!, l.user.userId, false);
                        setBlockedList((all) => (all ?? []).filter((x) => x.user.userId !== l.user.userId));
                      }, t("social.friends.unblocked", { name: l.user.name }))
                    }
                  >
                    {t("social.friends.unblock")}
                  </RowAct>
                </Row>
              ))}
              {!blockedList.length && <p className="fr-none">{t("social.friends.nobody")}</p>}
            </dl>
          )}
        </section>
        {muting && (
          <section className="fr-section" aria-labelledby="friends-muted-title">
            <h3 className="fr-section-head" id="friends-muted-title">
              {t("social.friends.muted")} {mutedList && <span className="fr-count">{mutedList.length}</span>}
            </h3>
            <p className="pf-say fr-section-say">{t("social.friends.mutedSay")}</p>
            {mutedList === null ? (
              <p className="pf-say">{t("social.friends.asking")}</p>
            ) : (
              <dl className="pf-rows" id="muted-list">
                {mutedList.map((l) => unmuteRow(l))}
                {!mutedList.length && <p className="fr-none">{t("social.friends.noMutes")}</p>}
              </dl>
            )}
          </section>
        )}
      </div>
    );
  } else if (view === "add") {
    body = (
      <form className="pf-view fr-add" id="friends-add" onSubmit={(e) => void ask(e)} noValidate>
        <ViewHead title={t("social.friends.addFriend")} say={t("social.friends.addSay")} />
        <Field id="friend-handle" label={t("social.friends.username")} value={handle} onChange={setHandle} maxLength={128} error={handleError} first />
        <Solid id="friend-send" submit disabled={busy}>
          {t("social.friends.sendRequest")}
        </Solid>
      </form>
    );
  } else if (view === "friend" && withFriend) {
    const f = withFriend;
    const said = whereText(f.presence) || t("social.where.offline");
    const where = t("social.friends.whereSentence", { where: said.charAt(0).toUpperCase() + said.slice(1) });
    const n = unread(f);
    body = (
      <div className="pf-view fr-card" id="friends-card" data-with={f.user.userId}>
        <div className="fr-card-head">
          <i className="fr-swatch fr-card-swatch" aria-hidden="true" style={f.presence?.color ? { background: f.presence.color } : undefined} />
          <div className="fr-card-who">
            <h3 className="fr-card-name">{f.user.name}</h3>
            <p className="pf-row-note"><Parts parts={[handleOf(f.user), f.user.level !== null ? t("social.friends.level", { level: f.user.level }) : "", friendsFor(f.since)]} /></p>
          </div>
        </div>
        <p className={"fr-card-where" + (online(f) ? " is-online" : "")}>{where}</p>
        <Lanes>
          {onJoin && f.presence?.joinable && (
            <Act id={"card-join-" + f.user.userId} first disabled={busy} onClick={() => onJoin(f, false)}>
              {t("social.friends.join")}
            </Act>
          )}
          {onJoin && f.presence?.joinable && (
            <Act id={"spectate-" + f.user.userId} disabled={busy} onClick={() => onJoin(f, true)}>
              {t("social.friends.spectate")}
            </Act>
          )}
          <Act id={"card-message-" + f.user.userId} first={!(onJoin && f.presence?.joinable)} disabled={busy} onClick={() => openChat(f)}>
            {t("social.friends.message")}{n > 0 && <span className="pf-lane-note">{t("social.friends.newCount", { n })}</span>}
          </Act>
          {duel && duel.can(f) && (
            <Act id={"card-duel-" + f.user.userId} disabled={busy} onClick={() => void challenge(f)}>
              {duel.sent(f) ? t("game.duel.cancelChallenge") : t("game.duel.challenge")}
            </Act>
          )}
        </Lanes>
        <div className="fr-card-acts">
          <RowAct id={"remove-" + f.user.userId} disabled={busy} onClick={() => sure("remove-" + f.user.userId, () => void act(() => api.removeFriend(apiUrl!, f.user.userId), t("social.friends.removed", { name: f.user.name })))}>
            {confirm === "remove-" + f.user.userId ? t("social.friends.sureRemove") : t("social.friends.remove")}
          </RowAct>
          <RowAct id={"block-" + f.user.userId} disabled={busy} onClick={() => sure("block-" + f.user.userId, () => void act(() => api.setBlocked(apiUrl!, f.user.userId, true), t("social.friends.blockedNow", { name: f.user.name })))}>
            {confirm === "block-" + f.user.userId ? t("social.friends.sureBlock") : t("social.friends.block")}
          </RowAct>
        </div>
      </div>
    );
  }

  function unmuteRow(l: ListedPerson) {
    return (
      <Row key={l.user.userId} id={"muted-" + l.user.userId} keyText={t("social.friends.mutedKey")} value={l.user.name} note={note(l.user, ago(l.createdAt))}>
        <RowAct
          id={"unmute-" + l.user.userId}
          disabled={busy}
          onClick={() =>
            void act(async () => {
              await api.setMuted(apiUrl!, l.user.userId, false);
              setMutedList((all) => (all ?? []).filter((x) => x.user.userId !== l.user.userId));
              await refreshMutes();
            }, t("social.friends.unmuted", { name: l.user.name }))
          }
        >
          {t("social.friends.unmute")}
        </RowAct>
      </Row>
    );
  }

  // ---- the sheet ----------------------------------------------------------------------------

  const inside = view === "add" || view === "friend";
  const appearOffline = !!privacy?.appearOffline;

  return (
    <section
      className={"pf fr" + (open ? " is-open" : "")}
      id="friends"
      role="dialog"
      aria-modal="true"
      aria-labelledby="friends-title"
      data-view={view}
      data-busy={busy ? "1" : "0"}
      hidden={!shown}
      onMouseOver={hover}
    >
      <header className="pf-plate fr-plate">
        <div className="pf-who">
          <h2 className="fr-title" id="friends-title">
            {t("social.friends.title")}
          </h2>
          <p className="pf-who-say" id="friends-say">
            {counts}
          </p>
        </div>
        {account && (
          <button className={"sheet-x fr-add-cell" + (view === "add" ? " is-on" : "")} id="friends-add-open" type="button" tabIndex={-1} aria-pressed={view === "add"} title={t("social.friends.addTitle")} onClick={() => (view === "add" ? go(tab) : go("add"))}>
            <Icon name="plus" className="sheet-x-icon" />
            {t("social.friends.add")}
          </button>
        )}
        <SheetClose label={t("social.friends.title")} onClose={onClose} />
      </header>

      {account && (
        <div className="fr-you" id="friends-you">
          <i className="fr-swatch fr-you-swatch" aria-hidden="true" />
          <span className="fr-you-name">{me}</span>
          {privacy && (
            <div className="fr-status-wrap" ref={statusRef}>
              <button
                className={"fr-status" + (statusOpen ? " is-open" : "") + (appearOffline ? " is-away" : "")}
                id="friends-status"
                type="button"
                aria-haspopup="true"
                aria-expanded={statusOpen}
                aria-controls="friends-status-tray"
                onClick={() => setStatusOpen(!statusOpen)}
              >
                <i className="fr-dot" aria-hidden="true" />
                {appearOffline ? t("social.friends.appearOffline") : t("social.friends.online")}
                <Icon name="chevron" className="fr-status-chev" />
              </button>
              {statusOpen && (
                <div className="fr-status-tray" id="friends-status-tray" role="group" aria-label={t("social.friends.yourStatus")}>
                  <div role="radiogroup" aria-label={t("social.friends.status")}>
                    <button
                      className={"fr-status-opt" + (!appearOffline ? " is-on" : "")}
                      id="status-online"
                      type="button"
                      role="radio"
                      aria-checked={!appearOffline}
                      onClick={() => {
                        if (appearOffline) setPrivacy({ appearOffline: false }, t("social.friends.onlineAgain"));
                        setStatusOpen(false);
                      }}
                    >
                      <i className="fr-dot" aria-hidden="true" />
                      <span className="fr-status-opt-text">
                        {t("social.friends.online")}
                        <span className="fr-status-opt-say">{t("social.friends.onlineSay")}</span>
                      </span>
                    </button>
                    <button
                      className={"fr-status-opt is-away" + (appearOffline ? " is-on" : "")}
                      id="status-offline"
                      type="button"
                      role="radio"
                      aria-checked={appearOffline}
                      onClick={() => {
                        if (!appearOffline) setPrivacy({ appearOffline: true }, t("social.friends.appearOfflineNow"));
                        setStatusOpen(false);
                      }}
                    >
                      <i className="fr-dot" aria-hidden="true" />
                      <span className="fr-status-opt-text">
                        {t("social.friends.appearOffline")}
                        <span className="fr-status-opt-say">{t("social.friends.appearOfflineSay")}</span>
                      </span>
                    </button>
                  </div>
                  <div className="fr-status-switches">
                    <StatusSwitch
                      id="privacy-join"
                      label={t("social.friends.canJoin")}
                      say={privacy.friendsCanJoin ? t("social.friends.canJoinOn") : t("social.friends.canJoinOff")}
                      on={privacy.friendsCanJoin}
                      disabled={busy}
                      onChange={(on) => setPrivacy({ friendsCanJoin: on }, on ? t("social.friends.canJoinNow") : t("social.friends.canJoinNoMore"))}
                    />
                    <StatusSwitch
                      id="privacy-messages"
                      label={t("social.friends.messages")}
                      say={privacy.friendsCanMessage !== false ? t("social.friends.messagesOn") : t("social.friends.messagesOff")}
                      on={privacy.friendsCanMessage !== false}
                      disabled={busy}
                      onChange={(on) => setPrivacy({ friendsCanMessage: on }, on ? t("social.friends.messagesOn") : t("social.friends.messagesOffNow"))}
                    />
                  </div>
                </div>
              )}
            </div>
          )}
        </div>
      )}

      {account && !inside && (
        <div className="fr-tabs" role="tablist" aria-label={t("social.friends.title")} onKeyDown={tabKey}>
          {TABS.map((tb) => {
            const n = tabCount(tb.id);
            return (
              <button
                key={tb.id}
                className={"fr-tab" + (tb.id === tab ? " is-on" : "")}
                id={tabId(tb.id)}
                type="button"
                role="tab"
                aria-selected={tb.id === tab}
                tabIndex={tb.id === tab ? 0 : -1}
                onClick={() => go(tb.id)}
              >
                {t(tb.words)}
                {n !== null && n > 0 && <span className={"fr-tab-n" + (tb.id === "requests" ? " is-ask" : "")}>{n}</span>}
              </button>
            );
          })}
        </div>
      )}

      <div className="pf-body" ref={bodyRef}>
        <p className="pf-notice" id="friends-notice" role="status" hidden={!notice}>
          {notice || ""}
        </p>
        <Cell text={error} id="friends-error" />
        <div className="pf-swap" key={view + (withId ?? "")}>
          {body}
        </div>
      </div>

      {(inside || confirm) && (
        <button className="pf-foot" type="button" tabIndex={-1} onClick={() => (confirm ? setConfirm(null) : go(tab))}>
          <span className="pf-foot-key">{t("common.esc")}</span> {t("common.back")}
        </button>
      )}
    </section>
  );
});

// a switch in the status tray: the key and its sentence, OFF ON at the right (Settings' switch)
function StatusSwitch({ id, label, say, on, disabled, onChange }: { id: string; label: string; say: string; on: boolean; disabled: boolean; onChange(on: boolean): void }) {
  const key = (e: ReactKeyboardEvent) => {
    if (e.key !== "ArrowLeft" && e.key !== "ArrowRight") return;
    e.preventDefault();
    e.stopPropagation();
    const next = e.key === "ArrowRight";
    if (next !== on && !disabled) onChange(next);
  };
  return (
    <button className={"fr-switch" + (on ? " is-on" : "")} id={id} type="button" role="switch" aria-checked={on} disabled={disabled} onKeyDown={key} onClick={() => onChange(!on)}>
      <span className="fr-switch-text">
        <span className="fr-switch-key">{label}</span>
        <span className="fr-switch-say">{say}</span>
      </span>
      <span className="fr-switch-val" aria-hidden="true">
        <span className="fr-switch-no">{t("common.off")}</span>
        <span className="fr-switch-yes">{t("common.on")}</span>
      </span>
    </button>
  );
}
