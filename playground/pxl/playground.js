// The pxl playground: an editor on the left, the running program on the
// right. A worker (compiler.js) compiles the code as it changes, and a
// sandboxed frame (preview.html) runs each program that compiles. A program
// that does not compile leaves the last good one running.

import { cpp } from "@codemirror/lang-cpp";
import { lintGutter, setDiagnostics } from "@codemirror/lint";
import { Compartment, EditorState, Prec } from "@codemirror/state";
import { oneDark } from "@codemirror/theme-one-dark";
import { keymap } from "@codemirror/view";
import { basicSetup, EditorView } from "codemirror";

const buildDelay = 400;
const maxOutputLines = 2000;
const storageKey = "pxl-playground";
/** The preview may be silent this long before the playground stops it. */
const silenceLimit = 5000;

const elements = {
  editor: document.getElementById("editor"),
  example: document.getElementById("example"),
  run: document.getElementById("run"),
  auto: document.getElementById("auto"),
  status: document.getElementById("status"),
  preview: document.getElementById("preview"),
  output: document.getElementById("output"),
  clear: document.getElementById("clear"),
  splitter: document.getElementById("splitter"),
};

// ---------------------------------------------------------------------------
// Saved state. Storage can fail, for example in a private window.

function loadSaved() {
  try {
    return JSON.parse(localStorage.getItem(storageKey)) ?? {};
  } catch {
    return {};
  }
}

const saved = loadSaved();

/** Saves values. Another tab can save too, so this keeps its other values. */
function save(changes) {
  Object.assign(saved, changes);
  try {
    const stored = { ...loadSaved(), ...changes };
    localStorage.setItem(storageKey, JSON.stringify(stored));
  } catch {
    // The playground works without storage.
  }
}

// ---------------------------------------------------------------------------
// Status and output

function setStatus(state, text) {
  document.body.dataset.state = state;
  elements.status.textContent = text;
}

let pendingLines = [];
let flushScheduled = false;

/** Adds a line to the output. Flushes once per animation frame. */
function print(text, kind = "") {
  for (const line of String(text).split("\n")) {
    pendingLines.push({ line, kind });
  }
  if (!flushScheduled) {
    flushScheduled = true;
    requestAnimationFrame(flushOutput);
  }
}

function flushOutput() {
  flushScheduled = false;
  const output = elements.output;
  const atBottom =
    output.scrollTop + output.clientHeight >= output.scrollHeight - 4;
  const fragment = document.createDocumentFragment();
  for (const { line, kind } of pendingLines.slice(-maxOutputLines)) {
    const span = document.createElement("span");
    span.textContent = `${line}\n`;
    if (kind) {
      span.className = kind;
    }
    fragment.append(span);
  }
  pendingLines = [];
  output.append(fragment);
  while (output.childElementCount > maxOutputLines) {
    output.firstElementChild.remove();
  }
  if (atBottom) {
    output.scrollTop = output.scrollHeight;
  }
}

function clearOutput() {
  pendingLines = [];
  elements.output.replaceChildren();
}

// ---------------------------------------------------------------------------
// Diagnostics

const diagnosticPattern =
  /^([^:\n]+):(\d+):(\d+): (fatal error|error|warning|note): (.*)$/;

/** Parses Clang output. Returns the diagnostics of main.c for the editor. */
function parseDiagnostics(text, doc) {
  const diagnostics = [];
  let errors = 0;
  let warnings = 0;
  for (const line of text.split("\n")) {
    const match = diagnosticPattern.exec(line);
    if (!match) {
      continue;
    }
    const [, file, lineText, columnText, severity, message] = match;
    if (severity.endsWith("error")) {
      errors++;
    } else if (severity === "warning") {
      warnings++;
    }
    if (file !== "main.c") {
      continue;
    }
    const lineNumber = Math.min(Math.max(Number(lineText), 1), doc.lines);
    const docLine = doc.line(lineNumber);
    const from = Math.min(docLine.from + Number(columnText) - 1, docLine.to);
    const word = /^\w+/.exec(docLine.text.slice(from - docLine.from));
    diagnostics.push({
      from,
      to: Math.min(from + (word ? word[0].length : 1), docLine.to),
      severity: severity.endsWith("error") ? "error" : severity === "warning" ? "warning" : "info",
      message,
    });
  }
  return { diagnostics, errors, warnings };
}

function printCompilerOutput(text) {
  for (const line of text.replace(/\n$/, "").split("\n")) {
    const match = diagnosticPattern.exec(line);
    const kind = match ? match[4].replace("fatal ", "") : "";
    print(line, kind);
  }
}

function plural(count, word) {
  return `${count} ${word}${count === 1 ? "" : "s"}`;
}

// ---------------------------------------------------------------------------
// Preview

let frame = null;
let program = null;
let lastAlive = 0;

/** Runs a program in a new frame. The old frame and its program stop. */
function run(bytes) {
  program = bytes;
  lastAlive = performance.now();
  // A program that loops forever can freeze the page in browsers that run
  // the frame on the thread of the page. Then the next visit must not run
  // it again. The "running", "exit" and "crash" messages clear this.
  save({ starting: true });
  frame?.remove();
  frame = document.createElement("iframe");
  frame.title = "Preview";
  // No allow-same-origin: the program cannot reach the playground.
  frame.sandbox = "allow-scripts allow-pointer-lock";
  frame.src = "preview.html";
  elements.preview.replaceChildren(frame);
}

addEventListener("message", (event) => {
  if (!frame || event.source !== frame.contentWindow) {
    return;
  }
  const message = event.data;
  lastAlive = performance.now();
  switch (message?.type) {
    case "alive":
      break;
    case "running":
      save({ starting: false });
      break;
    case "ready": {
      const copy = program.slice();
      frame.contentWindow.postMessage({ type: "run", program: copy.buffer }, "*", [
        copy.buffer,
      ]);
      break;
    }
    case "stdout":
      print(message.text);
      break;
    case "stderr":
      print(message.text, "warning");
      break;
    case "exit":
      save({ starting: false });
      print(`The program ended with status ${message.status}.`, "note");
      setStatus(message.status === 0 ? "ended" : "error", "The program ended");
      break;
    case "crash":
      save({ starting: false });
      print(`The program crashed: ${message.text}`, "error");
      setStatus("error", "The program crashed. See the output.");
      break;
  }
});

// A hidden page can stop its timers. Start the count again when it shows.
document.addEventListener("visibilitychange", () => {
  lastAlive = performance.now();
});

/** Stops the program if its frame is silent: it loops forever. */
setInterval(() => {
  if (document.visibilityState !== "visible") {
    lastAlive = performance.now();
  } else if (frame && performance.now() - lastAlive > silenceLimit) {
    frame.remove();
    frame = null;
    save({ starting: false });
    print(
      `The program did not respond for ${silenceLimit / 1000} s, so the playground stopped it. Does a loop never end?`,
      "error",
    );
    setStatus("error", "The program stopped responding");
  }
}, 1000);

// ---------------------------------------------------------------------------
// Builds

const worker = new Worker(new URL("compiler.js", import.meta.url), {
  type: "module",
});
let compilerReady = false;
let building = false;
let buildQueued = false;
let restartQueued = false;
let builtSource = null;
let builtOk = false;
/** The last visit ended while a program started. Wait for Run. */
let holdRun = saved.starting === true;
let nextBuild = 0;
let finishedBuilds = 0;
let buildTimer = 0;

/** Asks for a build. restart: run the program again if the code is the same. */
function requestBuild(restart = false) {
  clearTimeout(buildTimer);
  buildQueued = true;
  restartQueued ||= restart;
  startBuild();
}

/** Builds and runs the code now: the Run button. */
function runNow() {
  holdRun = false;
  requestBuild(true);
}

function startBuild() {
  if (!compilerReady || building || !buildQueued) {
    return;
  }
  const source = view.state.doc.toString();
  const restart = restartQueued;
  buildQueued = false;
  restartQueued = false;
  if (source === builtSource && (builtOk || !restart)) {
    if (restart && program) {
      clearOutput();
      run(program);
      setStatus("running", "Running");
    }
    return;
  }
  building = true;
  setStatus("compiling", "Compiling…");
  worker.postMessage({ type: "compile", id: ++nextBuild, source });
  builtSource = source;
}

function finishBuild({ program: bytes, output, time }) {
  building = false;
  builtOk = bytes !== null;
  finishedBuilds++;
  // This build answers a Run that came during it, if the code is the same.
  if (view.state.doc.toString() === builtSource) {
    restartQueued = false;
  }
  const { diagnostics, errors, warnings } = parseDiagnostics(
    output,
    view.state.doc,
  );
  // The code may have changed during the build. Show the markers only if
  // they still fit.
  if (view.state.doc.toString() === builtSource) {
    view.dispatch(setDiagnostics(view.state, diagnostics));
  }
  clearOutput();
  if (output.trim()) {
    printCompilerOutput(output);
  }
  if (bytes && holdRun) {
    program = bytes;
    setStatus(
      "error",
      "The last program froze the page, maybe in a loop. Fix it, then press Run.",
    );
  } else if (bytes) {
    const note = warnings ? `, ${plural(warnings, "warning")}` : "";
    setStatus("running", `Running · built in ${Math.round(time)} ms${note}`);
    run(bytes);
  } else {
    const what = errors ? plural(errors, "error") : "The build failed";
    const keeps = program ? " · the last good build keeps running" : "";
    setStatus("error", `${what}${keeps}`);
  }
  startBuild();
}

worker.addEventListener("message", (event) => {
  const message = event.data;
  switch (message.type) {
    case "progress": {
      if (document.body.dataset.state !== "loading") {
        break;
      }
      const mb = (bytes) => (bytes / 1e6).toFixed(1);
      setStatus(
        "loading",
        `Loading the compiler… ${mb(message.loaded)} of ${mb(message.total)} MB`,
      );
      break;
    }
    case "ready":
      compilerReady = true;
      requestBuild(true);
      break;
    case "failed":
      setStatus("error", `The compiler did not load: ${message.text}`);
      break;
    case "result":
      finishBuild(message);
      break;
  }
});

worker.addEventListener("error", (event) => {
  setStatus("error", `The compiler did not load: ${event.message ?? "error"}`);
});

// ---------------------------------------------------------------------------
// Editor

const darkScheme = matchMedia("(prefers-color-scheme: dark)");
const theme = new Compartment();

const view = new EditorView({
  parent: elements.editor,
  state: EditorState.create({
    doc: saved.source ?? "",
    extensions: [
      basicSetup,
      cpp(),
      lintGutter(),
      theme.of(darkScheme.matches ? oneDark : []),
      Prec.highest(
        keymap.of([
          { key: "Mod-Enter", run: () => (runNow(), true) },
          { key: "Mod-s", run: () => (runNow(), true), preventDefault: true },
        ]),
      ),
      EditorView.updateListener.of((update) => {
        if (!update.docChanged) {
          return;
        }
        const source = update.state.doc.toString();
        save({ source });
        clearTimeout(buildTimer);
        if (elements.auto.checked) {
          buildTimer = setTimeout(() => requestBuild(), buildDelay);
        }
      }),
    ],
  }),
});

darkScheme.addEventListener("change", () => {
  view.dispatch({
    effects: theme.reconfigure(darkScheme.matches ? oneDark : []),
  });
});

function setSource(source) {
  view.dispatch({
    changes: { from: 0, to: view.state.doc.length, insert: source },
  });
}

// ---------------------------------------------------------------------------
// Examples

const examples = new Map();

async function fetchExample(name) {
  if (!examples.has(name)) {
    const response = await fetch(`examples/${name}.c`);
    if (!response.ok) {
      throw new Error(`examples/${name}.c: HTTP ${response.status}`);
    }
    examples.set(name, await response.text());
  }
  return examples.get(name);
}

async function loadExample(name) {
  const source = await fetchExample(name);
  save({ example: name });
  setSource(source);
  runNow();
}

elements.example.addEventListener("change", async () => {
  const name = elements.example.value;
  const current = view.state.doc.toString();
  const previous = await fetchExample(saved.example ?? "hello").catch(() => "");
  if (current !== previous && !confirm(`Replace your code with ${name}.c?`)) {
    elements.example.value = saved.example ?? "hello";
    return;
  }
  loadExample(name).catch((error) => setStatus("error", String(error)));
});

elements.example.value = saved.example ?? "hello";
if (!saved.source) {
  loadExample(elements.example.value).catch((error) =>
    setStatus("error", String(error)),
  );
}

// ---------------------------------------------------------------------------
// Buttons and the splitter

elements.run.addEventListener("click", runNow);
elements.clear.addEventListener("click", clearOutput);
elements.auto.checked = saved.auto ?? true;
elements.auto.addEventListener("change", () => {
  save({ auto: elements.auto.checked });
  if (elements.auto.checked) {
    requestBuild();
  } else {
    clearTimeout(buildTimer);
  }
});

function setSplit(percent) {
  const clamped = Math.min(Math.max(percent, 20), 80);
  document.documentElement.style.setProperty("--split", `${clamped}%`);
  elements.splitter.setAttribute("aria-valuenow", String(Math.round(clamped)));
  return clamped;
}

let split = setSplit(saved.split ?? 50);

elements.splitter.addEventListener("pointerdown", (event) => {
  const panes = elements.splitter.parentElement.getBoundingClientRect();
  elements.splitter.setPointerCapture(event.pointerId);
  // The frame would take the pointer events during the drag.
  elements.preview.style.pointerEvents = "none";
  const move = (moveEvent) => {
    split = setSplit(((moveEvent.clientX - panes.left) / panes.width) * 100);
  };
  const stop = () => {
    elements.splitter.removeEventListener("pointermove", move);
    elements.preview.style.pointerEvents = "";
    save({ split });
  };
  elements.splitter.addEventListener("pointermove", move);
  elements.splitter.addEventListener("lostpointercapture", stop, { once: true });
});

elements.splitter.addEventListener("keydown", (event) => {
  const step = { ArrowLeft: -2, ArrowRight: 2 }[event.key];
  if (step) {
    event.preventDefault();
    split = setSplit(split + step);
    save({ split });
  }
});

// ---------------------------------------------------------------------------
// Browser support

if (
  typeof DecompressionStream === "undefined" ||
  typeof WebGL2RenderingContext === "undefined"
) {
  setStatus("error", "This browser lacks WebGL 2 or DecompressionStream.");
}

// For the tests: they set the code and read the state.
window.pxlPlayground = {
  setSource,
  get source() {
    return view.state.doc.toString();
  },
  get state() {
    return document.body.dataset.state;
  },
  get builds() {
    return finishedBuilds;
  },
};
