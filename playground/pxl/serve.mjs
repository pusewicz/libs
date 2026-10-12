// A static server for the playground. It sends the headers that GitHub Pages
// sends: CORS, which the sandboxed preview needs, and the type of
// WebAssembly.
//
//   node serve.mjs DIRECTORY [PORT]

import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

const types = {
  ".c": "text/plain; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".gz": "application/gzip",
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".mjs": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
};

/** Serves a directory. Returns the server when it listens. */
export function serve(directory, port) {
  const root = path.resolve(directory);
  const server = http.createServer((request, response) => {
    const url = new URL(request.url, "http://localhost");
    let file = path.join(root, decodeURIComponent(url.pathname));
    if (file.endsWith(path.sep)) {
      file = path.join(file, "index.html");
    }
    const inside = file === root || file.startsWith(root + path.sep);
    if (!inside || !fs.statSync(file, { throwIfNoEntry: false })?.isFile()) {
      response.writeHead(404, { "content-type": "text/plain" });
      response.end("Not found\n");
      return;
    }
    response.writeHead(200, {
      "access-control-allow-origin": "*",
      "content-type": types[path.extname(file)] ?? "application/octet-stream",
    });
    fs.createReadStream(file).pipe(response);
  });
  return new Promise((resolve) => server.listen(port, "localhost", () => resolve(server)));
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const [directory = ".", port = "8000"] = process.argv.slice(2);
  const server = await serve(directory, Number(port));
  console.log(`Serving ${directory} on http://localhost:${server.address().port}/pxl/`);
}
