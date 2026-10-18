// node compress.cjs <file>...: writes <file>.gz (gzip -9) and <file>.br (brotli, quality 11)
// next to each file, for servers that send precompressed files (the deploy's Caddy:
// `precompressed br gzip`). Node's zlib, so the build container needs no brotli tool.
const fs = require("fs");
const zlib = require("zlib");

for (const file of process.argv.slice(2)) {
  const raw = fs.readFileSync(file);
  const gz = zlib.gzipSync(raw, { level: 9 });
  const br = zlib.brotliCompressSync(raw, {
    params: {
      [zlib.constants.BROTLI_PARAM_QUALITY]: 11,
      [zlib.constants.BROTLI_PARAM_SIZE_HINT]: raw.length
    }
  });
  fs.writeFileSync(file + ".gz", gz);
  fs.writeFileSync(file + ".br", br);
  console.log(file + ": " + raw.length + " raw, " + gz.length + " gzip, " + br.length + " brotli");
}
