// What a round and a match earned, on the page layer over the running game:
// never in the way. Both take no focus and no keys (the game keeps them), take no pointer
// events except the one action, and go away on their own (platform/useRewards.ts). The
// round's line sits low at the left, just above the game's HUD, in the game's face. The
// match summary is a panel at the right edge (centred, it would
// cover the player's cycle in the next match's countdown): the place set large in the
// game's bitmap face, the facts as rows under hairlines (the match's XP, the level and its
// line), and for a guest on the matches savePrompt.ts picks, SAVE YOUR PROGRESS as a solid
// button with the trail running laps round it. The cross at its head closes it, the pointer
// resting on it holds it; neither takes the focus from the game.
import { useState, type MouseEvent } from "react";
import { levelFraction, ordinal, xpNumber, xpText } from "../platform/progression";
import { num, t } from "../i18n";
import { saveWords, strongSave, type SaveTrigger } from "../platform/savePrompt";
import type { MatchLine, RoundLine } from "../platform/useRewards";
import type { MyPlace, Progression } from "../platform/types";
import { Icon } from "./Icons";
import { LapTrail } from "./Trail";

export function XpBar({ progression, id }: { progression: Pick<Progression, "xp" | "levelXp" | "nextLevelXp">; id?: string }) {
  const done = levelFraction(progression);
  return (
    <span
      className="xp-bar"
      id={id}
      role="img"
      aria-label={t("game.xp.toNext", { done: progression.xp - progression.levelXp, total: progression.nextLevelXp - progression.levelXp })}
    >
      <i className="xp-bar-done" style={{ flexGrow: done }} />
      <i className="xp-bar-rest" style={{ flexGrow: 1 - done }} />
    </span>
  );
}

export function XpLine({ line }: { line: RoundLine }) {
  return (
    <p className="xp-line" id="xp-line" role="status" key={line.key}>
      {line.amount !== 0 && <span className="xp-line-gain">{xpText(line.amount)}</span>}
      {line.levelUp && <span className="xp-line-level">{t("game.xp.level", { level: line.level })}</span>}
    </p>
  );
}

interface MatchSummaryProps {
  summary: MatchLine;
  // today's place on the board after this match (features.leaderboards); null: none or not read yet
  board?: MyPlace | null;
  onSave(from: DOMRect, trigger: SaveTrigger): void;
  onClose(): void;
  onHold(hold: boolean): void;
}

// a click the player chose; the focus stays with the game
const keepFocus = (e: MouseEvent<HTMLButtonElement>) => e.preventDefault();

export function MatchSummary({ summary, board, onSave, onClose, onHold }: MatchSummaryProps) {
  const { match, progression, save: trigger } = summary;
  const up = match.levelAfter > match.levelBefore;
  const [button, setButton] = useState<HTMLButtonElement | null>(null);
  const save = (e: MouseEvent<HTMLButtonElement>) => trigger && onSave(e.currentTarget.getBoundingClientRect(), trigger);
  const place = match.place !== null && match.players > 0 ? match.place : null;
  return (
    <section
      className="ms"
      id="match-summary"
      role="status"
      aria-label={t("game.summary.label")}
      key={summary.key}
      onPointerEnter={() => onHold(true)}
      onPointerLeave={() => onHold(false)}
    >
      <div className="ms-top">
        <h2 className="ms-head">{match.result === "aborted" ? t("game.summary.ended") : t("game.summary.over")}</h2>
        <button className="ms-x" type="button" id="summary-close" tabIndex={-1} aria-label={t("game.summary.closeLabel")} title={t("game.summary.closeTitle")} onMouseDown={keepFocus} onClick={onClose}>
          <Icon name="x" className="ms-x-icon" />
        </button>
      </div>
      {place !== null && (
        <p className="ms-place">
          <span className={"ms-place-n" + (ordinal(place).endsWith(".") ? " is-dot" : "")}>{ordinal(place)}</span>
          <span className="ms-place-of">{t("game.summary.of", { players: match.players })}</span>
        </p>
      )}
      <dl className="ms-facts">
        <div className="ms-fact">
          <dt>{t("game.summary.xp")}</dt>
          <dd className="ms-val">{xpNumber(match.xp)}</dd>
        </div>
        <div className={"ms-fact ms-level" + (up ? " is-up" : "")}>
          <dt>{t("game.summary.level")}</dt>
          <dd className="ms-val">
            {progression.level}
            {up && <span className="ms-note">{t("game.summary.upFrom", { level: match.levelBefore })}</span>}
          </dd>
          <dd className="ms-bar">
            <XpBar progression={progression} id="summary-bar" />
            <span className="ms-xp">
              {t("game.summary.progress", { done: progression.xp - progression.levelXp, total: progression.nextLevelXp - progression.levelXp })}
            </span>
          </dd>
        </div>
        {board && (
          <div className="ms-fact ms-board">
            <dt>{t("game.summary.board")}</dt>
            <dd className="ms-val">
              {t("game.summary.boardToday", { rank: num(board.rank) })}
              {board.move ? (
                <span className={"bt-move " + (board.move > 0 ? "is-up" : "is-down")}>
                  <i className="bt-tri" aria-hidden="true" />
                  {num(Math.abs(board.move))}
                </span>
              ) : null}
            </dd>
          </div>
        )}
      </dl>
      {trigger && (
        <div className={"ms-save" + (strongSave(trigger) ? " is-strong" : "")} data-trigger={trigger}>
          <div className="ms-save-act">
            <button className="ms-button" type="button" id="summary-save" tabIndex={-1} ref={setButton} onMouseDown={keepFocus} onClick={save}>
              {t("game.summary.save")}
            </button>
            <LapTrail around={button} />
          </div>
          <p className="ms-say">{saveWords(trigger)}</p>
        </div>
      )}
    </section>
  );
}
