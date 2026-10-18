// The play page's ways to the trust documents (about/, conduct/, privacy/, terms/: static
// pages beside it, built by tools/legal-pages.ts). The documents line is a quiet row at
// the bottom right of the queue frame, so it is gone behind a stage and in the game. The
// conduct line is the code of conduct in one line over the game, the first time a player
// opens the chat in an official room (App.tsx); it takes no focus and no keys (the chat
// key belongs to the game), only its link takes the pointer, and it goes away on its own.
// Links from inside a session (the conduct line, the sign-up's consent) open a new tab so
// the game is not left; the documents line navigates (the game is not running then).
import type { ReactNode } from "react";
import { t, tx, type PlainKey } from "../i18n";
import { CommunityLink } from "./CommunityLink";
import { DISCORD } from "../links";
import { openOutside } from "../discord";
import { useNews } from "../platform/news";

export type DocId = "about" | "conduct" | "privacy" | "terms" | "age";

// relative to the play page (Vite's base "./"): the document's directory
export function docHref(id: DocId): string {
  return "./" + id + "/";
}

// a link to a document in a new tab (from the Profile or over the game)
export function DocLink({ id, children }: { id: DocId; children: ReactNode }) {
  return (
    <a href={docHref(id)} target="_blank" rel="noopener">
      {children}
    </a>
  );
}

const LINE: Array<{ id: DocId; label: PlainKey }> = [
  { id: "about", label: "home.docs.about" },
  { id: "conduct", label: "home.docs.conduct" },
  { id: "privacy", label: "home.docs.privacy" },
  { id: "terms", label: "home.docs.terms" }
];

export function DocsLine() {
  // the news page (features.news), the site's own like the documents; outside Discord from inside it
  const news = useNews();
  const newsUrl = news.status === "ready" ? news.allUrl : null;
  return (
    // the foot of the home screen: the documents, then The Grid in the corner (desktop); on an
    // upright phone one small centred line, The Grid as the Discord mark at its end (page.css)
    <div className="home-foot">
      <nav className="docs-line" id="docs-line" aria-label={t("home.docs.aria")}>
        {newsUrl && (
          <a
            className="docs-link"
            id="docs-news"
            href={newsUrl}
            onClick={(e) => {
              if (!DISCORD) return;
              e.preventDefault();
              openOutside(newsUrl);
            }}
          >
            {t("home.docs.news")}
          </a>
        )}
        {LINE.map((d) => (
          <a key={d.id} className="docs-link" id={"docs-" + d.id} href={docHref(d.id)}>
            {t(d.label)}
          </a>
        ))}
      </nav>
      <CommunityLink />
    </div>
  );
}

export function ConductLine() {
  return (
    <p className="conduct-line" id="conduct-line" role="status">
      {tx("home.docs.conductLine", { link: <DocLink id="conduct">{t("home.docs.codeOfConduct")}</DocLink> })}
    </p>
  );
}

// the conduct line is shown once per browser: a flag that says only "seen" (no id, no
// time), kept in localStorage; storage that is blocked or full simply shows it again
const SEEN_KEY = "rc.conductLineSeen";

export function conductLineSeen(): boolean {
  try {
    return localStorage.getItem(SEEN_KEY) === "1";
  } catch {
    return false;
  }
}

export function markConductLineSeen() {
  try {
    localStorage.setItem(SEEN_KEY, "1");
  } catch {
    // private mode or blocked storage: it may show once more next time
  }
}
