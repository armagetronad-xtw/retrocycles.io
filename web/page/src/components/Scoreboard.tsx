// The scoreboard: the game's SCORE key (Tab by default) shows it and hides it, as it did
// the game's own table, which the page now draws instead (aa.setPageScores, the `scores` event).
// In the left sheet's place, so the cycle on the right stays live; it never dims
// the game and takes no keys but Shift with the SCORE key, which moves to the next tab (App). The
// pointer can pick a tab or a sort.
//
// MATCH: the room's players by score, split by team when a team has more than one player: score,
// deaths (the platform's tally of the room's match, for official rooms), ping; a player waiting to
// respawn is faded, as the game's Alive column said. TODAY, THIS WEEK, ALL TIME (features.leaderboards):
// the board of the room's kind of game, opened on the player's own place.
import { useMemo } from "react";
import type { AaPlayer } from "../aa/types";
import type { BoardSort, BoardWindow } from "../platform/types";
import { useBoard, type TallyLine } from "../platform/leaderboards";
import { BoardTable } from "./BoardTable";
import { KeyCap } from "./KeyCaps";
import { num, t, type PlainKey } from "../i18n";

export type ScoreTab = "match" | BoardWindow;

export const SCORE_TABS: ScoreTab[] = ["match", "today", "week", "all-time"];

const TAB_WORDS: Record<ScoreTab, PlainKey> = {
  match: "game.score.match",
  today: "game.score.today",
  week: "game.score.week",
  "all-time": "game.score.allTime"
};


// the game's 0-15 colour as the swatch's
function swatch(p: AaPlayer): string {
  const [r, g, b] = p.color.map((c) => Math.round((Math.max(0, Math.min(15, c)) * 255) / 15));
  return `rgb(${r}, ${g}, ${b})`;
}

// a pointer press that must not take the focus from the game
const keepFocus = (e: { preventDefault(): void }) => e.preventDefault();

interface ScoreboardProps {
  tab: ScoreTab;
  onTab(tab: ScoreTab): void;
  players: AaPlayer[];
  tally: ReadonlyMap<string, TallyLine> | null;
  roomName: string;
  // a server that is not ours: nobody there signed in through Retrocycles, so the names are unverified
  unverified?: boolean;
  boards: { id: string; name: string }[]; // empty: MATCH only
  board: string | null;
  onBoard(id: string): void;
  sort: BoardSort;
  onSort(sort: BoardSort): void;
  keyLabel: string | null; // the SCORE key's name, for SHIFT TAB NEXT
  onSave(): void; // a guest's SAVE YOUR PROGRESS

  youColor?: string | null; // the player's own cycle colour, for their row's swatch
}

export function Scoreboard(props: ScoreboardProps) {
  const { tab, onTab, boards, keyLabel } = props;
  const tabs = boards.length ? SCORE_TABS : (["match"] as ScoreTab[]);
  return (
    <section className="sc" id="scoreboard" aria-label={t("game.score.label")}>
      <div className="sc-tabs" role="tablist">
        {tabs.map((k) => (
          <button
            key={k}
            type="button"
            role="tab"
            className={"sc-tab" + (k === tab ? " is-on" : "")}
            aria-selected={k === tab}
            tabIndex={-1}
            onMouseDown={keepFocus}
            onClick={() => onTab(k)}
          >
            {t(TAB_WORDS[k])}
          </button>
        ))}
        {tabs.length > 1 && keyLabel && (
          <span className="sc-key" aria-hidden="true">
            <KeyCap name={t("game.score.shift")} />
            <KeyCap name={keyLabel} />
            <span className="sc-keyword">{t("game.score.next")}</span>
          </span>
        )}
      </div>
      {tab === "match" ? <MatchView {...props} /> : <BoardView {...props} window={tab} />}
    </section>
  );
}

interface Group {
  key: string;
  name: string | null; // null: everyone (no team has more than one player)
  color: string | null;
  score: number;
  players: AaPlayer[];
}

function MatchView({ players, tally, roomName, unverified }: ScoreboardProps) {
  const groups = useMemo<Group[]>(() => {
    const playing = players.filter((p) => !p.spectating);
    const byScore = (a: AaPlayer, b: AaPlayer) => b.score - a.score || a.name.localeCompare(b.name);
    const teams = new Map<string, AaPlayer[]>();
    for (const p of playing) {
      const k = p.teamId == null ? "solo:" + p.id : String(p.teamId);
      teams.set(k, [...(teams.get(k) ?? []), p]);
    }
    // the game's table splits by team only when a team has more than one player
    if (![...teams.values()].some((m) => m.length > 1)) {
      return [{ key: "all", name: null, color: null, score: 0, players: playing.sort(byScore) }];
    }
    return [...teams.entries()]
      .map(([k, members]) => ({
        key: k,
        name: members[0].team,
        color: swatch(members[0]),
        score: members.reduce((s, p) => s + p.score, 0),
        players: members.sort(byScore)
      }))
      .sort((a, b) => b.score - a.score);
  }, [players]);
  const count = groups.reduce((n, g) => n + g.players.length, 0);
  let place = 0;
  return (
    <>
      <div className="sc-sub">
        <span className="sc-room">{roomName}</span>
        {unverified && <span className="sc-unverified" id="scores-unverified">{t("game.score.unverified")}</span>}
        <span className="sc-grow" />
        <span>{t("game.score.players", { n: count })}</span>
      </div>
      <table className="bt sc-match">
        <thead>
          <tr>
            <th scope="col" className="bt-l" colSpan={2}>{t("game.score.player")}</th>
            <th scope="col" className="is-sorted">{t("game.score.score")}</th>
            <th scope="col">{t("game.score.deaths")}</th>
            <th scope="col">{t("game.score.ping")}</th>
          </tr>
        </thead>
        {groups.map((g) => (
          <tbody key={g.key} className={g.name !== null ? "sc-team" : undefined}>
            {g.name !== null && (
              <tr className="sc-teamhead">
                <td className="bt-rank" />
                <td className="bt-who">
                  <i className="sc-swatch" style={g.color ? { background: g.color } : undefined} />
                  <span className="sc-teamname">{g.name}</span>
                  <span className="bt-mark">{t("game.score.players", { n: g.players.length })}</span>
                </td>
                <td className="bt-num sc-teamscore">{num(g.score)}</td>
                <td />
                <td />
              </tr>
            )}
            {g.players.map((p) => {
              place += 1;
              const line = tally?.get(p.name);
              const dead = p.alive === false;
              return (
                <tr key={p.id} className={(p.local && !p.bot ? "is-you" : "") + (dead ? " is-dead" : "")}>
                  <td className="bt-rank">{num(place)}</td>
                  <td className="bt-who">
                    <i className="sc-swatch" style={{ background: swatch(p) }} />
                    <span className="bt-name">{p.name}</span>
                    {(p.local || p.bot) && <span className="bt-mark">{p.bot ? t("game.players.bot") : t("game.players.you")}</span>}
                  </td>
                  <td className="bt-num bt-main">{num(p.score)}</td>
                  <td className="bt-num">{line ? num(line.deaths) : "–"}</td>
                  <td className="bt-num">{p.ping == null ? "–" : num(p.ping)}</td>
                </tr>
              );
            })}
          </tbody>
        ))}
      </table>
    </>
  );
}

function BoardView({ boards, board, onBoard, sort, onSort, onSave, youColor, window: w }: ScoreboardProps & { window: BoardWindow }) {
  const live = useBoard(board, w, sort, true);
  const page = live.page;
  const failed = live.failed;
  const current = boards.find((b) => b.id === board) ?? boards[0];
  const step = (by: number) => {
    const i = boards.findIndex((b) => b.id === current.id);
    onBoard(boards[(i + by + boards.length) % boards.length].id);
  };
  return (
    <>
      <div className="sc-sub">
        {boards.length > 1 ? (
          <span className="sc-choice">
            <span className="sc-choice-key">{t("game.score.mode")}</span>
            <button type="button" className="sc-chev" tabIndex={-1} aria-label={t("game.score.modePrev")} onMouseDown={keepFocus} onClick={() => step(-1)} />
            <span className="sc-choice-value">{current.name}</span>
            <button type="button" className="sc-chev is-next" tabIndex={-1} aria-label={t("game.score.modeNext")} onMouseDown={keepFocus} onClick={() => step(1)} />
          </span>
        ) : (
          <span className="sc-room">{current?.name}</span>
        )}
      </div>
      {page && page.rows.length > 0 && <BoardTable page={page} label={t("game.score.label")} youColor={youColor} onSort={onSort} />}
      {page && !page.rows.length && <p className="sc-empty">{t("game.score.empty")}</p>}
      {!page && failed && <p className="sc-empty">{t("game.score.offline")}</p>}
      {page && page.me && !page.me.saved && (
        <div className="sc-save">
          <button type="button" className="sc-solid" tabIndex={-1} onMouseDown={keepFocus} onClick={onSave}>
            {t("game.summary.save")}
          </button>
        </div>
      )}
      {page && !page.me && page.killsToQualify !== null && (
        <p className="sc-note">{t(sort === "kd" ? "game.score.qualifyKd" : "game.score.qualify", { n: page.killsToQualify })}</p>
      )}
    </>
  );
}
