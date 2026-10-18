import { createRoot } from "react-dom/client";
import { App } from "./App";
import { startAnalytics, watchErrors } from "./platform/analytics";
import { startGoogleTag } from "./googleTag";
import "./styles/page.css";
import "./styles/first-minute.css";
import "./styles/chat.css";
import "./styles/profile.css";
import "./styles/dock.css";
import "./styles/news.css";
import "./styles/settings.css";
import "./styles/servers.css";
import "./styles/hud.css";
import "./styles/boards.css";
import "./styles/training.css";
import "./styles/embed.css";
import { DISCORD, EMBED, dropCampaignCodes } from "./links";
import { startScrollHide } from "./fullscreen";
import { discordSdk } from "./discord";
import { keepIsolation } from "./isolation";
import { initLang } from "./i18n";
import { wantedTitle } from "./alerts";

// the rc_out cookie per tab (isolation.ts): a tab served unisolated by another tab's cookie
// loads once more, and nothing starts meanwhile
if (!keepIsolation()) start();

function start() {
  // in a social post's frame: one PLAY over the scene (links.ts, components/Embed.tsx)
  if (EMBED) document.body.classList.add("is-embed");

  // a Discord Activity (src/discord.ts): the handshake with Discord at once; the page reaches
  // under a phone's notch and keeps clear of it with Discord's safe-area insets (the CSS's
  // var(--discord-safe-area-inset-*, env(...)))
  if (DISCORD) {
    document.body.classList.add("is-discord");
    document.querySelector('meta[name="viewport"]')?.setAttribute("content", "width=device-width, initial-scale=1, viewport-fit=cover");
    void discordSdk();
  }

  // the tab keeps the page's title: the client names its window "Armagetron Advanced"
  // (SDL_WM_SetCaption -> document.title, web/wasm/sdl_shim.cpp) once it starts
  keepTitle();

  // iPhone Safari: room to scroll, so its bars can fold away (fullscreen.ts)
  startScrollHide();

  // the page_view goes first (queued until the platform says whether analytics are on)
  startAnalytics();
  // what breaks (client_error): a lost WebGL context, an uncaught error of the page's own scripts
  watchErrors(document.getElementById("canvas") as HTMLCanvasElement | null);
  // Google Analytics, when the platform names a tag (consent first where the law wants it)
  startGoogleTag();
  // both have read the link's codes (?c=, utm_): off the address, so it reads as the site's own
  // and a copied or bookmarked link does not count the campaign again
  dropCampaignCodes();

  // index.html's two handlers live here: the Content-Security-Policy allows no inline script
  // (web/deploy). The canvas has no context menu; the poster fades in once loaded (it may have
  // loaded already, before this module ran)
  document.getElementById("canvas")?.addEventListener("contextmenu", (e) => e.preventDefault());
  const posterImage = document.querySelector<HTMLImageElement>("#poster img");
  if (posterImage) {
    const show = () => posterImage.parentElement?.classList.add("is-shown");
    if (posterImage.complete && posterImage.naturalWidth > 0) show();
    else posterImage.addEventListener("load", show, { once: true });
  }

  // no StrictMode: its double-invoked effects would load the wasm runtime twice. The player's
  // language first (i18n/index.ts: English at once, another its own small chunk)
  void initLang().then(() => createRoot(document.getElementById("root")!).render(<App />));
}

// index.html's <title> ("Retrocycles") back whenever something else writes one
// (the page's own changes, an opponent's arrival flashing it, go through wantedTitle(): alerts.ts)
function keepTitle() {
  const restore = () => {
    const title = wantedTitle();
    if (document.title !== title) document.title = title;
  };
  new MutationObserver(restore).observe(document.head, { subtree: true, childList: true, characterData: true });
}
