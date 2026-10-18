// Vite configuration of the play page.
//
// The wasm client (client_opt.js / .wasm / .data) and its wrapper aa_api.js are
// NOT modules of this app: the Emscripten build produces them and `make dist`
// (web/wasm/Makefile) copies them into web/dist/, the static bundle Caddy
// serves. The page loads them at runtime as plain scripts from its own origin
// (src/aa/loader.ts). In development the dev server below serves those files
// straight out of ../dist, so nothing is copied and a rebuilt bundle is picked
// up on the next reload. `publicDir` stays the app's own (fonts).
//
// Beside the play page (index.html) the build has one more HTML entry per trust
// document (about/, conduct/, privacy/, terms/, age/: static pages with no
// script and no wasm), filled in from src/legal/config.ts by tools/legal-pages.ts. The share
// cards' tags (index.html's <!--rc:social-->) and the /play/ link are tools/social.ts.
//
// The client's files keep their names from one release to the next, so a phone could run an old
// copy after a deploy (seen on an iPhone, though they are served no-cache): the build
// tags their URLs with a hash of the files (clientVersion below), on the preloads and as
// <html data-client>, which the loader appends to every one (src/aa/loader.ts).
import { defineConfig, type Plugin } from "vite";
import react from "@vitejs/plugin-react";
import { createHash } from "node:crypto";
import { createReadStream, existsSync, readFileSync, statSync } from "node:fs";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { docInputs, legalPages } from "./tools/legal-pages.ts";
import { socialCards } from "./tools/social.ts";

const WASM_DIST = fileURLToPath(new URL("../dist", import.meta.url));

// the files the page pulls from web/dist at runtime, and what they are served as
const WASM_FILES: Record<string, string> = {
  "/aa_api.js": "text/javascript",
  "/client_opt.js": "text/javascript",
  "/client_opt.wasm": "application/wasm",
  "/client_opt.data": "application/octet-stream",
  "/client.js": "text/javascript",
  "/client.wasm": "application/wasm",
  "/client.data": "application/octet-stream",
  "/servers.json": "application/json",
  // optional (web/page/src/platform): absent -> 404 -> community mode
  "/platform.json": "application/json",
  "/client.html": "text/html"
};

// a hash of the client files the release ships: from RC_CLIENT_DIR (the release's client files,
// web/deploy) or else web/dist; none there, no tag (the URLs stay bare)
const CLIENT_FILES = ["aa_api.js", "client_opt.js", "client_opt.wasm", "client_opt.data"];

function clientVersion(): string {
  const dir = (process.env.RC_CLIENT_DIR || "").trim() || WASM_DIST;
  const files = CLIENT_FILES.map((f) => resolve(dir, f)).filter((f) => existsSync(f));
  if (files.length === 0) return "";
  const hash = createHash("sha256");
  for (const f of files) hash.update(readFileSync(f));
  return hash.digest("hex").slice(0, 12);
}

function tagClientUrls(): Plugin {
  let version = "";
  return {
    name: "retrocycles-client-version",
    apply: "build",
    buildStart() {
      version = clientVersion();
      this.info("client version " + (version || "(none: bare URLs)"));
    },
    transformIndexHtml(html) {
      if (!version) return html;
      return html
        .replace(/href="\.\/(aa_api\.js|client_opt\.(?:js|wasm|data))"/g, (_m, f) => `href="./${f}?v=${version}"`)
        .replace(/<html /, `<html data-client="${version}" `);
    }
  };
}

function serveWasmDist(): Plugin {
  return {
    name: "retrocycles-serve-wasm-dist",
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const path = (req.url || "").split("?")[0];
        const type = WASM_FILES[path];
        if (!type) return next();
        const file = resolve(WASM_DIST, "." + path);
        if (!existsSync(file)) {
          res.statusCode = 404;
          res.setHeader("Content-Type", "text/plain");
          res.end(path + " is not in web/dist; run `make dist` in web/wasm first");
          return;
        }
        res.setHeader("Content-Type", type);
        res.setHeader("Content-Length", String(statSync(file).size));
        res.setHeader("Cache-Control", "no-cache");
        createReadStream(file).pipe(res);
      });
    }
  };
}

export default defineConfig({
  plugins: [react(), serveWasmDist(), tagClientUrls(), legalPages(), socialCards()],
  // the page lives at the bundle's root, but relative URLs keep it movable
  base: "./",
  build: {
    outDir: "dist",
    emptyOutDir: true,
    // the play page and the documents: no manual chunks, hashed assets under assets/
    assetsDir: "assets",
    sourcemap: false,
    rollupOptions: {
      input: { index: fileURLToPath(new URL("./index.html", import.meta.url)), ...docInputs() }
    }
  },
  server: {
    port: 5173,
    strictPort: false
  }
});
