// client_error's `where` (src/platform/errorWhere.ts): which of the page's own files, never another's.
import { test } from "node:test";
import assert from "node:assert/strict";
import { whereOfFile, whereOfStack } from "../src/platform/errorWhere.ts";

const O = "https://retrocycles.io";

test("files of ours get their code", () => {
  assert.equal(whereOfFile(O + "/assets/index-DtMRDsfe.js", O), "page");
  assert.equal(whereOfFile(O + "/2/assets/output-BbR9mgxU.js", O), "page");
  assert.equal(whereOfFile(O + "/aa_api.js?v=914755ec7e16", O), "api");
  assert.equal(whereOfFile(O + "/client_opt.js?v=914755ec7e16", O), "client");
  assert.equal(whereOfFile(O + "/client.js", O), "client");
  assert.equal(whereOfFile(O + "/client_opt.wasm?v=1", O), "client");
  assert.equal(whereOfFile(O + "/auth-Ab12Cd34.js", O), "other");
});

test("files of others are not ours", () => {
  assert.equal(whereOfFile("https://www.googletagmanager.com/gtag/js?id=G-1", O), null);
  assert.equal(whereOfFile("chrome-extension://abc/content.js", O), null);
  assert.equal(whereOfFile("https://retrocycles.io.evil.example/assets/index-x.js", O), null);
  assert.equal(whereOfFile("", O), null);
  assert.equal(whereOfFile(undefined, O), null);
});

test("a stack: its first frame of ours", () => {
  const chrome = `TypeError: x is undefined
    at f (chrome-extension://abc/inject.js:1:20)
    at g (${O}/assets/index-DtMRDsfe.js:12:3456)`;
  assert.equal(whereOfStack(chrome, O), "page");
  const safari = `f@wasm://wasm/0a1b2c:wasm-function[12]:0x1234
g@${O}/client_opt.js?v=914755ec7e16:1:9999`;
  assert.equal(whereOfStack(safari, O), "client");
});

test("a stack with no frame of ours is not ours; none at all is unknown", () => {
  assert.equal(whereOfStack("Error\n    at https://www.googletagmanager.com/gtag/js?id=G-1:1:2", O), null);
  assert.equal(whereOfStack("Error: aborted", O), "unknown");
  assert.equal(whereOfStack(undefined, O), "unknown");
});
