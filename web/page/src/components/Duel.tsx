// A duel over the game, at the top right under the menu cell (clear of
// the chat and the gauges; the rewards wait while it is up). In the panel idiom of the match summary
//: the cell line on near-black, a quiet head with the seconds left at
// its right, the challenger's swatch and the sentence, ACCEPT as the solid act and DECLINE as a
// lined one. It takes no focus and no keys (the game keeps them): its acts take the pointer
// only, and the Esc menu holds the same choice as lanes, which the key cell under it names.
//   incoming  "<name> challenges you to a 1v1", ACCEPT / DECLINE, the seconds left
//   waiting   my challenge is out: "waiting for <name> to accept", CANCEL
//   note      how a duel ended (declined, no answer, a failure), a few seconds
//   moving    accepted: both pages are on their way to the duel room
//   arrived   my invite link (Challenge a Friend) was opened while I play: "Your opponent has arrived!",
//             GO TO 1V1 as the solid act, NOT NOW lined, the seconds left to answer
//   invite    my invite link is open while I play: the time it stays open, COPY LINK, CANCEL
// On the home screen (home: a friend's challenge, or mine to a friend, from the Friends panel) the
// same panel stands at the lower left, and its acts take the focus and the keys (App.tsx): no game
// holds them, and there is no Esc menu to answer in.
import { useEffect, useState, type MouseEvent } from "react";
import type { DuelView } from "../platform/types";
import { msOf } from "../platform/duels";
import { DUEL_ARRIVE_MS } from "../platform/quickplay";
import { clockLeft } from "./Connecting";
import { touchPrimary } from "../compact";
import { KeyCap } from "./KeyCaps";
import { t } from "../i18n";

export type DuelLine =
  // color: the challenger's swatch (their cycle colour, from the room's players), when the room has them
  | { kind: "incoming"; duel: DuelView; busy: boolean; color?: string }
  | { kind: "waiting"; duel: DuelView; name: string } // name: the challenged player's, as the game shows it
  | { kind: "note"; text: string }
  | { kind: "moving"; opponent: string }
  | { kind: "arrived"; duel: DuelView; name: string; busy: boolean } // name: the opener's, as their page plays
  | { kind: "invite"; until: number; copied: boolean }; // until: when the link closes (ms)

interface DuelProps {
  line: DuelLine;
  home?: boolean;
  onAccept(): void;
  onDecline(): void;
  onCancel(): void;
  onCopy?(): void; // the invite's COPY LINK
}

// when an arrival's answer is due (ms)
function arrivalDue(duel: DuelView): number {
  return msOf(duel.arrivedAt ?? duel.createdAt) + DUEL_ARRIVE_MS;
}

// a click the player chose; the focus stays with the game
const keepFocus = (e: MouseEvent<HTMLButtonElement>) => e.preventDefault();

// whole seconds until `until` (ms), ticking; null without one
export function useSecondsLeft(until: number | null): number | null {
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (until === null) return;
    const timer = window.setInterval(() => setNow(Date.now()), 500);
    return () => window.clearInterval(timer);
  }, [until]);
  return until === null ? null : Math.max(0, Math.ceil((until - now) / 1000));
}

// the challenge's last seconds: the clock turns waiting amber
const LOW_SECONDS = 10;

// what a screen reader hears, once per state: the sentence and the way to answer, then one
// warning at ten seconds (the ticking clock itself is hidden from it)
function said(line: DuelLine, left: number | null, plain: boolean): string {
  if (line.kind === "incoming") {
    if (left !== null && left > 0 && left <= LOW_SECONDS) return t("game.duel.tenLeft");
    const who = t("game.duel.incoming", { name: line.duel.from.name });
    return plain ? who : t("game.duel.announce", { sentence: who });
  }
  if (line.kind === "waiting") return t("game.duel.waiting", { name: line.name });
  if (line.kind === "arrived") {
    if (left !== null && left > 0 && left <= LOW_SECONDS) return t("game.duel.tenLeft");
    const who = t("game.duel.arrived", { name: line.name });
    return plain ? who : t("game.duel.announce", { sentence: who });
  }
  if (line.kind === "invite") return t("game.duel.inviteOpen");
  if (line.kind === "note") return line.text;
  return t("game.duel.moving", { name: line.opponent });
}

export function DuelPanel({ line, home = false, onAccept, onDecline, onCancel, onCopy }: DuelProps) {
  const left = useSecondsLeft(
    line.kind === "incoming" || line.kind === "waiting" ? msOf(line.duel.expiresAt) : line.kind === "arrived" ? arrivalDue(line.duel) : null
  );
  // the invite's clock: minutes and seconds, ticking
  const inviteLeft = useSecondsLeft(line.kind === "invite" ? line.until : null);
  const touch = touchPrimary();
  const low = (line.kind === "incoming" || line.kind === "arrived") && left !== null && left <= LOW_SECONDS;
  // keyed by the duel: a new challenge comes in afresh (its arrival cue plays again)
  const key =
    line.kind === "incoming" || line.kind === "waiting"
      ? line.kind + line.duel.code
      : line.kind === "arrived"
        ? line.kind + line.duel.code + (line.duel.arrivedAt ?? "")
        : line.kind;
  // over a game the acts take the pointer only; at home they are in the Tab order too
  const stop = home ? 0 : -1;
  return (
    <section className="duel" id="duel-panel" key={key} aria-label={t("game.duel.label")} data-kind={line.kind}>
      <p className="duel-said" aria-live="polite">
        {said(line, left, touch || home)}
      </p>
      <div className="duel-top" aria-hidden="true">
        <h2 className="duel-head">{t("game.duel.title")}</h2>
        {left !== null && <span className={"duel-left" + (low ? " is-low" : "")}>{t("game.duel.secondsLeft", { seconds: left })}</span>}
        {inviteLeft !== null && line.kind === "invite" && <span className="duel-left">{t("game.duel.timeLeft", { time: clockLeft(line.until, Date.now()) })}</span>}
      </div>
      {line.kind === "incoming" && (
        <>
          <p className="duel-say is-who" id="duel-from">
            {line.color && <span className="duel-swatch" style={{ background: line.color }} aria-hidden="true" />}
            <span>{t("game.duel.incoming", { name: line.duel.from.name })}</span>
          </p>
          <p className="duel-format">{t("game.duel.format")}</p>
          <div className="duel-acts">
            <button className="duel-act is-solid" type="button" id="duel-accept" tabIndex={stop} disabled={line.busy} onMouseDown={keepFocus} onClick={onAccept}>
              {t("game.duel.accept")}
            </button>
            <button className="duel-act" type="button" id="duel-decline" tabIndex={stop} disabled={line.busy} onMouseDown={keepFocus} onClick={onDecline}>
              {t("game.duel.decline")}
            </button>
          </div>
          {!home && <p className="duel-foot">{t("game.duel.leaves")}</p>}
          {!touch && !home && (
            <p className="duel-keys">
              <KeyCap name="esc" />
              <span>{t("game.duel.inMenu")}</span>
            </p>
          )}
        </>
      )}
      {line.kind === "waiting" && (
        <>
          <p className="duel-say" id="duel-waiting">
            {t("game.duel.waiting", { name: line.name })}
          </p>
          <div className="duel-acts">
            <button className="duel-act" type="button" id="duel-cancel" tabIndex={stop} onMouseDown={keepFocus} onClick={onCancel}>
              {t("game.duel.cancel")}
            </button>
          </div>
        </>
      )}
      {line.kind === "arrived" && (
        <>
          <p className="duel-say is-who is-arrived" id="duel-arrived">
            <span>{t("game.duel.arrived", { name: line.name })}</span>
          </p>
          <p className="duel-format">{t("game.duel.format")}</p>
          <div className="duel-acts">
            <button className="duel-act is-solid" type="button" id="duel-go" tabIndex={stop} disabled={line.busy} onMouseDown={keepFocus} onClick={onAccept}>
              {t("game.duel.go")}
            </button>
            <button className="duel-act" type="button" id="duel-not-now" tabIndex={stop} disabled={line.busy} onMouseDown={keepFocus} onClick={onDecline}>
              {t("game.duel.notNow")}
            </button>
          </div>
          {!touch && !home && (
            <p className="duel-keys">
              <KeyCap name="esc" />
              <span>{t("game.duel.inMenu")}</span>
            </p>
          )}
        </>
      )}
      {line.kind === "invite" && (
        <>
          <p className="duel-say" id="duel-invite">
            {t("game.duel.inviteOpen")}
          </p>
          <div className="duel-acts">
            <button className="duel-act" type="button" id="duel-invite-copy" tabIndex={stop} data-done={line.copied ? "" : undefined} onMouseDown={keepFocus} onClick={onCopy}>
              {line.copied ? t("game.duel.copied") : t("game.duel.copyLink")}
            </button>
            <button className="duel-act" type="button" id="duel-invite-cancel" tabIndex={stop} onMouseDown={keepFocus} onClick={onCancel}>
              {t("game.duel.cancel")}
            </button>
          </div>
        </>
      )}
      {line.kind === "note" && (
        <p className="duel-say" id="duel-note">
          {line.text}
        </p>
      )}
      {line.kind === "moving" && (
        <p className="duel-say is-ink" id="duel-moving">
          {t("game.duel.moving", { name: line.opponent })}
        </p>
      )}
    </section>
  );
}
