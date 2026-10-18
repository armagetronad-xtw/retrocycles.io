// The game's text as the page reads it. The client hands every string over byte
// by byte (API.md "Text": each byte above 0x7f as \u00xx), because the game's strings are
// Latin-1. In an official room a browser player's chat is UTF-8 inside those bytes, next to
// desktop players' Latin-1 names in the same line. decodeGameText reads well formed UTF-8
// sequences as the characters they spell and leaves every other byte as its Latin-1
// character, so "café" from a desktop client and "안녕" from a browser both come out right.
// The same reading as tools/tUtf8.h on the server.

// the length (2-4) of the well formed UTF-8 sequence at codes[at] and its code point, or 0
function sequence(codes: number[], at: number): [number, number] {
  const lead = codes[at];
  let length = 0;
  let cp = 0;
  if (lead >= 0xc2 && lead <= 0xdf) [length, cp] = [2, lead & 0x1f];
  else if (lead >= 0xe0 && lead <= 0xef) [length, cp] = [3, lead & 0x0f];
  else if (lead >= 0xf0 && lead <= 0xf4) [length, cp] = [4, lead & 0x07];
  else return [0, 0];
  if (at + length > codes.length) return [0, 0];
  for (let k = 1; k < length; k++) {
    const c = codes[at + k];
    if ((c & 0xc0) !== 0x80) return [0, 0];
    cp = (cp << 6) | (c & 0x3f);
  }
  if ((length === 3 && (cp < 0x800 || (cp >= 0xd800 && cp <= 0xdfff))) || (length === 4 && (cp < 0x10000 || cp > 0x10ffff))) return [0, 0];
  return [length, cp];
}

export function decodeGameText(text: string): string {
  // nothing above 0x7f: nothing to read (the common case, kept cheap)
  if (!/[\u0080-\u00ff]/.test(text)) return text;
  const codes = Array.from(text, (ch) => ch.charCodeAt(0));
  let out = "";
  for (let i = 0; i < codes.length; ) {
    const [length, cp] = codes[i] < 0x100 ? sequence(codes, i) : [0, 0];
    if (length) {
      out += String.fromCodePoint(cp);
      i += length;
    } else {
      out += text[i];
      i++;
    }
  }
  return out;
}

// the language a line is written in, for its font (chat.css: a CJK face per language, the
// same Han characters drawn the Japanese, Korean or Chinese way): hangul is Korean, kana
// Japanese, other Han Chinese; undefined for a line without any
export function cjkLang(text: string): "ko" | "ja" | "zh" | undefined {
  if (/[\uac00-\ud7af\u1100-\u11ff\u3130-\u318f]/.test(text)) return "ko";
  if (/[\u3040-\u30ff\u31f0-\u31ff\uff66-\uff9f]/.test(text)) return "ja";
  if (/[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]|[\ud840-\ud87f][\udc00-\udfff]/.test(text)) return "zh";
  return undefined;
}

// the CJK faces (public/fonts/cjk/faces.css, made by tools/cjk-fonts.mjs: Noto Sans KR / JP /
// SC / TC in unicode-range slices) are linked the first time a CJK line or draft is shown, so
// a first visit loads neither the 400 KB sheet nor any slice
let facesLinked = false;
export function linkCjkFaces() {
  if (facesLinked || typeof document === "undefined") return;
  facesLinked = true;
  const link = document.createElement("link");
  link.rel = "stylesheet";
  link.href = "/fonts/cjk/faces.css";
  document.head.appendChild(link);
}

// whether the text has a character the game's Latin-1 cannot hold (a room without UTF-8 chat
// sends it as '?')
export function hasWide(text: string): boolean {
  return /[^\u0000-\u00ff]/.test(text);
}
