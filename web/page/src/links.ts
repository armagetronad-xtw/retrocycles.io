// The page's own links beside the invite (?join=, platform/quickplay.ts) and the emailed
// account links (?account=, App.tsx):
//
// - ?play (the /play/ link, public/play/index.html): Quick Play as soon as the page is ready,
//   as an invite link does for its room. Read once and taken out of the address bar.
// - ?embed (the share card's player, index.html's twitter:player): the page inside a social
//   post's frame. The deploy lets those sites frame this URL alone (frame-ancestors, not
//   cross-origin isolated: their pages are not). The page there is one PLAY over the scene
//   (components/Embed.tsx) and plays without a platform session: the session cookie would be
//   a third-party one, which browsers drop (platform/session.ts). Kept in the address bar, so a
//   reload in the frame stays the embed.

// - Inside a Discord Activity: Discord opens the page with ?frame_id=...&instance_id=...
//   (src/discord.ts). It is an embed too (one PLAY, compact, silent at first), but
//   with a platform session: Discord signs the player in. Its frame forbids WebRTC, so the
//   game packets go over the signalling WebSocket; ?transport=websocket picks that
//   transport anywhere, for tests.
import { LEGAL } from "./legal/config";

export const DISCORD: boolean = (() => {
  const query = new URLSearchParams(location.search);
  return query.has("frame_id") && query.has("instance_id");
})();

export const EMBED: boolean = DISCORD || new URLSearchParams(location.search).has("embed");

export const WEBSOCKET_TRANSPORT: boolean = DISCORD || new URLSearchParams(location.search).get("transport") === "websocket";

export function readPlayLink(): boolean {
  return dropParams((name) => name === "play");
}

// takes the parameters `drop` names off the address bar, the others keeping their own spelling
// (`out`, not URLSearchParams' `out=`); whether any went
function dropParams(drop: (name: string) => boolean): boolean {
  if (!location.search) return false;
  const parts = location.search.slice(1).split("&");
  const kept = parts.filter((part) => {
    let name = part.split("=")[0].replace(/\+/g, " ");
    try {
      name = decodeURIComponent(name);
    } catch {
      // a malformed escape: the name as written
    }
    return part !== "" && !drop(name);
  });
  if (kept.length === parts.length) return false;
  history.replaceState(history.state, "", location.pathname + (kept.length ? "?" + kept.join("&") : "") + location.hash);
  return true;
}

// the whole page in a tab of its own, on its home screen (the embed's way out; not straight into
// a game); `from`
// is the usage events' campaign, so the reports tell the ways apart: `embed` under PLAY,
// `embed-menu` the Esc menu's lane, `embed-dead` the cell while the player waits to ride.
// Inside Discord the page's own address is Discord's proxy: the site's, with `discord` for
// `embed` in the campaign (src/discord.ts openOutside opens it). `out`: the site serves that
// tab without cross-origin isolation (the platform's Caddyfile), since X's sandboxed frame
// opens it sandboxed too, where Chrome refuses a COOP page (an error page instead)
// takes the campaign codes (?c=, a creator link's ?l=, utm_*) off the address bar once the usage
// events and GA have read them (main.tsx). The other parameters keep their own spelling (`out`, not
// `out=`); a page in a frame (the embed, Discord) has no address bar to tidy
export function dropCampaignCodes() {
  if (EMBED) return;
  dropParams((name) => name === "c" || name === "l" || name.startsWith("utm_"));
}

export function fullPageLink(from: "embed" | "embed-menu" | "embed-dead" | "embed-profile" = "embed"): string {
  if (DISCORD) return "https://" + LEGAL.siteHost + "/?c=" + from.replace("embed", "discord");
  return new URL("./?c=" + from + "&out", location.href).href;
}
