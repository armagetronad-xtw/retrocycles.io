// The share cards: what a link to the page shows when it is posted. A site's crawler reads
// the page's HTML without running it, so the tags are written at build time:
//
//   <!--rc:social-->   in index.html: Open Graph (Discord, Slack, iMessage, ...) and the X
//                      card. The X card is a player card: X frames `<site>/?embed=1&c=x-card`
//                      in the post (480 x 480; src/links.ts, the deploy's frame-ancestors) and
//                      shows the square image until it is pressed, where it cannot frame.
//   play/index.html    emitted (and served by the dev server): the /play/ link, the same tags,
//                      on to /?play (Quick Play at once) by public/play/forward.js.
//   robots.txt,        emitted: for search engines (Google Search Console, Bing Webmaster
//   sitemap.xml,       Tools). sitemap.xml is an index of two: sitemap-pages.xml, the page and
//   sitemap-pages.xml  the documents (src/legal/config.ts DOCS), and /news/sitemap.xml, which
//                      the platform writes from the published posts. /play/ only forwards, and
//                      client.html is the API test page, kept out of search.
//
// The site is `RC_SITE_URL`, else https://<LEGAL.siteHost> (src/legal/config.ts). The images
// are public/share/ (NOTICE.txt there).
import type { Plugin } from "vite";
import { DOCS, LEGAL } from "../src/legal/config.ts";

const MARK = "<!--rc:social-->";

const TITLE = "Retrocycles: lightcycles in your browser";
const DESCRIPTION = "Free online lightcycle battles, right in your browser. No download, no sign-up: press play and ride.";

function site(): string {
  const env = process.env.RC_SITE_URL;
  return (env && /^https?:\/\//.test(env) ? env : "https://" + LEGAL.siteHost).replace(/\/+$/, "");
}

function attr(value: string): string {
  return value.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
}

// the tags for a page at `path` ("/" or "/play/")
export function socialTags(path: string): string {
  const base = site();
  const meta = (key: "name" | "property", id: string, content: string) => `<meta ${key}="${id}" content="${attr(content)}">`;
  return [
    meta("name", "description", DESCRIPTION),
    meta("property", "og:type", "website"),
    meta("property", "og:site_name", "Retrocycles"),
    meta("property", "og:title", TITLE),
    meta("property", "og:description", DESCRIPTION),
    meta("property", "og:url", base + path),
    meta("property", "og:image", base + "/share/card.jpg"),
    meta("property", "og:image:width", "1200"),
    meta("property", "og:image:height", "630"),
    meta("property", "og:image:alt", "Lightcycles racing in the Retrocycles arena"),
    meta("name", "twitter:card", "player"),
    meta("name", "twitter:title", TITLE),
    meta("name", "twitter:description", DESCRIPTION),
    meta("name", "twitter:image", base + "/share/square.jpg"),
    meta("name", "twitter:image:alt", "Lightcycles racing in the Retrocycles arena"),
    meta("name", "twitter:player", base + "/?embed=1&c=x-card"),
    meta("name", "twitter:player:width", "480"),
    meta("name", "twitter:player:height", "480")
  ].join("\n");
}

// /play/: the tags, then on to /?play (no inline script: the Content-Security-Policy)
export function playPage(): string {
  return [
    "<!doctype html>",
    "<!-- The /play/ link (tools/social.ts): the share card's tags for crawlers; a visitor goes on",
    "     to /?play, Quick Play at once (src/links.ts), by public/play/forward.js. -->",
    '<html lang="en">',
    "<head>",
    '<meta charset="utf-8">',
    '<meta name="color-scheme" content="dark">',
    "<title>Retrocycles</title>",
    socialTags("/play/"),
    '<link rel="canonical" href="' + attr(site() + "/play/") + '">',
    '<link rel="stylesheet" href="../plain.css">',
    '<script src="./forward.js"></script>',
    "</head>",
    "<body></body>",
    "</html>",
    ""
  ].join("\n");
}

export function socialCards(): Plugin {
  return {
    name: "retrocycles-social-cards",
    transformIndexHtml: {
      order: "pre",
      handler: (html) => (html.includes(MARK) ? html.replace(MARK, socialTags("/")) : html)
    },
    configureServer(server) {
      server.middlewares.use((req, res, next) => {
        const path = (req.url || "").split("?")[0];
        if (path !== "/play/" && path !== "/play/index.html") return next();
        res.setHeader("Content-Type", "text/html; charset=utf-8");
        res.end(playPage());
      });
    },
    generateBundle() {
      this.emitFile({ type: "asset", fileName: "play/index.html", source: playPage() });
      this.emitFile({ type: "asset", fileName: "robots.txt", source: robots() });
      this.emitFile({ type: "asset", fileName: "sitemap.xml", source: sitemapIndex() });
      this.emitFile({ type: "asset", fileName: "sitemap-pages.xml", source: sitemap() });
    }
  };
}

function robots(): string {
  return ["User-agent: *", "Disallow: /client.html", "", "Sitemap: " + site() + "/sitemap.xml", ""].join("\n");
}

// the platform's /news/sitemap.xml lists the news and its posts (Caddy sends /news/* there)
function sitemapIndex(): string {
  const maps = ["/sitemap-pages.xml", "/news/sitemap.xml"].map((path) => `  <sitemap><loc>${site()}${path}</loc></sitemap>`);
  return ['<?xml version="1.0" encoding="UTF-8"?>', '<sitemapindex xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">', ...maps, "</sitemapindex>", ""].join("\n");
}

function sitemap(): string {
  const urls = ["/", ...DOCS.map((d) => "/" + d.id + "/")].map((path) => `  <url><loc>${site()}${path}</loc></url>`);
  return ['<?xml version="1.0" encoding="UTF-8"?>', '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">', ...urls, "</urlset>", ""].join("\n");
}
