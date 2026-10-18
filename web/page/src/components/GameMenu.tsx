// The in-game menu (the game's INGAME_MENU key, Escape by default, raises `menuOpen`): one view
// at a time over the game, which dims behind it and goes on.
//   home     the actions (RESUME, INVITE, SETTINGS, DISCONNECT) and the players in the room;
//            a challenge to a 1v1 heads the actions (ACCEPT, DECLINE)
//   card     one player: team, score, ping, and where the platform offers them ADD FRIEND,
//            CHALLENGE TO 1V1, MUTE and REPORT
//   report   the reasons, an optional note, SEND REPORT
//   friends  the account's friends online, with JOIN and SPECTATE
// The menu is marked data-aa-keys="page", so while the focus is inside it the client leaves
// every key to the page (web/wasm/API.md "Setup"); Escape steps back (report -> card -> home ->
// closed), and closing gives the keys back to the game.
import { forwardRef, useEffect, useImperativeHandle, useRef, useState, type KeyboardEvent as ReactKeyboardEvent } from "react";
import type { AaPlayer } from "../aa/types";
import type { FriendInfo, FriendsList, RoomPlayerInfo } from "../platform/types";
import { online, whereText } from "./Friends";
import { isTextField, moveFocus } from "../keyboard";
import { NOTE_MAX, REASONS, type ReasonId, type ReportResult, type ReportTarget } from "../platform/reports";
import { EMBED } from "../links";
import { SheetClose } from "./SheetClose";
import { useSecondsLeft } from "./Duel";
import { Icon } from "./Icons";
import { track } from "../platform/analytics";
import { touchPrimary } from "../compact";
import { t } from "../i18n";

type View = { kind: "home" } | { kind: "card"; id: number } | { kind: "report"; id: number } | { kind: "friends" };

// the account's friends in the game (App.tsx): the list, asked again when the view opens
export interface MenuFriends {
  list: FriendsList | null;
  refresh(): void;
  onJoin(friend: FriendInfo, spectate: boolean): void;
}

// what the platform knows of the room's players, and the card's two social acts (App.tsx)
export interface CardSocial {
  friends: boolean; // features.friends in an official room: the card offers ADD FRIEND
  who(id: number): RoomPlayerInfo | undefined; // the verified player behind a network id
  muted(p: AaPlayer): boolean;
  onFriend(p: AaPlayer): Promise<string>; // the line to show
  onMute(p: AaPlayer, on: boolean): Promise<string>;
  // duels: null where none can be had (no feature, a private room, not an official room)
  duel: {
    can(p: AaPlayer): boolean; // another browser player the room verified
    sent(p: AaPlayer): boolean; // my challenge to them waits for an answer
    blocked(p: AaPlayer): boolean; // I blocked their challenges (neither way while it stands)
    onChallenge(p: AaPlayer): Promise<string>;
    onCancel(): Promise<string>;
    onBlock(p: AaPlayer, on: boolean): Promise<string>;
  } | null;
}

// a challenge to me, while it waits for an answer
export interface MenuDuel {
  // incoming: a challenge to me (ACCEPT / DECLINE, and the ways to stop more); arrived: my invite
  // link was opened (GO TO 1V1 / NOT NOW)
  kind?: "incoming" | "arrived";
  from: string;
  color?: string; // their swatch, when the room has them
  until: number; // when it ends unanswered (ms)
  busy: boolean;
  onAccept(): void;
  onDecline(): void;
  onBlock(): Promise<string>; // no more challenges from them (this one declined)
  onOff(): Promise<string>; // no challenges from anyone (this one declined)
}

// a server that is not ours asks to send the player to another host: MOVE or STAY
export interface MenuMoveAsk {
  name: string; // the server the game runs on
  address: string; // where it sends the player
  onMove(): void;
  onStay(): void;
}

export interface GameMenuHandle {
  // the menu just opened: home, the focus on its first action
  reset(): void;
}

interface GameMenuProps {
  open: boolean;
  local: boolean;
  serverName: string;
  players: AaPlayer[];
  canReport: boolean; // an official room on a platform that takes reports
  level: number | null; // the platform's level of the local player, if it has one
  invite: string | null; // the link into this room (?join=), null where there is none (local, no server id)
  // inside a Discord Activity: INVITE opens Discord's own invite instead of copying the link
  onInvite?: (() => void) | null;
  onLeave(): void;
  // the embed's way to the whole page (links.ts fullPageLink), null elsewhere; onFullGame runs
  // as the link opens its tab
  fullGame?: string | null;
  onFullGame?(): void;
  onClose(): void;
  // the Settings sheet in the menu's place
  onSettings(): void;
  onReport(target: ReportTarget, reason: ReasonId, note: string): Promise<ReportResult>;
  social: CardSocial;
  friends: MenuFriends | null; // an account on a platform with friends
  duel: MenuDuel | null;
  moveAsk?: MenuMoveAsk | null; // a server's move elsewhere, waiting for the player's answer
}

function teamText(p: AaPlayer): string {
  if (p.spectating) return t("game.card.spectating");
  return p.team ?? "—";
}

// the game's 0-15 colour as the swatch's
export function swatch(p: AaPlayer): string {
  const [r, g, b] = p.color.map((c) => Math.round((Math.max(0, Math.min(15, c)) * 255) / 15));
  return `rgb(${r}, ${g}, ${b})`;
}

export const GameMenu = forwardRef<GameMenuHandle, GameMenuProps>(function GameMenu(
  { open, local, serverName, players, canReport, level, invite, onInvite, onLeave, fullGame, onFullGame, onClose, onSettings, onReport, social, friends, duel, moveAsk },
  ref
) {
  const [view, setView] = useState<View>({ kind: "home" });
  // a player who left keeps the card readable: the last copy we saw
  const [lastSeen, setLastSeen] = useState<Record<number, AaPlayer>>({});
  const [reason, setReason] = useState<ReasonId | null>(null);
  const [note, setNote] = useState("");
  const [sending, setSending] = useState(false);
  const [failure, setFailure] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  // the invite link's answer: copied, or the link itself to copy by hand
  const [copied, setCopied] = useState<"copied" | "shown" | null>(null);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const noteRef = useRef<HTMLTextAreaElement>(null);
  const focusNext = useRef<string | null>(null);

  useImperativeHandle(ref, () => ({
    reset() {
      setView({ kind: "home" });
      setNotice(null);
      setFailure(null);
      setCopied(null);
      focusNext.current = "#menu-resume";
    }
  }));

  useEffect(() => {
    if (!players.length) return;
    setLastSeen((seen) => {
      const next = { ...seen };
      for (const p of players) next[p.id] = p;
      return next;
    });
  }, [players]);

  // after a view change: the element asked for, else the view's first focusable
  useEffect(() => {
    if (!open) return;
    const root = rootRef.current;
    if (!root) return;
    const want = focusNext.current;
    focusNext.current = null;
    const el = (want && root.querySelector<HTMLElement>(want)) || root.querySelector<HTMLElement>("[data-first]");
    el?.focus({ preventScroll: true });
    if (el) el.scrollIntoView?.({ block: "nearest" });
  }, [open, view]);

  // the note grows with its text (one line empty, up to the limit), never scrolls
  useEffect(() => {
    const el = noteRef.current;
    if (!el) return;
    el.style.height = "auto";
    el.style.height = el.scrollHeight + "px";
  }, [note, view, open]);

  const go = (next: View, focus?: string) => {
    focusNext.current = focus ?? null;
    setFailure(null);
    if (next.kind === "home") setNotice(null);
    setView(next);
  };

  const copyInvite = () => {
    if (onInvite) return onInvite();
    if (!invite) return;
    const shown = () => setCopied("shown");
    const copy = (counted = false) => {
      if (!counted) track("link_copied", { what: "invite" });
      try {
        navigator.clipboard.writeText(invite).then(() => setCopied("copied"), shown);
      } catch {
        shown();
      }
    };
    // a phone or tablet: the system's share sheet (a message, a chat app), the copy where it has
    // none or it fails; put away (AbortError), nothing more
    if (touchPrimary() && typeof navigator.share === "function") {
      track("link_copied", { what: "invite", how: "share" });
      navigator.share({ title: "Retrocycles", text: t("game.menu.shareText"), url: invite }).catch((e: unknown) => {
        if (!(e instanceof DOMException && e.name === "AbortError")) copy(true);
      });
      return;
    }
    copy();
  };

  // the friend chosen in the friends view (its JOIN and SPECTATE showing)
  const [chosenFriend, setChosenFriend] = useState<string | null>(null);

  const back = () => {
    if (view.kind === "friends" && chosenFriend) {
      focusNext.current = "#mf-" + chosenFriend;
      setChosenFriend(null);
      setView({ ...view });
    } else if (view.kind === "friends") go({ kind: "home" }, "#menu-friends");
    else if (view.kind === "report") go({ kind: "card", id: view.id }, "#card-report");
    else if (view.kind === "card") go({ kind: "home" }, "#player-" + view.id);
    else onClose();
  };

  const openReport = (id: number) => {
    setReason(null);
    setNote("");
    setNotice(null);
    go({ kind: "report", id });
  };

  // a stable order for the arrows (the game's own order changes between rounds): the
  // people by name, then the bots
  const sorted = players.slice().sort((a, b) => Number(!!a.bot) - Number(!!b.bot) || a.name.localeCompare(b.name, undefined, { sensitivity: "base" }));
  const current = view.kind === "home" || view.kind === "friends" ? null : players.find((p) => p.id === view.id) ?? lastSeen[view.id] ?? null;
  const present = current ? players.some((p) => p.id === current.id) : false;
  const reportable = !!current && canReport && !current.local && !current.bot;
  // another person in the room: the card's social acts
  const person = !!current && present && !current.local && !current.bot;
  const known = current ? social.who(current.id) : undefined;
  const friendable = person && social.friends && (!known || !known.user.isGuest);
  const duelable = person && !!social.duel && social.duel.can(current);
  const duelBlocked = duelable && !!social.duel && social.duel.blocked(current);
  const duelSent = duelable && !duelBlocked && !!social.duel && social.duel.sent(current);
  const [socialBusy, setSocialBusy] = useState(false);
  const duelLeft = useSecondsLeft(duel && open ? duel.until : null);

  const runSocial = async (run: () => Promise<string>) => {
    if (socialBusy) return;
    setSocialBusy(true);
    try {
      setNotice(await run());
    } finally {
      setSocialBusy(false);
    }
  };

  const send = async () => {
    if (!current || sending) return;
    if (!reason) {
      setFailure(t("game.report.choose"));
      focusNext.current = "#reason-" + REASONS[0].id;
      setView({ ...view });
      return;
    }
    setSending(true);
    setFailure(null);
    try {
      const result = await onReport({ name: current.name }, reason, note);
      if (result.ok) {
        setNotice(result.text);
        go({ kind: "card", id: current.id }, "#card-back");
      } else {
        setFailure(result.text);
      }
    } finally {
      setSending(false);
    }
  };

  // keys inside the menu (the client leaves them to the page here)
  const onKeyDown = (e: ReactKeyboardEvent<HTMLDivElement>) => {
    const root = rootRef.current;
    const target = e.target as HTMLElement;
    const field = isTextField(target);
    if (e.key === "Escape") {
      e.preventDefault();
      back();
    } else if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      moveFocus(e.key === "ArrowUp", root);
    } else if (e.key === "Tab") {
      e.preventDefault();
      moveFocus(e.shiftKey, root);
    } else if (e.key === "Enter" && field) {
      e.preventDefault();
      if (view.kind === "report") void send();
    } else if ((e.key === "Enter" || e.key === " ") && !field) {
      if (target.tagName === "BUTTON") {
        e.preventDefault();
        if (!(target as HTMLButtonElement).disabled) target.click();
      } else if (target.tagName === "A" && e.key === "Enter") {
        e.preventDefault();
        target.click();
      }
    }
  };

  // hovering a lane gives it the focus (its solid ground and ink bar), as the arrows do
  const hover = (e: React.MouseEvent<HTMLElement>) => {
    const el = (e.target as HTMLElement).closest<HTMLElement>(".lane");
    if (el && document.activeElement !== el) el.focus({ preventScroll: true });
  };

  return (
    <div
      className="gm"
      id="game-menu"
      role="dialog"
      aria-label={t("game.menu.label")}
      data-aa-keys="page"
      data-view={view.kind}
      hidden={!open}
      ref={rootRef}
      onKeyDown={onKeyDown}
      onMouseOver={hover}
    >
      {/* the way out where the eye lands, as the sheets have it;
          a press off the menu resumes too (App.tsx). After the panels in a row, above them in
          a column (first-minute.css) */}
      <SheetClose label={t("game.menu.name")} onClose={onClose} />
      {view.kind === "home" && (
        <>
          <section className="gm-panel gm-actions" aria-label={local ? t("game.menu.actionsTraining") : t("game.menu.actions")}>
            {duel && (
              <div className="gm-duel" id="menu-duel">
                <p className="gm-duel-say">
                  {duel.color && <span className="duel-swatch" style={{ background: duel.color }} aria-hidden="true" />}
                  <span>{duel.kind === "arrived" ? t("game.duel.arrived", { name: duel.from }) : t("game.duel.incoming", { name: duel.from })}</span>
                </p>
                <p className="gm-say">
                  {duel.kind === "arrived"
                    ? t("game.duel.goLeaves", { seconds: duelLeft ?? 0 })
                    : duelLeft !== null
                      ? t("game.duel.leavesIn", { seconds: duelLeft })
                      : t("game.duel.leaves")}
                </p>
                <button className="lane" type="button" id="menu-duel-accept" disabled={duel.busy} onClick={duel.onAccept}>
                  {duel.kind === "arrived" ? t("game.duel.go") : t("game.duel.accept")}
                </button>
                <button className="lane lane-small" type="button" id="menu-duel-decline" disabled={duel.busy} onClick={duel.onDecline}>
                  {duel.kind === "arrived" ? t("game.duel.notNow") : t("game.duel.decline")}
                </button>
                {duel.kind !== "arrived" && <div className="gm-duel-more">
                  <button className="gm-duel-act" type="button" id="menu-duel-block" disabled={duel.busy || socialBusy} onClick={() => void runSocial(duel.onBlock)}>
                    {t("game.duel.blockFrom", { name: duel.from })}
                  </button>
                  <button className="gm-duel-act" type="button" id="menu-duel-off" disabled={duel.busy || socialBusy} onClick={() => void runSocial(duel.onOff)}>
                    {t("game.duel.turnOff")}
                  </button>
                </div>}
              </div>
            )}
            {!duel && moveAsk && (
              // a server that is not ours wants to send the player to another host
              <div className="gm-duel" id="menu-move-ask">
                <p className="gm-duel-say">
                  <span>{t("game.moveAsk.say", { name: moveAsk.name, address: moveAsk.address })}</span>
                </p>
                <p className="gm-say">{t("game.moveAsk.foot")}</p>
                <button className="lane" type="button" id="menu-move-ask-move" onClick={moveAsk.onMove}>
                  {t("game.moveAsk.move")}
                </button>
                <button className="lane lane-small" type="button" id="menu-move-ask-stay" onClick={moveAsk.onStay}>
                  {t("game.moveAsk.stay")}
                </button>
              </div>
            )}
            {!duel && notice && (
              <p className="gm-notice gm-duel-notice" id="menu-duel-notice" role="status">
                {notice}
              </p>
            )}
            {/* RESUME keeps the first focus with a challenge up too: Esc then Enter, the "pause, resume" habit, must never
                accept a duel and leave the room; ACCEPT is the arrows away */}
            <button className="lane" type="button" id="menu-resume" data-first="" onClick={onClose}>
              {t("game.menu.resume")}
            </button>
            {fullGame && (
              <a className="lane lane-out" id="menu-full-game" href={fullGame} target="_blank" rel="noopener" onClick={onFullGame}>
                {t("game.menu.fullGame")}
                <Icon name="external" className="lane-out-icon" />
              </a>
            )}
            {invite && (
              <button className="lane" type="button" id="invite" onClick={copyInvite}>
                {t("game.menu.invite")}
              </button>
            )}
            {friends && (
              <button
                className="lane"
                type="button"
                id="menu-friends"
                onClick={() => {
                  friends.refresh();
                  setChosenFriend(null);
                  go({ kind: "friends" });
                }}
              >
                {t("game.menu.friends")}
              </button>
            )}
            <button className="lane" type="button" id="menu-settings" onClick={onSettings}>
              {t("game.menu.settings")}
            </button>
            <button className="lane" type="button" id="leave-game" onClick={onLeave}>
              {t("game.menu.leave")}
            </button>
            {invite && copied && (
              <p className="gm-notice gm-invite" id="invite-notice" role="status">
                {copied === "copied" ? t("game.menu.inviteCopied") : t("game.menu.inviteShown")}
                <span className="invite-link">{invite}</span>
              </p>
            )}
          </section>

          {players.length > 0 && !EMBED && (
            <section className="gm-panel gm-players" aria-labelledby="players-key">
              <header className="gm-head">
                <h2 className="gm-head-title" id="players-key">
                  {t("game.players.title")} <span className="gm-count">({players.length})</span>
                </h2>
                <p className="gm-head-room">{local ? t("game.players.training") : serverName}</p>
              </header>
              <ul className="players">
                {sorted.map((p) => (
                  <li key={p.id}>
                    <button
                      className="lane prow"
                      type="button"
                      id={"player-" + p.id}
                      data-bot={p.bot ? "1" : undefined}
                      data-local={p.local ? "1" : undefined}
                      onClick={() => go({ kind: "card", id: p.id })}
                    >
                      <i className="prow-swatch" style={{ background: swatch(p) }} />
                      <span className="prow-who">
                        <span className="prow-name">{p.name}</span>
                        <span className="prow-mark">{p.local ? t("game.players.you") : p.bot ? t("game.players.bot") : ""}</span>
                      </span>
                      <span className="prow-score">{p.score}</span>
                    </button>
                  </li>
                ))}
              </ul>
            </section>
          )}
        </>
      )}

      {view.kind === "friends" && friends && (
        <section className="gm-panel gm-card gm-friends" aria-labelledby="mf-title">
          <h2 className="gm-name" id="mf-title">
            {t("game.friends.title")}
          </h2>
          {!friends.list ? (
            <p className="gm-sub">{t("game.friends.asking")}</p>
          ) : !friends.list.friends.some(online) ? (
            <p className="gm-sub">{friends.list.friends.length ? t("game.friends.noneOnline") : t("game.friends.noFriends")}</p>
          ) : (
            <ul className="players" id="mf-list">
              {friends.list.friends.filter(online).map((f, i) => (
                <li key={f.user.userId}>
                  <button
                    className="lane prow mf-row"
                    type="button"
                    id={"mf-" + f.user.userId}
                    data-first={i === 0 ? "" : undefined}
                    data-state={f.presence?.state}
                    aria-expanded={chosenFriend === f.user.userId}
                    disabled={!f.presence?.joinable}
                    onClick={() => {
                      focusNext.current = "#mf-join-" + f.user.userId;
                      setChosenFriend((c) => (c === f.user.userId ? null : f.user.userId));
                      setView({ ...view });
                    }}
                  >
                    <i className="prow-swatch" style={f.presence?.color ? { background: f.presence.color } : undefined} />
                    <span className="prow-who">
                      <span className="prow-name">{f.user.name}</span>
                      <span className="prow-mark">{whereText(f.presence)}</span>
                    </span>
                    <span className="prow-score" />
                  </button>
                  {chosenFriend === f.user.userId && f.presence?.joinable && (
                    <div className="mf-acts">
                      <p className="gm-say">{local ? t("game.friends.endsTraining") : t("game.friends.leavesRoom")}</p>
                      <button className="lane lane-small" type="button" id={"mf-join-" + f.user.userId} onClick={() => friends.onJoin(f, false)}>
                        {t("game.friends.join", { name: f.user.name })}
                      </button>
                      <button className="lane lane-small" type="button" id={"mf-spectate-" + f.user.userId} onClick={() => friends.onJoin(f, true)}>
                        {t("game.friends.spectate")}
                      </button>
                    </div>
                  )}
                </li>
              ))}
            </ul>
          )}
          <button className="lane lane-small" type="button" id="mf-back" data-first={friends.list?.friends.some(online) ? undefined : ""} onClick={back}>
            {t("common.back")}
          </button>
        </section>
      )}

      {view.kind === "card" && current && (
        <section className="gm-panel gm-card" aria-labelledby="card-name">
          {notice && (
            <p className="gm-notice" id="menu-notice" role="status">
              {notice}
            </p>
          )}
          <header className="gm-who">
            <i className="gm-who-swatch" style={{ background: swatch(current) }} />
            <div>
              <h2 className="gm-name" id="card-name">
                {current.name}
              </h2>
              <p className="gm-sub">
                {!present ? t("game.card.left") : current.local ? t("game.card.you") : current.bot ? t("game.card.bot") : t("game.card.player")}
              </p>
            </div>
          </header>
          <dl className="gm-facts card-facts">
            <div className="fact">
              <dt className="fact-key">{t("game.card.team")}</dt>
              <dd className="fact-val">{teamText(current)}</dd>
            </div>
            <div className="fact">
              <dt className="fact-key">{t("game.card.score")}</dt>
              <dd className="fact-val">{current.score}</dd>
            </div>
            {!current.bot && (
              <div className="fact">
                <dt className="fact-key">{t("game.card.ping")}</dt>
                <dd className="fact-val">{current.ping !== null && current.ping > 0 ? t("game.card.pingMs", { ms: current.ping }) : "—"}</dd>
              </div>
            )}
            {current.local && level !== null && (
              <div className="fact">
                <dt className="fact-key">{t("game.card.level")}</dt>
                <dd className="fact-val">{level}</dd>
              </div>
            )}
            {!current.local && known && known.user.level !== null && (
              <div className="fact">
                <dt className="fact-key">{t("game.card.level")}</dt>
                <dd className="fact-val">{known.user.level}</dd>
              </div>
            )}
            {/* the account the room verified, when its handle is not the game's name (names in
                the game are Latin-1 and free; handles are the platform's) */}
            {!current.local && known && !known.user.isGuest && known.user.handle.toLowerCase() !== current.name.toLowerCase() && (
              <div className="fact">
                <dt className="fact-key">{t("game.card.account")}</dt>
                <dd className="fact-val" id="card-handle">
                  @{known.user.handle}
                </dd>
              </div>
            )}
          </dl>
          {friendable && (
            <button
              className="lane"
              type="button"
              id="card-friend"
              data-first=""
              data-friend={known?.friend ?? "unknown"}
              disabled={socialBusy || known?.friend === "outgoing" || known?.friend === "friends"}
              onClick={() => void runSocial(() => social.onFriend(current))}
            >
              {known?.friend === "friends" ? t("game.card.friends") : known?.friend === "outgoing" ? t("game.card.requestSent") : known?.friend === "incoming" ? t("game.card.acceptFriend") : t("game.card.addFriend")}
            </button>
          )}
          {duelable && !duelBlocked && social.duel && (
            <button
              className="lane"
              type="button"
              id="card-duel"
              data-first={friendable ? undefined : ""}
              data-sent={duelSent ? "1" : undefined}
              disabled={socialBusy}
              onClick={() => void runSocial(() => (duelSent ? social.duel!.onCancel() : social.duel!.onChallenge(current)))}
            >
              {duelSent ? t("game.duel.cancelChallenge") : t("game.duel.challenge")}
            </button>
          )}
          {person && (
            <button
              className="lane lane-small"
              type="button"
              id="card-mute"
              data-first={friendable || (duelable && !duelBlocked) ? undefined : ""}
              disabled={socialBusy}
              onClick={() => void runSocial(() => social.onMute(current, !social.muted(current)))}
            >
              {social.muted(current) ? t("game.card.unmute") : t("game.card.mute")}
            </button>
          )}
          {duelable && social.duel && (
            <button
              className="lane lane-small"
              type="button"
              id="card-duel-block"
              data-blocked={duelBlocked ? "1" : undefined}
              disabled={socialBusy}
              onClick={() => void runSocial(() => social.duel!.onBlock(current, !duelBlocked))}
            >
              {duelBlocked ? t("game.duel.unblock") : t("game.duel.block")}
            </button>
          )}
          {reportable && (
            <>
              <button className="lane" type="button" id="card-report" data-first={person ? undefined : ""} onClick={() => openReport(current.id)}>
                {t("game.card.report")}
              </button>
              <p className="gm-say">{t("game.card.reportSay")}</p>
            </>
          )}
          <button className="lane lane-small" type="button" id="card-back" data-first={reportable ? undefined : ""} onClick={back}>
            {t("common.back")}
          </button>
        </section>
      )}

      {view.kind === "report" && current && (
        <section className="gm-panel gm-card gm-report" aria-labelledby="report-title">
          <h2 className="gm-name" id="report-title">
            <span className="gm-name-key">{t("game.report.head")}</span> {current.name}
          </h2>
          <p className="gm-sub">{t("game.report.describe")}</p>
          <ul className="reasons" role="radiogroup" aria-labelledby="report-title">
            {REASONS.map((r, i) => (
              <li key={r.id}>
                <button
                  className={"lane lane-small reason" + (reason === r.id ? " is-chosen" : "")}
                  type="button"
                  role="radio"
                  aria-checked={reason === r.id}
                  id={"reason-" + r.id}
                  data-first={i === 0 ? "" : undefined}
                  onClick={() => {
                    setReason(r.id);
                    setFailure(null);
                  }}
                >
                  {t(r.label)}
                </button>
              </li>
            ))}
          </ul>
          {/* the form row idiom (profile/bits.tsx Field) around a text area that grows: the
              note wraps instead of scrolling; Enter sends, it never makes a new line */}
          <div className="field gm-field">
            <label className="field-key" htmlFor="report-note">
              {t("game.report.note")}
            </label>
            <div className="field-val">
              <textarea
                className="field-input field-area"
                id="report-note"
                rows={1}
                ref={noteRef}
                value={note}
                placeholder="—"
                maxLength={NOTE_MAX}
                spellCheck={false}
                aria-describedby="report-note-hint"
                onChange={(e) => setNote(e.target.value.replace(/\s*\n\s*/g, " ").slice(0, NOTE_MAX))}
              />
              <p className="field-hint" id="report-note-hint">
                {t("game.report.noteHint", { max: NOTE_MAX }) + (note.length ? " " + t("game.report.noteLeft", { n: NOTE_MAX - note.length }) : "")}
              </p>
            </div>
          </div>
          <button className="lane" type="button" id="report-send" disabled={sending} onClick={() => void send()}>
            {sending ? t("game.report.sending") : t("game.report.send")}
          </button>
          {failure && (
            <p className="cell cell-block" id="report-error" role="alert">
              {failure}
            </p>
          )}
        </section>
      )}

    </div>
  );
});
