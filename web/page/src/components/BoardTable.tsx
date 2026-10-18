// A leaderboard's rows, shared by the Tab window's board tabs and the Leaderboards page
// (BoardTable draws a page of them; the page's scrolling board draws BoardHead and BoardLine itself):
// place, movement, the player (their cycle colour's swatch, name, handle), kills, deaths, K/D, wins;
// the sorted column marked. First place stays as one row on top when the page starts lower, with
// how many more stand above the rows, and reads a step heavier.
//
// Two marks never share a device: the player's own row is a ground a step lighter with YOU (a
// guest's, theirs alone, NOT SAVED in waiting amber); the focused row (the page's) carries the 4u
// ink bar. Movement is a state players read at a glance in every game: up in online green, down in
// failed red, always with its arrow and number, and said in words to a screen reader.
import type { BoardPage, BoardRow, BoardSort } from "../platform/types";
import { num, t } from "../i18n";

export function Move({ row }: { row: Pick<BoardRow, "move" | "new"> }) {
  if (row.new) return <span className="bt-move is-new">{t("game.score.new")}</span>;
  if (!row.move) {
    return (
      <span className="bt-move is-same">
        <span aria-hidden="true">–</span>
        <span className="vh">{t("game.score.moveSame")}</span>
      </span>
    );
  }
  const up = row.move > 0;
  return (
    <span className={"bt-move " + (up ? "is-up" : "is-down")}>
      <i className="bt-tri" aria-hidden="true" />
      <span aria-hidden="true">{num(Math.abs(row.move))}</span>
      <span className="vh">{t(up ? "game.score.moveUp" : "game.score.moveDown", { n: Math.abs(row.move) })}</span>
    </span>
  );
}

export function kdText(kd: number): string {
  return num(kd, { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

// the swatch: the player's cycle colour, or an empty cell when their page never said it
export function Swatch({ color }: { color: string | null | undefined }) {
  return <i className={"bt-swatch" + (color ? "" : " is-unknown")} style={color ? { background: color } : undefined} aria-hidden="true" />;
}

interface BoardTableProps {
  page: BoardPage;
  label: string; // the table's name for a screen reader
  youColor?: string | null; // the player's own colour, which the page knows before the platform does
  focused?: number | null; // the Leaderboards page's focused row (a rank); absent: no row focus (the Tab window)
  onRow?(row: BoardRow): void; // the page: a row pressed
  onSort?(sort: BoardSort): void; // the column heads sort (kills, K/D, wins), as the Server Browser's do
  keyHeads?: boolean; // the heads take the keyboard focus (the page); the Tab window's take the pointer only
}

// a row as a screen reader says it: the place, the player, the movement, every column by name
function rowLabel(row: BoardRow): string {
  const move = row.new
    ? t("game.score.new")
    : !row.move
      ? t("game.score.moveSame")
      : t(row.move > 0 ? "game.score.moveUp" : "game.score.moveDown", { n: Math.abs(row.move) });
  const said = t("game.score.rowLabel", { rank: num(row.rank), name: row.name, move, kills: num(row.kills), deaths: num(row.deaths), kd: kdText(row.kd), wins: num(row.wins) });
  return row.you ? said + " " + (row.saved ? t("game.players.you") : t("game.score.notSaved")) : said;
}

interface BoardHeadProps {
  sort: BoardSort;
  onSort?(sort: BoardSort): void;
  keyHeads?: boolean;
}

// the column heads, the sorted one marked
export function BoardHead({ sort, onSort, keyHeads }: BoardHeadProps) {
  const head = (key: BoardSort | "deaths", word: string) => (
    <th scope="col" className={key === sort ? "is-sorted" : undefined} aria-sort={key === sort ? "descending" : undefined}>
      {onSort && key !== "deaths" ? (
        <button
          type="button"
          className="bt-sort"
          data-sort={key}
          tabIndex={keyHeads ? 0 : -1}
          onMouseDown={keyHeads ? undefined : (e) => e.preventDefault()}
          onClick={() => onSort(key)}
        >
          {word}
        </button>
      ) : (
        word
      )}
    </th>
  );
  return (
    <tr>
      <th scope="col" className="bt-l">{t("game.score.rank")}</th>
      <th scope="col" className="bt-l" aria-label={t("game.score.move")} />
      <th scope="col" className="bt-l">{t("game.score.player")}</th>
      {head("kills", t("game.score.kills"))}
      {head("deaths", t("game.score.deaths"))}
      {head("kd", t("game.score.kd"))}
      {head("wins", t("game.score.wins"))}
    </tr>
  );
}

interface BoardLineProps {
  row: BoardRow;
  youColor?: string | null;
  focused?: number | null; // absent: no row focus (the Tab window)
  onRow?(row: BoardRow): void;
  rowIndex?: number; // aria-rowindex, when only some of the rows are drawn (the page's scrolling board)
}

export function BoardLine({ row, youColor, focused, onRow, rowIndex }: BoardLineProps) {
  const interactive = focused !== undefined;
  const on = interactive && focused === row.rank;
  return (
    <tr
      className={(row.you ? "is-you" : "") + (on ? " is-focus" : "") + (row.rank === 1 ? " is-first" : "")}
      data-rank={row.rank}
      aria-rowindex={rowIndex}
      aria-selected={interactive ? on : undefined}
      aria-label={rowLabel(row)}
      tabIndex={interactive ? (on ? 0 : -1) : undefined}
      onClick={onRow ? () => onRow(row) : undefined}
    >
      <td className="bt-rank">{num(row.rank)}</td>
      <td className="bt-movecell">
        <Move row={row} />
      </td>
      <td className="bt-who">
        <div className="bt-who-in">
          <Swatch color={row.you ? youColor ?? row.color : row.color} />
          {row.you && !row.saved && <i className="bt-dot" aria-hidden="true" />}
          <span className="bt-name">{row.name}</span>
          {row.handle && <span className="bt-handle">@{row.handle}</span>}
          {row.you && <span className={"bt-mark" + (row.saved ? "" : " is-unsaved")}>{row.saved ? t("game.players.you") : t("game.score.notSaved")}</span>}
        </div>
      </td>
      <td className="bt-num bt-main">{num(row.kills)}</td>
      <td className="bt-num">{num(row.deaths)}</td>
      <td className="bt-num">{kdText(row.kd)}</td>
      <td className="bt-num">{num(row.wins)}</td>
    </tr>
  );
}

// a place whose row is on its way: its number, quiet dashes for the rest
export function PendingLine({ rank, focused, rowIndex, onRow }: { rank: number; focused?: number | null; rowIndex?: number; onRow?(): void }) {
  const on = focused === rank;
  return (
    <tr className={"bt-pending" + (on ? " is-focus" : "")} data-rank={rank} aria-rowindex={rowIndex} aria-selected={on} aria-busy="true" tabIndex={on ? 0 : -1} onClick={onRow}>
      <td className="bt-rank">{num(rank)}</td>
      <td className="bt-movecell" />
      <td className="bt-who">
        <span className="bt-name">–</span>
      </td>
      <td className="bt-num">–</td>
      <td className="bt-num">–</td>
      <td className="bt-num">–</td>
      <td className="bt-num">–</td>
    </tr>
  );
}

export function BoardTable({ page, label, youColor, focused, onRow, onSort, keyHeads }: BoardTableProps) {
  const interactive = focused !== undefined;
  const leaderApart = page.leader && page.rows.length > 0 && page.rows[0].rank > 1;
  const above = page.rows.length ? page.rows[0].rank - (leaderApart ? 2 : 1) : 0;
  const line = (row: BoardRow) => <BoardLine key={row.rank + ":" + row.name} row={row} youColor={youColor} focused={focused} onRow={onRow} />;
  return (
    <table className="bt" data-sort={page.sort} role={interactive ? "grid" : undefined} aria-label={label}>
      <thead>
        <BoardHead sort={page.sort} onSort={onSort} keyHeads={keyHeads} />
      </thead>
      <tbody>
        {leaderApart && page.leader && line(page.leader)}
        {leaderApart && above > 0 && (
          <tr className="bt-gap">
            <td colSpan={7}>{t("game.score.above", { n: above })}</td>
          </tr>
        )}
        {page.rows.map(line)}
      </tbody>
    </table>
  );
}

// a board while its rows are on their way: the columns, with quiet dashes for rows (no spinner)
export function BoardSkeleton({ rows = 11 }: { rows?: number }) {
  return (
    <table className="bt is-loading" aria-hidden="true">
      <tbody>
        {Array.from({ length: rows }, (_, i) => (
          <tr key={i}>
            <td className="bt-rank">–</td>
            <td className="bt-movecell" />
            <td className="bt-who">
              <span className="bt-name">–</span>
            </td>
            <td className="bt-num">–</td>
            <td className="bt-num">–</td>
            <td className="bt-num">–</td>
            <td className="bt-num">–</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

// "7h 12m", from the window's resetsAt
export function resetsText(resetsAt: string | null, now = Date.now()): string | null {
  if (!resetsAt) return null;
  const left = Math.max(0, Date.parse(resetsAt) - now);
  const minutes = Math.ceil(left / 60000);
  const h = Math.floor(minutes / 60);
  const m = minutes % 60;
  return h > 0 ? t("game.score.hoursMinutes", { h, m }) : t("game.score.minutes", { m });
}

// what the window's movement compares with: an hour ago on today, the day's start on the others
export function movementWords(window: string): string {
  return window === "today" ? t("game.score.sinceHour") : t("game.score.sinceDay");
}
