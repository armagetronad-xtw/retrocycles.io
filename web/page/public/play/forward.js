// The /play/ link's forwarder (play/index.html, tools/social.ts): on to /?play, Quick Play as
// soon as the page is ready (src/links.ts), keeping any other parameters (?c=, ?build=debug).
// A file, not inline: the Content-Security-Policy allows no inline script.
(function () {
  var rest = location.search.replace(/^\?/, "");
  location.replace(new URL("../?play" + (rest ? "&" + rest : "") + location.hash, location.href).href);
})();
