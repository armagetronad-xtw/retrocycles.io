// The account links' forwarders (account/<kind>/index.html): on to /?account=<kind>&userId=..&token=..
// with location.replace, so the token does not stay in the history. A file, not inline: the
// Content-Security-Policy allows no inline script.
(function () {
  var kind = document.currentScript && document.currentScript.dataset.kind;
  if (kind !== "verify-email" && kind !== "reset-password") return;
  location.replace(new URL("../../?account=" + kind + "&" + location.search.replace(/^\?/, ""), location.href).href);
})();
