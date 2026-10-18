// Linking an old id with its password on the Linked IDs screen (contract section 1
// "Linked IDs"): the platform asks the id's authority as a game server would and hands over the
// method and a salt; the page answers as a game client would (nKrawall in the game):
//   md5   md5(md5(prefix + password + suffix) + salt), %u in prefix/suffix the name, UTF-8
//   bmd5  md5(md5(password) + salt), the password in Latin-1
// The password never leaves this tab; only the answer goes to the platform, good for one check.
// WebCrypto has no md5, so it is here (RFC 1321).

export interface PasswordCheck {
  check: string; // sealed by the platform, passed back with the answer
  gid: string; // as it will be linked: "name@forums"
  user: string; // what %u stands for
  method: string; // "md5" | "bmd5"
  prefix: string;
  suffix: string;
  salt: string; // 16 bytes, hex
  expiresAt: number;
}

export function answerCheck(c: PasswordCheck, password: string): string {
  const stored =
    c.method === "bmd5"
      ? md5(latin1(password))
      : md5(new TextEncoder().encode(c.prefix.split("%u").join(c.user) + password + c.suffix.split("%u").join(c.user)));
  const salt = fromHex(c.salt);
  const both = new Uint8Array(32);
  both.set(stored, 0);
  both.set(salt, 16);
  return toHex(md5(both));
}

function latin1(s: string): Uint8Array {
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xff;
  return out;
}

function fromHex(hex: string): Uint8Array {
  const out = new Uint8Array(hex.length >> 1);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}

export function toHex(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += (b < 16 ? "0" : "") + b.toString(16);
  return s;
}

const S = [7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21];
const K = Array.from({ length: 64 }, (_, i) => Math.floor(Math.abs(Math.sin(i + 1)) * 2 ** 32) >>> 0);

export function md5(data: Uint8Array): Uint8Array {
  const length = data.length;
  const blocks = ((length + 8) >>> 6) + 1;
  const padded = new Uint8Array(blocks * 64);
  padded.set(data);
  padded[length] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(padded.length - 8, (length * 8) >>> 0, true);
  view.setUint32(padded.length - 4, Math.floor(length / 0x20000000), true);

  let a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  for (let block = 0; block < padded.length; block += 64) {
    let a = a0, b = b0, c = c0, d = d0;
    for (let i = 0; i < 64; i++) {
      let f: number, g: number;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      const sum = (a + f + K[i] + view.getUint32(block + g * 4, true)) >>> 0;
      a = d;
      d = c;
      c = b;
      b = (b + ((sum << S[i]) | (sum >>> (32 - S[i])))) >>> 0;
    }
    a0 = (a0 + a) >>> 0;
    b0 = (b0 + b) >>> 0;
    c0 = (c0 + c) >>> 0;
    d0 = (d0 + d) >>> 0;
  }
  const out = new Uint8Array(16);
  const o = new DataView(out.buffer);
  o.setUint32(0, a0, true);
  o.setUint32(4, b0, true);
  o.setUint32(8, c0, true);
  o.setUint32(12, d0, true);
  return out;
}
