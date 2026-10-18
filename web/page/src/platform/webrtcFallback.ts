// WebRTC that cannot get through: a connect that failed at ICE (the signalling answered, then
// no path opened: a network that lets neither UDP nor the TURN relay out, like some school,
// office and hotel networks). Remembered for this tab (sessionStorage, so a reload keeps it):
// from then on the page goes in over the room's signalling WebSocket instead, the transport
// the Discord Activity uses (links.ts WEBSOCKET_TRANSPORT): Quick Play asks the platform for a
// room that takes it (`webSocket: true`), and the Server Browser joins that way a room whose
// lobby says it does. The game packets then ride TCP: a little more lag, but a game.

const KEY = "rc.webrtcBlocked";

let blocked = (() => {
  try {
    return sessionStorage.getItem(KEY) === "1";
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
    sessionStorage.setItem(KEY, "1");
  } catch {
    // private mode or blocked storage: this page load only
  }
  console.info("connect: WebRTC did not get through (ICE); the next connects go over the WebSocket");
}
