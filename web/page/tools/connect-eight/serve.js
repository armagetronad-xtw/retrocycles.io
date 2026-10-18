// static server for the recording build, cross-origin isolated like the deploy
const http = require("http");
const fs = require("fs");
const path = require("path");
const root = process.argv[2];
const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".data": "application/octet-stream", ".json": "application/json", ".cfg": "text/plain" };
http
  .createServer((req, res) => {
    const file = path.join(root, decodeURIComponent(req.url.split("?")[0]).replace(/^\/+/, "") || "client.html");
    fs.readFile(file, (err, body) => {
      if (err) {
        res.writeHead(404);
        return res.end("no " + req.url);
      }
      res.writeHead(200, {
        "Content-Type": types[path.extname(file)] || "application/octet-stream",
        "Cross-Origin-Opener-Policy": "same-origin",
        "Cross-Origin-Embedder-Policy": "require-corp",
        "Cache-Control": "no-store"
      });
      res.end(body);
    });
  })
  .listen(5199, () => console.log("serving " + root + " on 5199"));
