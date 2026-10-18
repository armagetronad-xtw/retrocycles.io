// The bell (features.news): the status bar's cell for the news staff send to every
// player, before the settings cell. Its count is the posts sent since the player last opened it
// (ink, like the friends cell's: news never waits on anyone, so not the waiting amber). One
// activation hangs the news panel under the bar at its right edge, the friends panel's idiom at
// the sheets' density: NEWS and ALL NEWS over a hairline, then one row per post, newest first.
// A row is a link to the post on the site's /news page in a new tab (outside Discord, its own
// confirmation); unread ones are ink 600 with NEW, read ones ink-rest. Opening reads them all
// (the count goes at once; the NEW marks stay until the panel is put away). The panel takes the
// page's keys (data-aa-keys): the arrows move the Focus Bar, Enter follows, Esc, the cell again or
// a press elsewhere close it and give the focus back to the cell. For a screen reader the panel is a
// dialog, each row is named by its title (NEW first when unread) and described by its age, its
// summary and the new tab; a count that grows while the page is open is said once (polite).
import { forwardRef, useEffect, useRef, useState, type KeyboardEvent as ReactKeyboardEvent } from "react";
import { Icon } from "./Icons";
import { ago, t } from "../i18n";
import { DISCORD } from "../links";
import { openOutside } from "../discord";
import type { NewsState } from "../platform/news";

interface NewsCellProps {
  /** null until the news is in */
  unread: number | null;
  open: boolean;
  disabled: boolean;
  onToggle(): void;
}

export const NewsCell = forwardRef<HTMLButtonElement, NewsCellProps>(function NewsCell({ unread: known, open, disabled, onToggle }, ref) {
  const unread = known ?? 0;
  const label = unread > 0 ? t("home.news.cellUnread", { n: unread }) : t("home.news.cell");
  // the first count read is the baseline; only a later rise is said
  const seen = useRef<number | null>(null);
  const [said, setSaid] = useState("");
  useEffect(() => {
    if (known === null) return;
    if (seen.current !== null && known > seen.current) setSaid(t("home.news.cellUnread", { n: known }));
    seen.current = known;
  }, [known]);
  return (
    <>
      <button
        className={"status-cell status-icon news-cell" + (unread > 0 ? " has-new" : "")}
        id="news-cell"
        type="button"
        ref={ref}
        disabled={disabled}
        aria-haspopup="dialog"
        aria-controls="news-panel"
        aria-expanded={open}
        aria-label={label}
        title={label}
        onClick={onToggle}
      >
        <Icon name="bell" className="status-glyph" />
        {unread > 0 && (
          <span className="status-count" id="news-count">
            {unread > 9 ? "9+" : unread}
          </span>
        )}
      </button>
      <span className="vh" role="status">
        {said}
      </span>
    </>
  );
});

interface NewsPanelProps {
  news: NewsState;
  cell: HTMLButtonElement | null;
  onClose(focusCell: boolean): void;
}

function follow(e: { preventDefault(): void }, url: string) {
  if (!DISCORD) return; // a plain link: the browser opens the new tab
  e.preventDefault();
  openOutside(url);
}

export function NewsPanel({ news, cell, onClose }: NewsPanelProps) {
  const panel = useRef<HTMLElement | null>(null);

  // the focus goes in with the panel: its first row (else ALL NEWS)
  useEffect(() => {
    const el = panel.current;
    (el?.querySelector<HTMLElement>(".news-row") ?? el?.querySelector<HTMLElement>("a"))?.focus({ preventScroll: true });
  }, [news.status]);

  // a press anywhere else puts it away (the cell's own press toggles it there)
  useEffect(() => {
    const down = (e: PointerEvent) => {
      const target = e.target as Node | null;
      if (!target || panel.current?.contains(target) || cell?.contains(target)) return;
      onClose(false);
    };
    window.addEventListener("pointerdown", down, { capture: true });
    return () => window.removeEventListener("pointerdown", down, { capture: true });
  }, [cell, onClose]);

  const keys = (e: ReactKeyboardEvent<HTMLElement>) => {
    const el = panel.current;
    if (!el) return;
    const links = [...el.querySelectorAll<HTMLElement>("a")];
    const at = links.indexOf(document.activeElement as HTMLElement);
    if (e.key === "Escape") {
      e.preventDefault();
      e.stopPropagation();
      onClose(true);
    } else if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      e.stopPropagation();
      const next = e.key === "ArrowDown" ? (at + 1) % links.length : (at - 1 + links.length) % links.length;
      links[at < 0 ? 0 : next]?.focus({ preventScroll: true });
    } else if (e.key === "Tab") {
      // Tab keeps to the panel, as in the sheets
      e.preventDefault();
      e.stopPropagation();
      const next = e.shiftKey ? (at - 1 + links.length) % links.length : (at + 1) % links.length;
      links[at < 0 ? 0 : next]?.focus({ preventScroll: true });
    } else if (e.key === "Enter") {
      // the page's handler would click it too; once is enough
      e.stopPropagation();
    }
  };

  const all = news.status === "ready" ? news.allUrl : "/news";
  return (
    <section className="news-panel" id="news-panel" ref={panel} role="dialog" aria-labelledby="news-title" data-aa-keys="page" onKeyDown={keys}>
      <header className="news-head">
        <h2 className="news-title" id="news-title">
          {t("home.news.title")}
        </h2>
        <a className="news-all" href={all} target="_blank" rel="noopener" onClick={(e) => follow(e, all)}>
          {t("home.news.all")}
          <Icon name="external" className="news-out" />
        </a>
      </header>
      {news.status === "ready" && news.items.length > 0 && (
        <ul className="news-list">
          {news.items.map((item) => {
            const id = `news-${item.id}`;
            return (
              <li key={item.id}>
                <a
                  className={"news-row" + (item.unread ? " is-new" : "")}
                  href={item.url}
                  target="_blank"
                  rel="noopener"
                  aria-labelledby={(item.unread ? `${id}-new ` : "") + `${id}-title`}
                  aria-describedby={`${id}-when ` + (item.summary ? `${id}-say ` : "") + "news-opens"}
                  onClick={(e) => {
                    follow(e, item.url);
                    onClose(false);
                  }}
                >
                  <span className="news-row-top">
                    {item.unread && (
                      <span className="news-new" id={`${id}-new`}>
                        {t("home.news.new")}
                      </span>
                    )}
                    <span className="news-when" id={`${id}-when`}>
                      {when(item.sentAt)}
                    </span>
                    <Icon name="external" className="news-row-out" />
                  </span>
                  <span className="news-row-title" id={`${id}-title`}>
                    {item.title}
                  </span>
                  {item.summary && (
                    <span className="news-row-say" id={`${id}-say`}>
                      {item.summary}
                    </span>
                  )}
                </a>
              </li>
            );
          })}
        </ul>
      )}
      {news.status === "ready" && news.items.length === 0 && <p className="news-say">{t("home.news.empty")}</p>}
      {news.status === "loading" && <p className="news-say">{t("home.news.asking")}</p>}
      {news.status === "failed" && <p className="news-say">{t("home.news.failed")}</p>}
      <span className="news-opens" id="news-opens" hidden>
        {t("home.news.opens")}
      </span>
    </section>
  );
}

function when(iso: string): string {
  const at = Date.parse(iso);
  return Number.isFinite(at) ? ago(Math.floor(at / 1000)) : "";
}
