// The page's chat filter (src/chat/filter.ts) against the contract's vectors, which the server's
// local filter (gPlatformChatFilter) passes too. `npm test` (node's runner, types stripped).
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { ChatFilter, filterChat } from "../src/chat/filter.ts";

const vectors = JSON.parse(readFileSync(new URL("../../../platform-contract/test-vectors/chat-filter-v0.json", import.meta.url), "utf8")) as {
  filter: { version: string; mask: string[]; block: string[]; allow: string[] };
  cases: Array<{ name: string; text: string; expected: string; masked: boolean; blocked: boolean }>;
};

const filter = new ChatFilter(vectors.filter);

for (const c of vectors.cases) {
  test("vector: " + c.name, () => {
    const got = filter.apply(c.text);
    assert.equal(got.text, c.expected);
    assert.equal(got.masked, c.masked);
    assert.equal(got.blocked, c.blocked);
  });
}

test("built-in lists: game talk passes", () => {
  for (const line of ["gg wp", "kill him", "I died again", "core dump", "cut them off!"]) {
    const got = filterChat(line);
    assert.equal(got.masked, false, line);
    assert.equal(got.text, line);
  }
});

test("built-in lists: a mask term and a block term", () => {
  const masked = filterChat("oh sh1t");
  assert.equal(masked.text, "oh ****");
  assert.equal(masked.blocked, false);
  const blocked = filterChat("just kys");
  assert.equal(blocked.text, "just ***");
  assert.equal(blocked.blocked, true);
});

test("characters outside Latin-1 stay as they are", () => {
  assert.equal(filterChat("こんにちは darn").text, "こんにちは darn");
  assert.equal(filterChat("😀 fuck").text, "😀 ****");
});
