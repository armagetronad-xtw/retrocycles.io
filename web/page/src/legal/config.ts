// The one place for what the operator of a Retrocycles.io deployment must fill in or
// decide before the trust pages (about, code of conduct, privacy, terms, age)
// are final. Read at build time by tools/legal-pages.ts, which writes the static
// documents (web/page/<doc>/index.html -> dist/<doc>/index.html), and by the play page
// (the sign-up's consent line and the optional age confirmation).
//
// A value left `null` is shown on the documents as a placeholder: a reversed cell
// naming what is missing ("[operator name]"). A value listed in `pending` is shown, but
// still marked as a placeholder, because it is a default awaiting a decision (the
// minimum age is written for 13+ until the operator decides).
//
// Two values come from the build, not from here (tools/legal-pages.ts):
//   RC_SOURCE_COMMIT    the commit the bundle was built from (default: `git rev-parse
//                       HEAD` when git is available, else "unknown")
//   RC_SOURCE_REPO_URL  overrides `sourceRepoUrl` below

export interface LegalConfig {
  // the "Draft - not yet reviewed" cell on the legal documents (privacy, terms and age)
  // and `noindex` on them; false once a lawyer has read them
  draft: boolean;

  // who runs the service: the legal entity (or person) and how to reach it
  operator: string | null;
  contactEmail: string | null; // privacy, legal, account deletion, source code requests
  conductEmail: string | null; // reports and appeals (code of conduct); falls back to contactEmail
  postalAddress: string | null;
  governingLaw: string | null; // e.g. "the laws of England and Wales" / "the State of New York, USA"
  effectiveDate: string | null; // e.g. "1 November 2026"

  // the age policy (age page, terms, privacy, sign-up)
  minimumAge: number;
  // ask "I am <minimumAge> or older" at sign-up (a checkbox the form requires); off until
  // the age policy says it is needed
  requireAgeConfirmation: boolean;


  // the processors and retention periods the platform does not fix itself
  emailProvider: string | null; // the service that sends account emails
  guestRetention: string | null; // when unused guests are deleted (not built yet)
  auditRetention: string | null; // how long security (audit) logs are kept (not built yet)
  serverLogRetention: string | null; // how long official rooms keep their own logs

  // the public repository of the browser port (the GPL source offer). null while it is
  // private: the About page then makes the written offer (source by email) instead of a
  // link. `sourceCommitUrl` turns repository + commit into a link to that exact tree.
  sourceRepoUrl: string | null;
  sourceCommitUrl: string; // "{repo}" and "{commit}" are replaced
  // optional: an archive of the exact source served next to the bundle (e.g.
  // "/source/retrocycles-{commit}.tar.gz"); null: none
  sourceArchiveUrl: string | null;

  // the host the documents name ("retrocycles.io")
  siteHost: string;

  // defaults still awaiting the operator's decision: shown, but marked as placeholders
  pending: Array<keyof LegalConfig>;
}

export const LEGAL: LegalConfig = {
  draft: false,

  operator: "Velvet Innovations LLC",
  contactEmail: "support@retrocycles.io",
  conductEmail: "support@retrocycles.io",
  postalAddress: "30 N Gould St, Ste R, Sheridan, WY 82801, USA",
  governingLaw: "the laws of the State of Wyoming, USA",
  effectiveDate: "17 October 2026",

  minimumAge: 13,
  requireAgeConfirmation: false,


  emailProvider: "Resend",
  guestRetention: "12 months after the guest's last game",
  auditRetention: "for 12 months",
  serverLogRetention: "for up to five weeks",

  sourceRepoUrl: null,
  sourceCommitUrl: "{repo}/tree/{commit}",
  sourceArchiveUrl: null,

  siteHost: "retrocycles.io",

  pending: []
};

// the documents, in the order their navigation lists them; `legal` ones carry the draft cell
export interface DocInfo {
  id: string;
  title: string;
  legal: boolean;
  // not built, linked or listed for now; its source stays for later
  hidden?: boolean;
}

export const ALL_DOCS: DocInfo[] = [
  { id: "about", title: "About", legal: false },
  { id: "conduct", title: "Code of conduct", legal: false },
  { id: "privacy", title: "Privacy", legal: true },
  { id: "terms", title: "Terms", legal: true },
  { id: "age", title: "Age", legal: true }
];

// the documents the site builds, links and lists
export const DOCS: DocInfo[] = ALL_DOCS.filter((d) => !d.hidden);
