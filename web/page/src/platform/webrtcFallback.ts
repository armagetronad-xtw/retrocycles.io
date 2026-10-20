// WebRTC that cannot get through: a connect that failed at ICE (the signalling answered, then
// no path opened: a network that lets neither UDP nor the TURN relay out, like some school,
// office and hotel networks). Remembered across visits for a week (localStorage, with the time
// it was seen), so a player on such a network does not fail once at every visit: from then on
// the page goes in over the room's signalling WebSocket instead, the transport the Discord
// Activity uses (links.ts WEBSOCKET_TRANSPORT): Quick Play asks the platform for a room that
// takes it (`webSocket: true`), and the Server Browser joins that way a room whose lobby says it
// does. The game packets then ride TCP: a little more lag, but a game. A WebRTC path that does
// open later (a server reached without the WebSocket) forgets it at once.

const KEY = "rc.webrtcBlocked";
const REMEMBER_MS = 7 * 24 * 3600 * 1000;

let blocked = (() => {
  try {
    const at = Number(localStorage.getItem(KEY));
    return Number.isFinite(at) && at > 0 && Date.now() - at < REMEMBER_MS;
  } catch {
    return false;
  }
})();

export function webRtcBlocked(): boolean {
  return blocked;
}

export function noteWebRtcBlocked() {
  if (blocked) return;
  blocked = true;
  try {
    localStorage.setItem(KEY, String(Date.now()));
  } catch {
    // private mode or blocked storage: this page load only
  }
  console.info("connect: WebRTC did not get through (ICE); the next connects go over the WebSocket");
}

// a WebRTC path opened (direct or through the relay): this network lets it through after all
export function noteWebRtcWorked() {
  if (!blocked) return;
  blocked = false;
  try {
    localStorage.removeItem(KEY);
  } catch {
    // nothing kept to clear
  }
  console.info("connect: WebRTC got through; connects use it again");
}
