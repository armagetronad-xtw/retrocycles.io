// Loads the wasm client the way web/wasm/client.html does, as plain scripts from
// the page's own origin (they are not modules of this app; see vite.config.ts):
//
//   1. build the Emscripten Module object (canvas, --userdatadir, print hooks)
//   2. <script src="aa_api.js">, then AA.prepare(Module, {resourceProxy})
//   3. <script src="client_opt.js"> (or client.js with ?build=debug)
//   4. resolve on the API's `ready` event
//
// `data-build` and `data-resource-proxy` on <html> carry the production values
// (index.html: "opt" and "/resource/", the deploy's caching CORS proxy). In
// development the proxy is dropped (direct fetches, like the dev client.html),
// because the Vite dev server has no /resource/ route.
import type { Aa, AaStatic, EmscriptenModule } from "./types";
import { clientError } from "../platform/analytics";

export interface LoadHooks {
  // fraction 0..1 of the preloaded data package, null while unknown
  onProgress(fraction: number | null): void;
  // every line the runtime prints (BROWSER: ..., PAGE: ...)
  onLog(text: string): void;
  // the API object exists (before the client script runs): its events can be heard from here
  onPrepared?(aa: Aa): void;
}

function loadScript(src: string): Promise<void> {
  return new Promise((resolve, reject) => {
    const script = document.createElement("script");
    script.src = src;
    script.async = false;
    script.onload = () => resolve();
    script.onerror = () => reject(new Error("could not load " + src));
    document.body.appendChild(script);
  });
}

// the resource proxy base in effect: "" means direct fetches (API.md "fetching")
export function resourceProxy(): string {
  if (import.meta.env.DEV) return "";
  return document.documentElement.getAttribute("data-resource-proxy") || "";
}

// "opt" (client_opt.js, the dist bundle) or "debug" (client.js), ?build= overrides
export function buildName(): "opt" | "debug" {
  const query = new URLSearchParams(location.search).get("build");
  const html = document.documentElement.dataset.build;
  return (query || html) === "debug" ? "debug" : "opt";
}

export async function loadClient(canvas: HTMLCanvasElement, hooks: LoadHooks): Promise<Aa> {
  const log = (text: string) => {
    console.log(text);
    hooks.onLog(text);
  };

  const Module: EmscriptenModule = {
    // --userdatadir /aa/user: var/ is the IDBFS mount aa_api.js sets up
    arguments: ["--userdatadir", "/aa/user"],
    canvas,
    print: log,
    printErr: log,
    // "Downloading data... (loaded/total)" while the preloaded package streams in
    setStatus(text: string) {
      const m = /\((\d+)\/(\d+)\)/.exec(text);
      if (m && Number(m[2]) > 0) hooks.onProgress(Number(m[1]) / Number(m[2]));
      else if (!text) hooks.onProgress(1);
    },
    onExit(code: number) {
      log("BROWSER: exit " + code);
    },
    onAbort(what: unknown) {
      log("BROWSER: abort " + String(what));
      clientError(/memory/i.test(String(what)) ? "out_of_memory" : "wasm_abort");
    },
    onRuntimeInitialized() {
      log("BROWSER: runtime initialized");
    }
  };
  // the release's client version (vite.config.ts tagClientUrls): on every client URL, so a
  // phone never keeps running an old copy after a deploy; the same URLs as the preloads
  const version = document.documentElement.dataset.client;
  const tag = version ? "?v=" + encodeURIComponent(version) : "";
  Module.locateFile = (path: string, prefix: string) => prefix + path + (/^client(_opt)?\.(wasm|data)$/.test(path) ? tag : "");
  window.Module = Module;

  await loadScript("./aa_api.js" + tag);
  const AA = window.AA as AaStatic | undefined;
  if (!AA) throw new Error("aa_api.js did not define AA");

  const aa = AA.prepare(Module, { resourceProxy: resourceProxy() });
  hooks.onPrepared?.(aa);
  log("PAGE: resource proxy: " + (resourceProxyOf(aa) || "(none, direct fetch)"));

  const ready = new Promise<Aa>((resolve, reject) => {
    aa.on("ready", () => resolve(aa));
    aa.on("exit", (data: { code: number }) => reject(new Error("runtime exited with " + data.code)));
  });

  const src = buildName() === "debug" ? "./client.js" : "./client_opt.js";
  log("BROWSER: loading " + src);
  await loadScript(src + tag);
  return ready;
}

function resourceProxyOf(aa: Aa): string {
  const withProxy = aa as Aa & { getResourceProxy?: () => string };
  return withProxy.getResourceProxy ? withProxy.getResourceProxy() : "";
}
