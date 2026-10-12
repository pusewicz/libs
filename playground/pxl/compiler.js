// The compiler of the playground. It runs in a worker: Clang and LLD in
// WebAssembly (YoWASP) compile a C file into a side module that the host of
// the preview loads.
//
// Messages to the worker:
//   {type: "compile", id, source}
// Messages from the worker:
//   {type: "progress", loaded, total}  bytes of the download
//   {type: "ready"}                    the compiler can compile
//   {type: "failed", text}             the compiler did not load
//   {type: "result", id, program, output, time}
//                                      program is null if the build failed

const base = new URL(".", import.meta.url);
let manifest;
let clang;
let headers;
/** The command of the Clang front end that compiles main.c. */
let frontend;

/**
 * Serves the large files from their .gz copies. GitHub Pages may not
 * compress them, so the site stores them compressed. The YoWASP bundle
 * fetches its files with fetch(), so this must run before it loads.
 */
function serveCompressedFiles() {
  const nativeFetch = globalThis.fetch.bind(globalThis);
  const total = Object.values(manifest.compressed).reduce((a, b) => a + b, 0);
  let loaded = 0;
  globalThis.fetch = async (input, init) => {
    const url = new URL(input instanceof Request ? input.url : input, base);
    const name = url.href.startsWith(base.href)
      ? url.href.slice(base.href.length)
      : "";
    if (!Object.hasOwn(manifest.compressed, name)) {
      return nativeFetch(input, init);
    }
    const response = await nativeFetch(`${url.href}.gz`, init);
    if (!response.ok) {
      throw new Error(`${name}: HTTP ${response.status}`);
    }
    const counted = response.body.pipeThrough(
      new TransformStream({
        transform(chunk, controller) {
          loaded += chunk.length;
          postMessage({ type: "progress", loaded, total });
          controller.enqueue(chunk);
        },
      }),
    );
    const type = name.endsWith(".wasm")
      ? "application/wasm"
      : "application/octet-stream";
    return new Response(counted.pipeThrough(new DecompressionStream("gzip")), {
      headers: { "content-type": type },
    });
  };
}

/** Reads a tar archive into a YoWASP file tree. */
function untar(bytes) {
  const root = {};
  const decoder = new TextDecoder();
  const field = (header, start, length) =>
    decoder.decode(header.subarray(start, start + length)).replace(/\0.*$/s, "");
  for (let offset = 0; offset + 512 <= bytes.length; ) {
    const header = bytes.subarray(offset, offset + 512);
    if (header.every((byte) => byte === 0)) {
      break;
    }
    const prefix = field(header, 345, 155);
    const name = prefix ? `${prefix}/${field(header, 0, 100)}` : field(header, 0, 100);
    const size = parseInt(field(header, 124, 12).trim() || "0", 8);
    const type = field(header, 156, 1);
    offset += 512;
    if (type === "0" || type === "") {
      const parts = name.split("/").filter(Boolean);
      let dir = root;
      for (const part of parts.slice(0, -1)) {
        dir = dir[part] ??= {};
      }
      dir[parts.at(-1)] = bytes.slice(offset, offset + size);
    }
    offset += Math.ceil(size / 512) * 512;
  }
  return root;
}

async function fetchBytes(name) {
  const response = await fetch(new URL(name, base));
  if (!response.ok) {
    throw new Error(`${name}: HTTP ${response.status}`);
  }
  return new Uint8Array(await response.arrayBuffer());
}

async function load() {
  manifest = await (await fetch(new URL("manifest.json", base))).json();
  serveCompressedFiles();
  const [bundle, archive] = await Promise.all([
    import(new URL("clang/bundle.js", base).href),
    fetchBytes("include.tar"),
  ]);
  clang = bundle;
  headers = untar(archive);
  // The first run loads and compiles the WebAssembly of LLVM.
  await clang.runLLVM(null, {}, { fetchProgress: () => {} });
  frontend = await frontendCommand();
}

/**
 * Returns the command that the Clang driver runs for main.c. runClang() asks
 * the driver for it on each compile, which takes a run of LLVM. The flags do
 * not change, so the playground asks once.
 */
async function frontendCommand() {
  let output = "";
  const decoder = new TextDecoder();
  const files = { "main.c": "" };
  await clang.runLLVM(["clang", "-###", ...manifest.compile, "-c", "main.c", "-o", "main.o"], files, {
    stdout: (bytes) => (output += bytes ? decoder.decode(bytes, { stream: true }) : ""),
    stderr: (bytes) => (output += bytes ? decoder.decode(bytes, { stream: true }) : ""),
  });
  const commands = output.split("\n").filter((line) => line.startsWith(' "'));
  if (commands.length !== 1) {
    throw new Error(`Unexpected output of clang -###:\n${output}`);
  }
  // The driver quotes each argument and puts a backslash before ", \\ and $.
  const command = Array.from(commands[0].matchAll(/ "((?:[^"\\]|\\.)*)"/g), (match) =>
    match[1].replace(/\\(.)/g, "$1"),
  );
  if (command[0] === "") {
    command.shift();
  }
  return command;
}

/** Compiles and links a program. Returns the side module and the output. */
async function compile(source) {
  const decoder = new TextDecoder();
  let output = "";
  const options = {
    decodeASCII: false,
    stdout: (bytes) => (output += bytes ? decoder.decode(bytes, { stream: true }) : ""),
    stderr: (bytes) => (output += bytes ? decoder.decode(bytes, { stream: true }) : ""),
  };
  try {
    const compiled = await clang.runLLVM(frontend, { ...headers, "main.c": source }, options);
    const linked = await clang.runLLVM(
      ["wasm-ld", ...manifest.link, "main.o", "-o", "program.wasm"],
      { "main.o": compiled["main.o"], "host.exports": headers["host.exports"] },
      options,
    );
    return { program: linked["program.wasm"], output };
  } catch (error) {
    if (error instanceof clang.Exit) {
      return { program: null, output };
    }
    throw error;
  }
}

// Listen before the first await, so that no message is lost.
const loading = load();
let queue = Promise.resolve();

addEventListener("message", (event) => {
  const { type, id, source } = event.data;
  if (type !== "compile") {
    return;
  }
  queue = queue.then(async () => {
    const start = performance.now();
    try {
      await loading;
      const { program, output } = await compile(source);
      const time = performance.now() - start;
      postMessage(
        { type: "result", id, program, output, time },
        program ? [program.buffer] : [],
      );
    } catch (error) {
      const output = `The compiler crashed: ${error?.stack ?? error}`;
      postMessage({ type: "result", id, program: null, output, time: 0 });
    }
  });
});

loading.then(
  () => postMessage({ type: "ready" }),
  (error) => postMessage({ type: "failed", text: String(error?.message ?? error) }),
);
