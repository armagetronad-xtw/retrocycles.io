// The Leaderboards page: the home menu's LEADERBOARDS lane, a full opaque page in the
// Server Browser's frame (its BACK, plate, filter line and docked details; servers.css), so it
// reads as the same kind of place. TODAY, THIS WEEK, ALL TIME as tabs, the sort (kills, K/D, wins),
// what the board is and what its arrows compare with, the board, and the focused row's details at
// the right: its place and totals, and for the player's own row their places on all three boards,
// the kills to pass the next one, and the one act (PLAY, or SAVE YOUR PROGRESS for a guest). On a
// phone the details give way to the board and the player's own place stands in a bar at the foot.
//
// The board is every place in one scrolling list (the rows past the player's own are never
// locked away). It opens on the player's place, or on the top; the rows are read in
// blocks of 50 as they come into view (useBoardRows) and only the rows in view are drawn, a
// spacer standing for the rest. The column heads stay at the top, and the player's own row holds
// to the top or the foot of the list while it is scrolled out of view (pressing it goes back).
//
// The board is a grid: the focused row holds the keyboard focus (a screen reader follows it).
// Keys (App hands them here first): Up/Down move along the rows, Page Up/Down a screenful, Home the
// top, End the bottom, Left/Right the tabs, Enter the focused row's act or a focused button; Esc is App's.
import { Fragment, forwardRef, useCallback, useEffect, useImperativeHandle, useLayoutEffect, useRef, useState, type ReactNode } from "react";
import type { BoardRow, BoardSort, BoardWindow, MyPlace, MyPlaces } from "../platform/types";
import { leaderboardsOn, readMyPlaces, useBoardList, useBoardRows } from "../platform/leaderboards";
import { BoardHead, BoardLine, BoardSkeleton, Move, PendingLine, Swatch, kdText, movementWords, resetsText } from "./BoardTable";
import { Icon } from "./Icons";
import { num, t, type PlainKey } from "../i18n";

export interface LeaderboardsHandle {
  // a key press while the page is up: true when the page took it
  key(e: KeyboardEvent): boolean;
}

interface LeaderboardsProps {
  open: boolean;
  youColor: string | null; // the player's cycle colour (the page's own, before the platform knows it)
  onBack(): void;
  onSave(): void; // a guest's SAVE YOUR PROGRESS
  onPlay(): void; // Quick Play
}

const WINDOWS: { id: BoardWindow; title: PlainKey; mine: "today" | "week" | "allTime" }[] = [
  { id: "today", title: "game.score.today", mine: "today" },
  { id: "week", title: "game.score.week", mine: "week" },
  { id: "all-time", title: "game.score.allTime", mine: "allTime" }
];

const OVERDRAW = 12; // rows drawn past each edge of the view
const ROW_GUESS = 50; // a row's height before one is measured

export const Leaderboards = forwardRef<LeaderboardsHandle, LeaderboardsProps>(function Leaderboards({ open, youColor, onBack, onSave, onPlay }, ref) {
  const live = leaderboardsOn();
  const boards = useBoardList(open && live);
  const [boardId, setBoardId] = useState<string | null>(null);
  const board = boardId ?? boards[0]?.id ?? null;
  const [window_, setWindow] = useState<BoardWindow>("today");
  const [sort, setSort] = useState<BoardSort>("kills");
  const list = useBoardRows(board, window_, sort, open && live);
  const page = list.meta;
  const total = list.total;
  const failed = list.failed;
  const [focus, setFocus] = useState<number | null>(null); // a rank
  const keyed = useRef(true); // a key moved the focus (or the page just opened): the row takes the DOM focus
  const [mine, setMine] = useState<MyPlaces | null>(null);
  const rootRef = useRef<HTMLDivElement>(null);
  const boxRef = useRef<HTMLElement>(null); // the board's scrolling box
  const bodyRef = useRef<HTMLTableSectionElement>(null);
  const headRef = useRef<HTMLTableSectionElement>(null);
  const [rowH, setRowH] = useState(ROW_GUESS);
  const [range, setRange] = useState<[number, number]>([1, 2 * OVERDRAW]);
  const [stuck, setStuck] = useState<"top" | "foot" | null>(null); // the player's row held at an edge
  const opened = useRef(""); // the board, window and sort the list last opened on
  // a column head chosen from the keyboard: it takes the focus back once its rows are in
  const headFocus = useRef<BoardSort | null>(null);
  const sortBy = (s: BoardSort) => {
    if ((document.activeElement as HTMLElement | null)?.classList.contains("bt-sort")) headFocus.current = s;
    setSort(s);
  };

  // a new board, window or sort opens on the player again
  useEffect(() => {
    setFocus(null);
  }, [board, window_, sort]);

  const me = page?.me ?? null;

  // where the rows stand in the box: the body's top and the heads' height
  const geometry = useCallback(() => {
    const box = boxRef.current;
    const body = bodyRef.current;
    if (!box || !body) return null;
    const top = body.getBoundingClientRect().top - box.getBoundingClientRect().top + box.scrollTop;
    const head = headRef.current?.getBoundingClientRect().height ?? 0;
    return { box, top, head };
  }, []);

  // the rows in view (and the edge the player's row holds to), from the scroll position
  const follow = useCallback(() => {
    const g = geometry();
    if (!g) return;
    const { box, top, head } = g;
    const first = Math.floor((box.scrollTop + head - top) / rowH) + 1 - OVERDRAW;
    const last = Math.ceil((box.scrollTop + box.clientHeight - top) / rowH) + OVERDRAW;
    setRange((r) => (r[0] === first && r[1] === last ? r : [first, last]));
    if (me) {
      const at = top + (me.rank - 1) * rowH;
      setStuck(at < box.scrollTop + head ? "top" : at + rowH > box.scrollTop + box.clientHeight ? "foot" : null);
    } else setStuck(null);
  }, [geometry, rowH, me]);

  // bring a place into view: at the top, centred (a jump), or just inside the nearest edge (a step)
  const reveal = useCallback(
    (rank: number, how: "top" | "center" | "nearest") => {
      const g = geometry();
      if (!g) return;
      const { box, top, head } = g;
      const at = top + (rank - 1) * rowH;
      if (how === "top") box.scrollTop = 0;
      else if (how === "center") box.scrollTop = at - head - (box.clientHeight - head - rowH) / 2;
      else if (at < box.scrollTop + head) box.scrollTop = at - head;
      else if (at + rowH > box.scrollTop + box.clientHeight) box.scrollTop = at + rowH - box.clientHeight;
      follow();
    },
    [geometry, rowH, follow]
  );

  // the rows in view are read when missing
  const want = list.want;
  useEffect(() => want(range[0], range[1]), [range, total, want]);

  // a row's real height (the unit scales with the window), and the heads' for the player's held row
  useLayoutEffect(() => {
    const row = bodyRef.current?.querySelector<HTMLElement>("tr[data-rank]");
    const h = row?.getBoundingClientRect().height;
    if (h && Math.abs(h - rowH) > 0.25) setRowH(h);
    const head = headRef.current?.getBoundingClientRect().height;
    if (head) boxRef.current?.style.setProperty("--lb-head", head + "px");
  });

  // the list opens on the player's place, else on the top
  useLayoutEffect(() => {
    if (!page || !total) return;
    const key = page.board + "/" + page.window + "/" + page.sort;
    if (opened.current === key) return;
    // the rows' real height first (a phone's are shorter): the jump is worked out from it
    const h = bodyRef.current?.querySelector<HTMLElement>("tr[data-rank]")?.getBoundingClientRect().height;
    if (h && Math.abs(h - rowH) > 0.25) return setRowH(h);
    opened.current = key;
    keyed.current = true;
    const rank = page.me?.rank ?? 1;
    setFocus(rank);
    reveal(rank, page.me ? "center" : "top");
  }, [page, total, reveal, rowH]);

  // the page closed: it opens on the player again next time
  useEffect(() => {
    if (!open) opened.current = "";
  }, [open]);

  useEffect(() => {
    const box = boxRef.current;
    if (!box) return;
    let frame = 0;
    const onScroll = () => {
      cancelAnimationFrame(frame);
      frame = requestAnimationFrame(follow);
    };
    box.addEventListener("scroll", onScroll, { passive: true });
    window.addEventListener("resize", onScroll);
    return () => {
      cancelAnimationFrame(frame);
      box.removeEventListener("scroll", onScroll);
      window.removeEventListener("resize", onScroll);
    };
  }, [follow]);

  // a key moved the focus: the row takes the DOM focus, so a screen reader reads it
  useEffect(() => {
    const head = headFocus.current;
    if (head && page?.sort === head) {
      headFocus.current = null;
      rootRef.current?.querySelector<HTMLElement>(`.bt-sort[data-sort="${head}"]`)?.focus();
      return;
    }
    if (!keyed.current || focus === null) return;
    const row = bodyRef.current?.querySelector<HTMLElement>(`tr[data-rank="${focus}"]`);
    if (!row) return;
    keyed.current = false;
    row.focus({ preventScroll: true });
  }, [focus, page, range]);

  useEffect(() => {
    if (!open || !live) return;
    let gone = false;
    void readMyPlaces(board ?? undefined, sort).then((p) => !gone && setMine(p));
    return () => {
      gone = true;
    };
  }, [open, live, board, sort]);

  const rowAt = (rank: number): BoardRow | undefined => (me && me.rank === rank ? me : list.row(rank));
  const focused = focus !== null ? rowAt(focus) ?? null : null;
  const act = (row: BoardRow | null) => (row?.you ? (row.saved ? onPlay : onSave) : null);
  const toYou = () => {
    if (!me) return;
    setFocus(me.rank);
    reveal(me.rank, "center");
  };
  const toTop = () => {
    setFocus(1);
    reveal(1, "top");
  };

  useImperativeHandle(
    ref,
    () => ({
      key(e: KeyboardEvent) {
        // a button of the page with the focus: Enter or Space presses it (the client takes those keys too)
        const target = e.target as HTMLElement | null;
        if ((e.key === "Enter" || e.key === " ") && target?.tagName === "BUTTON" && rootRef.current?.contains(target)) {
          e.preventDefault();
          target.click();
          return true;
        }
        // Left/Right on a focused window or sort radio move within its own group
        const group = target?.closest?.('[role="radiogroup"]');
        if ((e.key === "ArrowLeft" || e.key === "ArrowRight") && group && rootRef.current?.contains(group)) {
          e.preventDefault();
          const radios = [...group.querySelectorAll<HTMLButtonElement>('[role="radio"]')];
          const i = radios.indexOf(target as HTMLButtonElement);
          const next = radios[(i + (e.key === "ArrowRight" ? 1 : radios.length - 1)) % radios.length];
          next?.click();
          next?.focus();
          return true;
        }
        if (!page || !total) return false;
        const at = focus ?? 1;
        // a screenful: the rows the box shows under its heads, less one so a row stays in sight
        const screen = Math.max(1, Math.floor(((boxRef.current?.clientHeight ?? 0) - (headRef.current?.offsetHeight ?? 0)) / rowH) - 1);
        const go = (rank: number) => {
          e.preventDefault();
          const next = Math.min(total, Math.max(1, rank));
          keyed.current = true;
          setFocus(next);
          reveal(next, "nearest");
          return true;
        };
        switch (e.key) {
          case "ArrowDown":
            return go(at + 1);
          case "ArrowUp":
            return go(at - 1);
          case "PageDown":
            return go(at + screen);
          case "PageUp":
            return go(at - screen);
          case "Home":
            return go(1);
          case "End":
            return go(total);
          case "Enter": {
            // only the row that holds the focus, and only the player's own has an act
            const onRow = target?.tagName === "TR" && Number(target.getAttribute("data-rank")) === focused?.rank;
            const press = onRow ? act(focused) : null;
            if (!press) return false;
            e.preventDefault();
            press();
            return true;
          }
          case "ArrowLeft":
          case "ArrowRight": {
            e.preventDefault();
            keyed.current = true; // from the board: its starting row takes the focus
            const i = WINDOWS.findIndex((w) => w.id === window_);
            setWindow(WINDOWS[(i + (e.key === "ArrowRight" ? 1 : WINDOWS.length - 1)) % WINDOWS.length].id);
            return true;
          }
        }
        return false;
      }
    }),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [page, total, focus, window_, focused, rowH, reveal]
  );

  // the places drawn: those in view, and the player's own wherever it stands (it holds to an edge)
  const drawn: number[] = [];
  if (total) {
    for (let r = Math.max(1, range[0]); r <= Math.min(total, range[1]); r++) drawn.push(r);
    if (me && me.rank <= total && !drawn.includes(me.rank)) {
      drawn.push(me.rank);
      drawn.sort((a, b) => a - b);
    }
  }
  const lines: ReactNode[] = [];
  let next = 1;
  for (const rank of drawn) {
    if (rank > next) lines.push(<Spacer key={"gap" + next} rows={rank - next} rowH={rowH} />);
    const row = rowAt(rank);
    // the player's own row, held at an edge: pressing it goes back to it
    const held = me?.rank === rank && stuck !== null;
    // one <tr> whether the row is in or on its way (called, not mounted), so a focused row keeps the focus when it arrives
    lines.push(
      <Fragment key={rank}>
        {row
          ? BoardLine({ row, youColor, focused: focus, rowIndex: rank + 1, onRow: held ? toYou : (r) => setFocus(r.rank) })
          : PendingLine({ rank, focused: focus, rowIndex: rank + 1, onRow: () => setFocus(rank) })}
      </Fragment>
    );
    next = rank + 1;
  }
  if (total && next <= total) lines.push(<Spacer key="gap-end" rows={total - next + 1} rowH={rowH} />);

  const resets = page ? resetsText(page.resetsAt) : null;
  const current = boards.find((b) => b.id === board);
  const myPlace = mine ? mine[WINDOWS.find((w) => w.id === window_)!.mine] : null;
  return (
    <div className={"sb lb" + (me ? " has-you" : "")} id="leaderboards" ref={rootRef}>
      <button className="sb-home" type="button" aria-label={t("connect.browser.home")} tabIndex={-1} onClick={onBack} />
      <button className="sb-back" type="button" aria-keyshortcuts="Escape" onClick={onBack}>
        <Icon name="arrow-left" className="sb-back-icon" />
        {t("connect.browser.back")}
      </button>

      <header className="sb-plate">
        <h2 className="sb-h">{t("connect.boards.title")}</h2>
        <div className="sb-tools">
          {boards.length > 1 && current && (
            <span className="sc-choice lb-mode">
              <span className="sc-choice-key">{t("game.score.mode")}</span>
              <button type="button" className="sc-chev" aria-label={t("game.score.modePrev")} onClick={() => setBoardId(boards[(boards.indexOf(current) + boards.length - 1) % boards.length].id)} />
              <span className="sc-choice-value">{current.name}</span>
              <button type="button" className="sc-chev is-next" aria-label={t("game.score.modeNext")} onClick={() => setBoardId(boards[(boards.indexOf(current) + 1) % boards.length].id)} />
            </span>
          )}
          <button className="sb-cell" type="button" disabled={!me} onClick={toYou}>
            {t("connect.boards.you")}
          </button>
          <button className="sb-cell" type="button" disabled={!total} onClick={toTop}>
            {t("connect.boards.top")}
          </button>
        </div>
      </header>

      <div className="sb-filters lb-filters" role="toolbar" aria-label={t("connect.boards.windows")}>
        <div className="sb-scopes" role="radiogroup" aria-label={t("connect.boards.windows")}>
          {WINDOWS.map((w) => (
            <button key={w.id} type="button" role="radio" aria-checked={window_ === w.id} tabIndex={window_ === w.id ? 0 : -1} className={"sb-tab" + (window_ === w.id ? " is-on" : "")} onClick={() => setWindow(w.id)}>
              {t(w.title)}
            </button>
          ))}
        </div>
        {/* what this board is and what its arrows say: the name first and heavier, then the facts */}
        <span className="lb-context">
          <span className="lb-context-board">{current?.name ?? page?.boardName}</span>
          <span className="lb-context-fact">{movementWords(window_)}</span>
          {resets && <span className="lb-context-fact lb-context-resets">{t("game.score.resetsIn", { time: resets })}</span>}
          {page && <span className="lb-context-fact">{t("game.score.ranked", { n: page.ranked })}</span>}
        </span>
      </div>

      {(!live || (failed && !page)) && (
        <p className="sb-fail lb-fail" role="alert">
          <span className="sb-fail-say">{live ? t("game.score.offline") : t("connect.boards.unavailable")}</span>
          {live && (
            <button type="button" className="lb-retry" onClick={list.reload}>
              {t("connect.boards.retry")}
            </button>
          )}
        </p>
      )}

      <div className="sb-main">
        <section className={"sb-board lb-board" + (stuck ? " is-held-" + stuck : "")} aria-label={t("connect.boards.list")} ref={boxRef}>
          {!page && live && !failed && <BoardSkeleton />}
          {page && total > 0 && (
            <table className="bt" data-sort={page.sort} role="grid" aria-label={t("connect.boards.list")} aria-rowcount={total + 1}>
              <thead ref={headRef}>
                <BoardHead sort={page.sort} onSort={sortBy} keyHeads />
              </thead>
              <tbody ref={bodyRef}>{lines}</tbody>
            </table>
          )}
          {page && failed && (
            <p className="lb-stale" role="status">
              {t("connect.boards.stale")}
              <button type="button" className="lb-retry" onClick={list.reload}>
                {t("connect.boards.retry")}
              </button>
            </p>
          )}
          {page && !me && (page.killsToQualify !== null || !total) && (
            <div className="lb-empty">
              <p className="lb-say">
                {!total
                  ? t("game.score.empty")
                  : t(sort === "kd" ? "game.score.qualifyKd" : "game.score.qualify", { n: page.killsToQualify ?? 1 })}
              </p>
              <button type="button" className="lb-line-act" onClick={onPlay}>
                {t("connect.boards.play")}
              </button>
            </div>
          )}
        </section>

        <aside className="sb-detail lb-detail" aria-label={t("connect.boards.detail")}>
          {focused && (
            <Detail row={focused} youColor={youColor} mine={focused.you ? mine : null} myPlace={focused.you ? myPlace : null} sort={sort} window={window_} onSave={onSave} onPlay={onPlay} />
          )}
        </aside>
      </div>

      {/* a phone: the player's own place and its act at the foot, where the details stood */}
      {me && (
        <div className="lb-bar">
          <Swatch color={youColor ?? me.color} />
          <span className="lb-bar-who">
            <span className="lb-bar-place">
              <span className="lb-bar-rank">{t("game.note.rank", { rank: num(me.rank) })}</span>
              <Move row={me} />
            </span>
            <span className={"lb-bar-mark" + (me.saved ? "" : " is-unsaved")}>{me.saved ? t("game.players.you") : t("game.score.notSaved")}</span>
          </span>
          <button type="button" className="sb-join lb-bar-act" onClick={me.saved ? onPlay : onSave}>
            {me.saved ? t("connect.boards.play") : t("game.summary.save")}
          </button>
        </div>
      )}
    </div>
  );
});

// the places not drawn: one row as tall as they are
function Spacer({ rows, rowH }: { rows: number; rowH: number }) {
  return (
    <tr className="lb-spacer" aria-hidden="true">
      <td colSpan={7} style={{ height: rows * rowH }} />
    </tr>
  );
}

interface DetailProps {
  row: BoardRow;
  youColor: string | null;
  mine: MyPlaces | null;
  myPlace: MyPlace | null;
  sort: BoardSort;
  window: BoardWindow;
  onSave(): void;
  onPlay(): void;
}

// what passing the next player takes, in the sort's own terms
function toNextWords(place: MyPlace, sort: BoardSort): string | null {
  if (!place.next || place.toNext === null) return null;
  if (sort === "kd") return t("game.note.toNextKd", { n: place.toNext, name: place.next });
  if (sort === "wins") return t("game.note.toNextWins", { n: place.toNext, name: place.next });
  return t("game.note.toNext", { n: place.toNext, name: place.next });
}

function Detail({ row, youColor, mine, myPlace, sort, window: w, onSave, onPlay }: DetailProps) {
  const next = myPlace ? (myPlace.rank === 1 ? t("game.note.first") : toNextWords(myPlace, sort)) : null;
  return (
    <>
      <div className="lb-who">
        {(row.you ? youColor ?? row.color : row.color) && <Swatch color={row.you ? youColor ?? row.color : row.color} />}
        <div className="lb-who-text">
          <h3 className="lb-name">{row.name}</h3>
          <p className="lb-facts">
            <span className="lb-facts-rank">{t("game.note.rank", { rank: num(row.rank) })}</span>
            <Move row={row} />
            {row.handle ? <span className="lb-facts-quiet">@{row.handle}</span> : <span className="lb-facts-unsaved">{t("game.score.notSaved")}</span>}
          </p>
        </div>
      </div>
      {next && (
        <p className="lb-next" role="status">
          {next}
        </p>
      )}
      {mine && (
        <div className="lb-places">
          {WINDOWS.map((x) => {
            const p = mine[x.mine];
            return (
              <div key={x.id} className={"lb-place" + (x.id === w ? " is-on" : "")}>
                <span className="lb-place-k">{t(x.title)}</span>
                <span className="lb-place-n">{p ? t("game.note.rank", { rank: num(p.rank) }) : "–"}</span>
              </div>
            );
          })}
        </div>
      )}
      <dl className="lb-rows">
        <div>
          <dt>{t("game.score.kills")}</dt>
          <dd>{num(row.kills)}</dd>
        </div>
        <div>
          <dt>{t("game.score.deaths")}</dt>
          <dd>{num(row.deaths)}</dd>
        </div>
        <div>
          <dt>{t("game.score.kd")}</dt>
          <dd>{kdText(row.kd)}</dd>
        </div>
        <div>
          <dt>{t("game.score.wins")}</dt>
          <dd>{num(row.wins)}</dd>
        </div>
      </dl>
      {row.you && !row.saved && (
        <>
          <p className="lb-say">{t("game.score.guestKept")}</p>
          <button type="button" className="sb-join lb-act" onClick={onSave}>
            {t("game.summary.save")}
          </button>
        </>
      )}
      {row.you && row.saved && (
        <button type="button" className="sb-join lb-act" onClick={onPlay}>
          {t("connect.boards.play")}
        </button>
      )}
    </>
  );
}
