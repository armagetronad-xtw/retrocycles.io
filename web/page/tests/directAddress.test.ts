// Direct Connect's addresses (src/directAddress.ts): what may be typed, the blocklist's host
// form, the gateway's URL and the same-host rule for a server's moves.
import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { blockCandidates, gatewayUrl, normalizeHost, parseAddress, sameHost, signalHost, signalPortFor } from "../src/directAddress.ts";

const PAGE = "https://retrocycles.io/";

test("host alone takes the default game port", () => {
  assert.deepEqual(parseAddress("Example.COM"), { kind: "ip", host: "example.com", port: 4534, key: "example.com:4534" });
});

test("host:port and IPv4", () => {
  assert.deepEqual(parseAddress(" 203.0.113.7:4540 "), { kind: "ip", host: "203.0.113.7", port: 4540, key: "203.0.113.7:4540" });
});

test("IPv6 in brackets, with and without a port, and bare", () => {
  assert.deepEqual(parseAddress("[2001:DB8:0:0::1]:4534"), { kind: "ip", host: "2001:db8::1", port: 4534, key: "[2001:db8::1]:4534" });
  assert.deepEqual(parseAddress("[2001:db8::1]"), { kind: "ip", host: "2001:db8::1", port: 4534, key: "[2001:db8::1]:4534" });
  assert.deepEqual(parseAddress("2001:db8::1"), { kind: "ip", host: "2001:db8::1", port: 4534, key: "[2001:db8::1]:4534" });
});

test("a wss:// URL is kept as a URL", () => {
  const a = parseAddress("wss://Fort.Example.org/signal");
  assert.equal("error" in a ? a.error : a.kind, "wss");
  if (!("error" in a) && a.kind === "wss") {
    assert.equal(a.host, "fort.example.org");
    assert.equal(a.url, "wss://fort.example.org/signal");
  }
});

test("what is refused", () => {
  assert.deepEqual(parseAddress(""), { error: "empty" });
  assert.deepEqual(parseAddress("host:0"), { error: "port" });
  assert.deepEqual(parseAddress("host:70000"), { error: "port" });
  assert.deepEqual(parseAddress("host:abc"), { error: "port" });
  assert.deepEqual(parseAddress("http://example.com"), { error: "invalid" });
  assert.deepEqual(parseAddress("ws://example.com:4535"), { error: "invalid" });
  assert.deepEqual(parseAddress("bad host"), { error: "invalid" });
  assert.deepEqual(parseAddress("-x.com"), { error: "invalid" });
});

test("the blocklist's host form", () => {
  assert.equal(normalizeHost("EXAMPLE.com."), "example.com");
  assert.equal(normalizeHost("[2001:0db8:0000::0001]"), "2001:db8::1");
  assert.equal(normalizeHost("127.1"), "127.0.0.1");
});

test("the signalling port", () => {
  assert.equal(signalPortFor(4534, null), 4535);
  assert.equal(signalPortFor(4534, 9000), 9000);
});

test("the gateway URL", () => {
  assert.equal(gatewayUrl("https://api.retrocycles.io", "203.0.113.7", 4535), "wss://api.retrocycles.io/v1/direct/signal?to=203.0.113.7%3A4535");
  assert.equal(gatewayUrl("http://localhost:5080/", "2001:db8::1", 4535), "ws://localhost:5080/v1/direct/signal?to=%5B2001%3Adb8%3A%3A1%5D%3A4535");
});

test("the host a signalling URL reaches", () => {
  assert.equal(signalHost("wss://api.retrocycles.io/v1/direct/signal?to=203.0.113.7%3A4535", PAGE), "203.0.113.7");
  assert.equal(signalHost("/signal", PAGE), "retrocycles.io");
  assert.equal(signalHost("wss://Fort.example.org:8443/x", PAGE), "fort.example.org");
});

test("moves: the same host only", () => {
  const gw = "wss://api.retrocycles.io/v1/direct/signal?to=203.0.113.7%3A4535";
  assert.equal(sameHost(gw, "wss://api.retrocycles.io/v1/direct/signal?to=203.0.113.7%3A4537", PAGE), true);
  assert.equal(sameHost(gw, "ws://203.0.113.7:4537", PAGE), true);
  assert.equal(sameHost(gw, "wss://api.retrocycles.io/v1/direct/signal?to=198.51.100.2%3A4535", PAGE), false);
  assert.equal(sameHost("wss://fort.example.org/a", "wss://FORT.example.org/b", PAGE), true);
  assert.equal(sameHost("wss://fort.example.org/a", "wss://evil.example.net/b", PAGE), false);
  assert.equal(sameHost("wss://fort.example.org/a", "not a url ::", PAGE), false);
});

// the platform's normalization before hashing (its README "Direct Connect")
test("normalization: names, IDN, IPv4 forms, IPv6, IPv4-mapped", () => {
  assert.equal(normalizeHost("Fort.Example.COM."), "fort.example.com");
  assert.equal(normalizeHost("bücher.example"), "xn--bcher-kva.example");
  assert.equal(normalizeHost("127.1"), "127.0.0.1");
  assert.equal(normalizeHost("0x7f.0.0.1"), "127.0.0.1");
  assert.equal(normalizeHost("2130706433"), "127.0.0.1");
  assert.equal(normalizeHost("[2001:0DB8:0000:0000:0000:0000:0000:0001]"), "2001:db8::1");
  assert.equal(normalizeHost("fe80::1%eth0"), "fe80::1");
  assert.equal(normalizeHost("::ffff:203.0.113.7"), "203.0.113.7");
  assert.equal(normalizeHost("[::FFFF:cb00:7107]"), "203.0.113.7");
  assert.equal(normalizeHost("bad host"), null);
  // a name needs two labels as a target
  assert.deepEqual(parseAddress("localhost:4534"), { error: "invalid" });
});

test("blocklist candidates: the host and every wildcard above it", () => {
  assert.deepEqual(blockCandidates("a.b.example.com"), ["a.b.example.com", "*.b.example.com", "*.example.com", "*.com"]);
  assert.deepEqual(blockCandidates("1.2.3.4"), ["1.2.3.4", "1.2.3.*", "1.2.*", "1.*"]);
  assert.deepEqual(blockCandidates("2001:db8::1"), ["2001:db8::1"]);
});

test("blocklist hashes: sha256 hex of each normalized candidate", () => {
  const sha = (s: string) => createHash("sha256").update(s, "utf8").digest("hex");
  // a list as GET /v1/direct/blocklist sends it: a wildcard name, an IPv4 prefix, one exact host
  const list = new Set([sha("*.example.com"), sha("198.51.*"), sha("evil.example.net")]);
  const blocked = (typed: string) => {
    const host = normalizeHost(typed);
    return !!host && blockCandidates(host).some((c) => list.has(sha(c)));
  };
  assert.equal(sha("evil.example.net"), createHash("sha256").update("evil.example.net").digest("hex"));
  assert.equal(blocked("Fort.EXAMPLE.com."), true); // under *.example.com
  assert.equal(blocked("example.com"), false); // the wildcard is not the name itself
  assert.equal(blocked("198.51.100.20"), true);
  assert.equal(blocked("::ffff:198.51.100.20"), true); // IPv4-mapped reads as the IPv4 address
  assert.equal(blocked("EVIL.example.net"), true);
  assert.equal(blocked("good.example.net"), false);
  assert.equal(blocked("203.0.113.7"), false);
});
