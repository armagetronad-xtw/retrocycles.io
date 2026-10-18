// The page's own chat filter: the server's local word filter
// (src/tron/gPlatformModeration.cpp, gPlatformChatFilter: BuiltIn and Apply) ported line for
// line, so a line reads the same here as in a room whose platform is out of reach. It runs on
// the chat a server that is not one of ours sends (a community server, a Direct Connect): our
// rooms filter on the platform already. Verified against
// platform-contract/test-vectors/chat-filter-v0.json (tests/chatFilter.test.ts).
//
// Pure, and without imports: the tests load it under node.
//
// How a line is read (the contract's section 5): words are runs of letters (a-z, the Latin-1
// accents folded) and of signs that stand for letters (4 for a, @ for a, $ for s, ...); the
// game's colour codes (0xRRGGBB, 0xRESETT) are left out, even inside a word; a word is read
// twice, without the signs at its ends (core) and with them (full); three or more one-letter
// words in a row are one word ("d a r n"); runs of three or more equal letters are read cut to
// one and to two. A term is one or more words: "word" matches whole words, "word*" a prefix,
// "*word*" anywhere inside; a match covered word for word by an allowed term is no match. Every
// character of a match becomes "*"; a block term among the matches marks the line blocked.

export interface FilterLists {
  version: string;
  mask: readonly string[];
  block: readonly string[];
  allow: readonly string[];
}

export interface FilterResult {
  text: string; // the line with every match masked
  masked: boolean; // anything was replaced
  blocked: boolean; // a block term was among the matches
}

// the server's fallback lists (gPlatformChatFilter::BuiltIn), kept the same
export const BUILT_IN: FilterLists = {
  version: "builtin-1",
  mask: ["fuck*", "motherfuck*", "shit*", "bullshit*", "bitch*", "cunt*", "asshole*", "dickhead*", "bastard*", "wank*", "twat*", "slut*", "whore*", "retard*", "stfu", "fck*", "fuk*"],
  block: ["nigger*", "nigga*", "faggot*", "kys", "kill yourself", "kill urself", "go die"],
  allow: ["kill", "kills", "killed", "frag", "frags", "core dump", "cut him off", "cut her off", "cut them off", "die", "died", "dead"]
};

interface Word {
  text: string;
  infix: boolean; // *word*
  prefix: boolean; // word*
}
type Term = Word[];

interface Token {
  core: string;
  full: string;
  coreCps: number[];
  fullCps: number[];
}

// 0xc0..0xdf and 0xe0..0xff; "" for the multiplication and division signs
const UPPER = "aaaaaaaceeeeiiiidnooooo\0ouuuuyts";
const LOWER = "aaaaaaaceeeeiiiidnooooo\0ouuuuyty";

// the letter a code point stands for, "" if none
function letter(cp: number): string {
  if (cp >= 0x61 && cp <= 0x7a) return String.fromCharCode(cp);
  if (cp >= 0x41 && cp <= 0x5a) return String.fromCharCode(cp + 32);
  const c = cp >= 0xc0 && cp <= 0xdf ? UPPER[cp - 0xc0] : cp >= 0xe0 && cp <= 0xff ? LOWER[cp - 0xe0] : "";
  return c === "\0" ? "" : c;
}

const LEET: Record<string, string> = { "0": "o", "1": "i", "3": "e", "4": "a", "5": "s", "7": "t", "8": "b", "@": "a", $: "s", "!": "i", "|": "i", "+": "t" };

// the letter a digit or sign may stand for, "" if none
function leet(cp: number): string {
  return cp < 0x80 ? LEET[String.fromCharCode(cp)] ?? "" : "";
}

function isHex(cp: number): boolean {
  return (cp >= 0x30 && cp <= 0x39) || (cp >= 0x61 && cp <= 0x66) || (cp >= 0x41 && cp <= 0x46);
}

function tokenize(cps: number[]): Token[] {
  const n = cps.length;
  // colour codes 0xRRGGBB and 0xRESETT are left out
  const transparent: boolean[] = new Array(n).fill(false);
  for (let i = 0; i + 7 < n; ++i) {
    if (cps[i] !== 0x30 || cps[i + 1] !== 0x78) continue;
    let hex = true;
    for (let k = 2; k < 8 && hex; ++k) hex = isHex(cps[i + k]);
    let reset = true;
    for (let k = 0; k < 6 && reset; ++k) reset = cps[i + 2 + k] === "RESETT".charCodeAt(k);
    if (hex || reset) {
      for (let k = 0; k < 8; ++k) transparent[i + k] = true;
      i += 7;
    }
  }

  const tokens: Token[] = [];
  let run: number[] = []; // indices of letters and signs
  const flush = () => {
    if (!run.length) return;
    let first = run.length;
    let last = 0;
    for (let k = 0; k < run.length; ++k) {
      if (letter(cps[run[k]])) {
        if (first === run.length) first = k;
        last = k;
      }
    }
    if (first < run.length) {
      const token: Token = { core: "", full: "", coreCps: [], fullCps: [] };
      for (let k = 0; k < run.length; ++k) {
        const cp = cps[run[k]];
        const l = letter(cp) || leet(cp);
        token.full += l;
        token.fullCps.push(run[k]);
        if (k >= first && k <= last) {
          token.core += l;
          token.coreCps.push(run[k]);
        }
      }
      tokens.push(token);
    }
    run = [];
  };
  for (let i = 0; i < n; ++i) {
    if (transparent[i]) continue;
    const cp = cps[i];
    if (letter(cp) || leet(cp)) run.push(i);
    else flush();
  }
  flush();

  // three or more one-letter words in a row are one word
  const joined: Token[] = [];
  for (let i = 0; i < tokens.length; ) {
    let j = i;
    while (j < tokens.length && tokens[j].core.length === 1) ++j;
    if (j - i >= 3) {
      const token: Token = { core: "", full: "", coreCps: [], fullCps: [] };
      for (let k = i; k < j; ++k) {
        token.core += tokens[k].core;
        token.coreCps.push(...tokens[k].coreCps);
      }
      token.full = token.core;
      token.fullCps = token.coreCps.slice();
      joined.push(token);
      i = j;
    } else {
      joined.push(tokens[i]);
      ++i;
    }
  }
  return joined;
}

// runs of three or more equal letters cut to `keep` letters
function collapse(text: string, keep: number): string {
  let out = "";
  for (let i = 0; i < text.length; ) {
    let j = i;
    while (j < text.length && text[j] === text[i]) ++j;
    out += text[i].repeat(j - i >= 3 ? keep : j - i);
    i = j;
  }
  return out;
}

function parse(list: readonly string[]): Term[] {
  const out: Term[] = [];
  for (const entry of list) {
    const term: Term = [];
    for (const word of entry.split(/[ \t]+/)) {
      if (!word) continue;
      const leading = word[0] === "*";
      const trailing = word.length > 1 && word[word.length - 1] === "*";
      let text = "";
      for (const c of word) {
        const cp = c.codePointAt(0) ?? 0;
        text += cp <= 0xff ? letter(cp) : "";
      }
      if (text) term.push({ text, infix: leading, prefix: !leading && trailing });
    }
    if (term.length) out.push(term);
  }
  return out;
}

// which reading of a token a word of a term matched: 0 none, 1 core, 2 full
function matchWord(word: Word, token: Token): number {
  const matches = (text: string) => {
    for (const v of [text, collapse(text, 1), collapse(text, 2)]) {
      if (word.infix ? v.includes(word.text) : word.prefix ? v.startsWith(word.text) : v === word.text) return true;
    }
    return false;
  };
  if (matches(token.core)) return 1;
  if (token.full !== token.core && matches(token.full)) return 2;
  return 0;
}

// the readings of tokens[at..] matched by term, empty if it does not match there
function matchTerm(term: Term, tokens: Token[], at: number): number[] {
  if (at + term.length > tokens.length) return [];
  const how: number[] = [];
  for (let k = 0; k < term.length; ++k) {
    const reading = matchWord(term[k], tokens[at + k]);
    if (!reading) return [];
    how.push(reading);
  }
  return how;
}

export class ChatFilter {
  readonly version: string;
  private readonly terms: Term[];
  private readonly blockFrom: number;
  private readonly allow: Term[];

  constructor(lists: FilterLists = BUILT_IN) {
    this.version = lists.version;
    const mask = parse(lists.mask);
    this.blockFrom = mask.length;
    this.terms = [...mask, ...parse(lists.block)];
    this.allow = parse(lists.allow);
  }

  apply(text: string): FilterResult {
    // the line's characters (code points: the page has the text decoded already)
    const chars = Array.from(text);
    const cps = chars.map((c) => c.codePointAt(0) ?? 0);
    const tokens = tokenize(cps);
    if (!tokens.length || !this.terms.length) return { text, masked: false, blocked: false };

    const allowed: boolean[] = new Array(tokens.length).fill(false);
    for (const term of this.allow)
      for (let i = 0; i < tokens.length; ++i) if (matchTerm(term, tokens, i).length) for (let k = 0; k < term.length; ++k) allowed[i + k] = true;

    const hide: boolean[] = new Array(cps.length).fill(false);
    let any = false;
    let anyBlock = false;
    this.terms.forEach((term, t) => {
      for (let i = 0; i < tokens.length; ++i) {
        const how = matchTerm(term, tokens, i);
        if (!how.length) continue;
        let covered = true;
        for (let k = 0; k < term.length; ++k) covered = covered && allowed[i + k];
        if (covered) continue;
        any = true;
        anyBlock = anyBlock || t >= this.blockFrom;
        for (let k = 0; k < term.length; ++k) {
          const token = tokens[i + k];
          for (const cp of how[k] === 1 ? token.coreCps : token.fullCps) hide[cp] = true;
        }
      }
    });
    if (!any) return { text, masked: false, blocked: false };
    return { text: chars.map((c, i) => (hide[i] ? "*" : c)).join(""), masked: true, blocked: anyBlock };
  }
}

let builtIn: ChatFilter | null = null;

// a line from a server that is not ours, as the page shows it: masked where the filter says
export function filterChat(text: string): FilterResult {
  builtIn ??= new ChatFilter(BUILT_IN);
  return builtIn.apply(text);
}
