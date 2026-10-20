// client_error `where` (analytics.ts): which of the page's own files an error came from, as a fixed
// code, never the file name itself. null: not one of ours (an extension, a Google tag), so not sent.
//   page:   the page's own bundle (assets/*: React, the menus, the platform calls)
//   api:    aa_api.js, the bridge between the page and the client
//   client: the game client (client_opt.js / client.js, its wasm)
//   other:  another file of ours (auth, forward pages)
//   unknown: a promise rejection with no stack to tell (a rejected plain value)

export type ErrorWhere = "page" | "api" | "client" | "other" | "unknown";

/** The code for one script URL, or null when the URL is not on `origin`. */
export function whereOfFile(file: string | undefined | null, origin: string): ErrorWhere | null {
  if (!file || !file.startsWith(origin + "/")) return null;
  const path = file.slice(origin.length).split(/[?#]/)[0];
  if (/\/assets\/[^/]+\.js$/.test(path)) return "page";
  if (/\/aa_api\.js$/.test(path)) return "api";
  if (/\/client(_opt)?\.(js|wasm)$/.test(path)) return "client";
  return "other";
}

/** The code for a stack: its top frame with a URL. Frames on another origin (or a wasm:// frame
 *  below one of ours) are passed over until a URL of ours is found; a stack whose URLs are all
 *  elsewhere is not ours (null); no URL at all: unknown. */
export function whereOfStack(stack: string | undefined | null, origin: string): ErrorWhere | null {
  const urls = (stack ?? "").match(/(https?|wasm):\/\/[^\s)]+/g);
  if (!urls) return "unknown";
  for (const u of urls) {
    // a frame is "url:line:col": the file is what comes before
    const where = whereOfFile(u.replace(/(:\d+)+$/, ""), origin);
    if (where) return where;
  }
  return null;
}
