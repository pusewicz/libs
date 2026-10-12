// Runs one program in the preview frame. The playground sends the program;
// this page sends back its output and how it ended. The frame is sandboxed,
// so it talks to the playground only with messages.

import createHost from "./host.mjs";

const canvas = document.getElementById("canvas");

function send(message) {
  parent.postMessage(message, "*");
}

function reportError(error) {
  send({ type: "crash", text: String(error?.stack ?? error?.message ?? error) });
}

addEventListener("error", (event) => reportError(event.error ?? event.message));
addEventListener("unhandledrejection", (event) => reportError(event.reason));

addEventListener("message", async (event) => {
  if (event.source !== parent || event.data?.type !== "run") {
    return;
  }
  const program = new Uint8Array(event.data.program);
  try {
    await createHost({
      canvas,
      preRun: [(host) => host.FS.writeFile("/program.wasm", program)],
      print: (text) => send({ type: "stdout", text }),
      printErr: (text) => send({ type: "stderr", text }),
      onRunning: () => send({ type: "running" }),
      onExit: (status) => send({ type: "exit", status }),
      onAbort: (reason) => reportError(reason),
    });
    send({ type: "started" });
  } catch (error) {
    reportError(error);
  }
});

// The playground stops the program if these stop: then a loop never ends.
setInterval(() => send({ type: "alive" }), 1000);

send({ type: "ready" });
