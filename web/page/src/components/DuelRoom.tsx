// A duel's own start and finish in its private room: the room itself is an ordinary
// game, so the page frames it. High in the arena, right of the chat's column, in the panel idiom
//:
//   vs     on arrival, for a few seconds: 1V1, your swatch and name VS theirs, the format
//   end    at the match's end (the game's "Overall Winner:" line): who won and the score, and the
//          choice. REMATCH says /rematch to the room (gPlatformRematch): once both players have,
//          the match starts over at once, the round being driven ended; QUICK PLAY leaves for a
//          Quick Play. The room's window counts down in the head (its own "… within 30 seconds"
//          line); without both answers it closes and disconnects everyone
//   alone  the opponent left the room: who, QUICK PLAY or STAY (in case they come back)
// It takes no focus and no keys; its acts take the pointer only (the Esc menu leaves too).
import { useEffect, useRef, useState, type MouseEvent } from "react";
import type { Aa, AaConsoleEvent, AaPlayer } from "../aa/types";
import { stripColors } from "./Colored";
import { swatch } from "./GameMenu";
import { useSecondsLeft } from "./Duel";
import { t } from "../i18n";

// how long the VS card stays on arrival
const VS_MS = 5000;
// the opponent gone this long from the room's players: they left (a reconnect is quicker)
const GONE_MS = 4000;
// the room's window: its last seconds in waiting amber
const LOW_SECONDS = 10;

// the game's match winner (with the winner's points when the score limit ended it) and
// gPlatformRematch's lines (AskText, WantsText, StartText)
const WINNER = /^Overall Winner: (.+?) (?:with (-?\d+) points?|after .+)\.?$/;
const REMATCH_ASK = /^Rematch\? .* within (\d+) seconds/;
const REMATCH_WANTS = /^(.+) wants a rematch\.$/;
const REMATCH_START = /^Rematch: a new match starts\.$/;

// the reason the room gives when it closes without a rematch (gPlatformRematch::ClosedText)
export const REMATCH_CLOSED = /nobody asked for a rematch/i;

export type DuelRoomView = "vs" | "end" | "alone";

interface DuelRoomProps {
  aa: Aa | null;
  opponent: string | null; // their name as the game shows it (the duel's); null: the one other person here
  players: AaPlayer[];
  onQuickPlay(): void;
}

const keepFocus = (e: MouseEvent<HTMLButtonElement>) => e.preventDefault();

function nameOf(p: AaPlayer): string {
  return stripColors(p.name).trim();
}

export function DuelRoom({ aa, opponent, players, onQuickPlay }: DuelRoomProps) {
  const me = players.find((p) => p.local) ?? null;
  const people = players.filter((p) => !p.local && !p.bot && !p.spectating);
  const them = (opponent ? people.find((p) => nameOf(p) === opponent.trim()) : undefined) ?? (people.length === 1 ? people[0] : null);
  const theirName = them ? nameOf(them) : opponent ?? "";

  const [view, setView] = useState<DuelRoomView | null>("vs");
  const [winner, setWinner] = useState<string | null>(null);
  // the scores when the winner line came, and the winner's points from that line: the players'
  // list can still lack the last round's points then (they sync a moment later), so the card keeps
  // the higher of these and the live scores, and the winner's side is the line's number
  const [score, setScore] = useState<[number, number] | null>(null);
  const [winnerPoints, setWinnerPoints] = useState<number | null>(null);
  // the room's rematch window, my answer, theirs
  const [closesAt, setClosesAt] = useState<number | null>(null);
  const [asked, setAsked] = useState(false);
  const [theyAsked, setTheyAsked] = useState(false);
  const left = useSecondsLeft(view === "end" ? closesAt : null);

  // the VS card goes on its own
  useEffect(() => {
    if (view !== "vs") return;
    const timer = window.setTimeout(() => setView((v) => (v === "vs" ? null : v)), VS_MS);
    return () => window.clearTimeout(timer);
  }, [view]);

  // the room's lines: the match's end, the rematch window, the other answer, the restart
  const latest = useRef({ me, them });
  latest.current = { me, them };
  useEffect(() => {
    if (!aa) return;
    return aa.on("console", (e: AaConsoleEvent) => {
      const text = stripColors(e.text).trim();
      const ask = REMATCH_ASK.exec(text);
      if (ask) {
        setClosesAt(Date.now() + Number(ask[1]) * 1000);
        return;
      }
      if (REMATCH_START.test(text)) {
        setView((v) => (v === "end" ? null : v));
        return;
      }
      const wants = REMATCH_WANTS.exec(text);
      if (wants) {
        const mine = latest.current.me;
        if (!mine || wants[1].trim() !== nameOf(mine)) setTheyAsked(true);
        return;
      }
      const won = WINNER.exec(text);
      if (!won) return;
      const { me: mine, them: theirs } = latest.current;
      setWinner(won[1].trim());
      setScore(mine && theirs ? [mine.score, theirs.score] : null);
      setWinnerPoints(won[2] !== undefined ? Number(won[2]) : null);
      setAsked(false);
      setTheyAsked(false);
      setClosesAt(null);
      setView("end");
    });
  }, [aa]);

  // the opponent left: seen here once, then missing for a while
  const seen = useRef(false);
  useEffect(() => {
    if (them) {
      seen.current = true;
      setView((v) => (v === "alone" ? null : v));
      return;
    }
    if (!seen.current) return;
    const timer = window.setTimeout(() => setView("alone"), GONE_MS);
    return () => window.clearTimeout(timer);
  }, [them]);

  const askRematch = () => {
    if (asked) return;
    setAsked(true);
    aa?.chat("/rematch");
  };

  if (!view) return null;
  const myName = me ? nameOf(me) : "";
  const iWon = !!winner && winner === myName;
  const settled = (snap: number, live: number | undefined, isWinner: boolean) =>
    isWinner && winnerPoints !== null ? winnerPoints : Math.max(snap, live ?? snap);
  const [mine, theirs] = score
    ? [settled(score[0], me?.score, iWon), settled(score[1], them?.score, !!winner && winner === theirName)]
    : [null, null];
  const low = left !== null && left <= LOW_SECONDS;
  const foot = asked
    ? t("game.duelRoom.waitingFor", { name: theirName })
    : theyAsked
      ? t("game.duelRoom.theyWant", { name: theirName })
      : t("game.duelRoom.rematchSay");

  return (
    <section className="duel-room" id="duel-room" data-view={view} aria-label={t("game.duelRoom.label")} role="status">
      <div className="duel-room-top">
        <h2 className="duel-head">{view === "end" ? t("game.duelRoom.endHead") : t("game.duel.title")}</h2>
        {view === "end" && left !== null && (
          <span className={"duel-left" + (low ? " is-low" : "")} aria-hidden="true">
            {t("game.duelRoom.closesIn", { seconds: left })}
          </span>
        )}
      </div>
      {view === "vs" && (
        <>
          <p className="duel-vs">
            <span className="duel-vs-side">
              {me && <span className="duel-vs-swatch" style={{ background: swatch(me) }} aria-hidden="true" />}
              <span className="duel-vs-name">{myName}</span>
            </span>
            <span className="duel-vs-word">{t("game.duelRoom.vs")}</span>
            <span className="duel-vs-side is-them">
              <span className="duel-vs-name">{theirName}</span>
              {them && <span className="duel-vs-swatch" style={{ background: swatch(them) }} aria-hidden="true" />}
            </span>
          </p>
          <p className="duel-format">{t("game.duel.format")}</p>
        </>
      )}
      {view === "end" && (
        <>
          <p className="duel-result" id="duel-result">
            <span>{iWon ? t("game.duelRoom.youWin") : winner ? t("game.duelRoom.wins", { name: winner }) : t("game.duelRoom.over")}</span>
            {mine !== null && theirs !== null && (
              <span className="duel-score" aria-label={t("game.duelRoom.scoreSay", { mine, theirs })}>
                {mine}
                <span className="duel-score-dash">–</span>
                {theirs}
              </span>
            )}
          </p>
          <p className={"duel-foot" + (theyAsked && !asked ? " is-ink" : "")} id="duel-rematch-say">
            {foot}
          </p>
          <div className="duel-acts">
            <button
              className="duel-act is-solid"
              type="button"
              id="duel-rematch"
              tabIndex={-1}
              data-asked={asked ? "" : undefined}
              disabled={asked}
              onMouseDown={keepFocus}
              onClick={askRematch}
            >
              {asked ? t("game.duelRoom.asked") : t("game.duelRoom.rematch")}
            </button>
            <button className="duel-act" type="button" id="duel-quickplay" tabIndex={-1} onMouseDown={keepFocus} onClick={onQuickPlay}>
              {t("game.duelRoom.quickPlay")}
            </button>
          </div>
        </>
      )}
      {view === "alone" && (
        <>
          <p className="duel-say">{t("game.duelRoom.left", { name: theirName })}</p>
          <p className="duel-foot">{t("game.duelRoom.leftSay")}</p>
          <div className="duel-acts">
            <button className="duel-act is-solid" type="button" id="duel-alone-quickplay" tabIndex={-1} onMouseDown={keepFocus} onClick={onQuickPlay}>
              {t("game.duelRoom.quickPlay")}
            </button>
            <button className="duel-act" type="button" id="duel-alone-stay" tabIndex={-1} onMouseDown={keepFocus} onClick={() => setView(null)}>
              {t("game.duelRoom.stay")}
            </button>
          </div>
        </>
      )}
    </section>
  );
}
