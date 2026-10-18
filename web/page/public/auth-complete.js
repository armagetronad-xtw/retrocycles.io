// auth-complete.html's script (a file: the Content-Security-Policy allows no inline script)
(function () {
  var found = /(?:^#|&)rc-auth=([^&]*)/.exec(location.hash);
  // the result has done its job once read: keep it out of the history
  try { history.replaceState(null, "", location.pathname); } catch (e) {}
  if (!found) return;
  var message = null;
  try { message = JSON.parse(decodeURIComponent(found[1])); } catch (e) { return; }
  if (!message || message.type !== "rc-auth" || typeof message.status !== "string") return;
  try {
    var channel = new BroadcastChannel("rc-auth");
    channel.postMessage(message);
    channel.close();
  } catch (e) {
    return; // no BroadcastChannel: the play page notices the new session when it gets the focus back
  }
  document.getElementById("m").textContent = message.status === "error"
    ? "Sign-in did not complete. You can close this window."
    : "Done. You can close this window.";
  setTimeout(function () { window.close(); }, 100);
})();
