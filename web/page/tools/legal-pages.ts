// The trust pages (about, code of conduct, privacy, terms, age) as static
// documents: web/page/<doc>/index.html are extra Vite HTML entries (dist/<doc>/index.html,
// served at /<doc>/), plain HTML and CSS, no script, no wasm. Before Vite processes them
// this plugin fills them in from src/legal/config.ts and the build:
//
//   <!--rc:head-->          charset, viewport, the documents' stylesheet, noindex while draft
//   <!--rc:top-->           the mark (back to the game) and the "play" line
//   <!--rc:draft-->         the "Draft - not yet reviewed" cell (legal documents, LEGAL.draft)
//   <!--rc:foot-->          the documents' navigation and the licence / build line
//   {{key}}                 a config or build value (escaped); missing -> a placeholder cell
//   {{key:mail}}            an email value as a mailto link
//   {{todo:words}}          always a placeholder cell: a decision the operator must make
//   <!--rc:if flag-->A<!--rc:else-->B<!--rc:end-->   (flags: sourcePublic, repoWithStars,
//                           repoWithoutStars, sourceOffer, sourceArchive, ageConfirmation,
//                           commitKnown); not nested
//
// The document's id is its <body data-doc="...">. Anything left unfilled ({{ or <!--rc:)
// fails the build. The licence texts (COPYING.txt, AUTHORS at the repository root) are
// emitted as /licenses/COPYING.txt and /licenses/AUTHORS.txt (served by the dev server too).
import { execSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { Plugin } from "vite";
import { DOCS, LEGAL, type LegalConfig } from "../src/legal/config.ts";

const PAGE_ROOT = fileURLToPath(new URL("..", import.meta.url));
const REPO_ROOT = resolve(PAGE_ROOT, "../..");

const LICENSE_FILES: Record<string, string> = {
  "licenses/COPYING.txt": resolve(REPO_ROOT, "COPYING.txt"),
  "licenses/AUTHORS.txt": resolve(REPO_ROOT, "AUTHORS")
};

// what a missing value is called in its placeholder cell
const LABELS: Partial<Record<keyof LegalConfig | string, string>> = {
  operator: "operator name",
  contactEmail: "contact email",
  conductEmail: "conduct email",
  postalAddress: "postal address",
  governingLaw: "governing law and courts",
  effectiveDate: "effective date",
  emailProvider: "email provider",
  guestRetention: "guest retention period",
  auditRetention: "audit log retention period",
  serverLogRetention: "game server log retention period",
  sourceRepoUrl: "public repository URL"
};

export interface BuildFacts {
  commit: string;
  buildDate: string;
  // the public repository's stars, read from GitHub when the bundle is built (RC_REPO_STARS
  // overrides it); null: not known, and About shows the link without a count
  stars: number | null;
}

function gitCommit(): string | null {
  try {
    const head = execSync("git rev-parse HEAD", { cwd: PAGE_ROOT, stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
    if (!/^[0-9a-f]{40}$/.test(head)) return null;
    const dirty = execSync("git status --porcelain", { cwd: PAGE_ROOT, stdio: ["ignore", "pipe", "ignore"] }).toString().trim();
    return dirty ? head + "-dirty" : head;
  } catch {
    return null;
  }
}

export function buildFacts(): BuildFacts {
  const env = (process.env.RC_SOURCE_COMMIT || "").trim();
  const commit = /^[0-9a-f]{7,40}(-dirty)?$/.test(env) ? env : gitCommit() || "unknown";
  return { commit, buildDate: new Date().toISOString().slice(0, 10), stars: null };
}

function repoUrl(): string | null {
  return (process.env.RC_SOURCE_REPO_URL || "").trim() || LEGAL.sourceRepoUrl;
}

// "owner/name" of a GitHub repository URL, else null
function githubRepo(url: string | null): string | null {
  const m = url ? /^https:\/\/github\.com\/([\w.-]+\/[\w.-]+?)\/?$/.exec(url) : null;
  return m ? m[1] : null;
}

async function repoStars(): Promise<number | null> {
  const env = (process.env.RC_REPO_STARS ?? "").trim();
  if (/^\d+$/.test(env)) return Number(env);
  const repo = githubRepo(repoUrl());
  if (!repo) return null;
  try {
    const res = await fetch(`https://api.github.com/repos/${repo}`, {
      headers: { Accept: "application/vnd.github+json" },
      signal: AbortSignal.timeout(5000)
    });
    if (!res.ok) return null;
    const body = (await res.json()) as { stargazers_count?: unknown };
    return typeof body.stargazers_count === "number" ? body.stargazers_count : null;
  } catch {
    return null;
  }
}

function esc(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function cell(text: string, title = "placeholder: to be filled in"): string {
  return `<span class="ph" title="${esc(title)}">${esc(text)}</span>`;
}

interface Values {
  text: Record<string, string | null>;
  pending: Set<string>;
  flags: Record<string, boolean>;
}

function values(facts: BuildFacts): Values {
  const repo = repoUrl();
  const commitKnown = facts.commit !== "unknown";
  const pureCommit = facts.commit.replace(/-dirty$/, "");
  const sourceUrl = repo && commitKnown ? LEGAL.sourceCommitUrl.replace("{repo}", repo.replace(/\/$/, "")).replace("{commit}", pureCommit) : null;
  const archive = LEGAL.sourceArchiveUrl && commitKnown ? LEGAL.sourceArchiveUrl.replace("{commit}", pureCommit) : null;
  const text: Record<string, string | null> = {
    operator: LEGAL.operator,
    contactEmail: LEGAL.contactEmail,
    conductEmail: LEGAL.conductEmail ?? LEGAL.contactEmail,
    postalAddress: LEGAL.postalAddress,
    governingLaw: LEGAL.governingLaw,
    effectiveDate: LEGAL.effectiveDate,
    minimumAge: String(LEGAL.minimumAge),
    emailProvider: LEGAL.emailProvider,
    guestRetention: LEGAL.guestRetention,
    auditRetention: LEGAL.auditRetention,
    serverLogRetention: LEGAL.serverLogRetention,
    sourceRepoUrl: repo,
    sourceRepoName: repo ? githubRepo(repo) ?? repo.replace(/^https?:\/\//, "") : null,
    repoStars: facts.stars === null ? null : String(facts.stars),
    sourceUrl,
    sourceArchiveUrl: archive,
    siteHost: LEGAL.siteHost,
    commit: facts.commit,
    commitShort: facts.commit === "unknown" ? "unknown" : pureCommit.slice(0, 12) + (facts.commit.endsWith("-dirty") ? "-dirty" : ""),
    buildDate: facts.buildDate
  };
  return {
    text,
    pending: new Set(LEGAL.pending as string[]),
    flags: {
      sourcePublic: !!sourceUrl,
      repoWithStars: !!repo && facts.stars !== null,
      repoWithoutStars: !!repo && facts.stars === null,
      sourceOffer: !repo,
      sourceArchive: !!archive,
      ageConfirmation: LEGAL.requireAgeConfirmation,
      commitKnown,
      draft: LEGAL.draft
    }
  };
}

function token(v: Values, key: string, mode: string | undefined): string {
  if (key === "todo") return cell("[" + (mode || "to be decided") + "]", "placeholder: a decision for the operator");
  if (!(key in v.text)) throw new Error(`legal-pages: unknown token {{${key}}}`);
  const value = v.text[key];
  if (value === null || value === "") return cell("[" + (LABELS[key] || key) + "]");
  if (mode === "mail") {
    const link = `<a href="mailto:${esc(value)}">${esc(value)}</a>`;
    return v.pending.has(key) ? `<span class="ph" title="placeholder: a default awaiting a decision">${link}</span>` : link;
  }
  if (mode === "link") return `<a href="${esc(value)}">${esc(value)}</a>`;
  if (mode) throw new Error(`legal-pages: unknown mode {{${key}:${mode}}}`);
  return v.pending.has(key) ? cell(value, "placeholder: a default awaiting a decision") : esc(value);
}

function head(doc: string): string {
  const info = DOCS.find((d) => d.id === doc)!;
  return [
    '<meta charset="utf-8">',
    '<meta name="viewport" content="width=device-width, initial-scale=1">',
    '<meta name="color-scheme" content="dark">',
    LEGAL.draft && info.legal ? '<meta name="robots" content="noindex">' : "",
    '<link rel="stylesheet" href="/src/legal/docs.css">'
  ]
    .filter(Boolean)
    .join("\n");
}

function top(): string {
  return [
    '<header class="doc-top">',
    '<a class="doc-mark" href="../">Retrocycles</a>',
    '<a class="doc-play" href="../">play</a>',
    "</header>"
  ].join("\n");
}

function draft(doc: string): string {
  const info = DOCS.find((d) => d.id === doc)!;
  if (!LEGAL.draft || !info.legal) return "";
  return '<p class="doc-draft" id="doc-draft">Draft - not yet reviewed</p>';
}

function foot(doc: string, v: Values): string {
  const links = DOCS.map((d) =>
    d.id === doc
      ? `<a href="../${d.id}/" aria-current="page">${esc(d.title)}</a>`
      : `<a href="../${d.id}/">${esc(d.title)}</a>`
  ).join("\n");
  const build = `<a href="../about/#source">Source code</a>.`;
  return [
    '<footer class="doc-foot">',
    '<nav class="doc-nav" aria-label="Retrocycles.io documents">',
    links,
    "</nav>",
    `<p class="doc-licence">Armagetron Advanced is free software under the <a href="../licenses/COPYING.txt">GNU General Public License</a>, version 2 or later. ${build}</p>`,
    "</footer>"
  ].join("\n");
}

export function renderDoc(html: string, facts: BuildFacts): string {
  const m = /<body[^>]*\sdata-doc="([a-z]+)"/.exec(html);
  if (!m) return html;
  const doc = m[1];
  if (!DOCS.some((d) => d.id === doc)) throw new Error(`legal-pages: unknown document ${doc}`);
  const v = values(facts);
  let out = html.replace(/<!--rc:if ([a-zA-Z]+)-->([\s\S]*?)(?:<!--rc:else-->([\s\S]*?))?<!--rc:end-->/g, (_all, flag: string, yes: string, no?: string) => {
    if (!(flag in v.flags)) throw new Error(`legal-pages: unknown flag ${flag}`);
    return v.flags[flag] ? yes : no ?? "";
  });
  out = out
    .replace("<!--rc:head-->", head(doc))
    .replace("<!--rc:top-->", top())
    .replace("<!--rc:draft-->", draft(doc))
    .replace("<!--rc:foot-->", foot(doc, v))
    .replace(/\{\{([a-zA-Z]+)(?::([^}]+))?\}\}/g, (_all, key: string, mode?: string) => token(v, key, mode));
  const left = /\{\{|<!--rc:/.exec(out);
  if (left) throw new Error(`legal-pages: unfilled template in ${doc}: ${out.slice(left.index, left.index + 60)}`);
  return out;
}

// the documents as Rollup inputs (beside the play page's index.html)
export function docInputs(): Record<string, string> {
  return Object.fromEntries(DOCS.map((d) => [d.id, resolve(PAGE_ROOT, d.id, "index.html")]));
}

export function legalPages(): Plugin {
  const facts = buildFacts();
  return {
    name: "retrocycles-legal-pages",
    async buildStart() {
      facts.stars = await repoStars();
      this.info("repository stars " + (facts.stars ?? "(unknown)"));
    },
    transformIndexHtml: {
      order: "pre",
      handler: (html) => renderDoc(html, facts)
    },
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const path = (req.url || "").split("?")[0].replace(/^\//, "");
        const file = LICENSE_FILES[path];
        if (!file) return next();
        res.setHeader("Content-Type", "text/plain; charset=utf-8");
        res.end(readFileSync(file));
      });
    },
    generateBundle() {
      for (const [fileName, file] of Object.entries(LICENSE_FILES)) {
        this.emitFile({ type: "asset", fileName, source: readFileSync(file) });
      }
    }
  };
}
