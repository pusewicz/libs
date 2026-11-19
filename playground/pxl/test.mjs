// Tests the playground in headless Chromium:
//
// 1. The tests of pxl and of the GPU layer (gpu.c) run on the GPU layer and
//    must pass.
// 2. The page compiles hello.c and runs it. A syntax error and an undefined
//    function keep the program running and show the errors. A fix runs the
//    new code.
// 3. After a program that loops forever, the next visit does not run it.
//
//   node test.mjs build/playground

import { chromium } from "playwright";

import { serve } from "./serve.mjs";

const root = process.argv[2] ?? "build/playground";
const server = await serve(root, 0);
const base = `http://localhost:${server.address().port}`;
// Without a GPU, WebGL needs SwiftShader.
const browser = await chromium.launch({
  args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"],
});
let failures = 0;

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function check(name, test) {
  // Each test has its own storage. Closing the context closes a frozen page.
  const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const page = await context.newPage();
  const log = [];
  page.on("console", (message) => log.push(message.text()));
  page.on("pageerror", (error) => log.push(`page error: ${error.message}`));
  try {
    await test(page, log);
    console.log(`ok - ${name}`);
  } catch (error) {
    failures++;
    console.log(`FAILED - ${name}: ${error.message}\n${log.join("\n")}`);
  } finally {
    await Promise.race([context.close(), delay(10_000)]);
  }
}

for (const program of ["test_pxl", "test_options", "test_gpu"]) {
  await check(program, async (page, log) => {
    await page.goto(`${base}/tests/test.html?program=${program}`);
    await page.waitForFunction(() => window.exitStatus !== undefined, null, {
      timeout: 120_000,
    });
    const status = await page.evaluate(() => window.exitStatus);
    console.log(log.filter((line) => /Summary|GPU driver|skipped/.test(line)).join("\n"));
    if (status !== 0) {
      throw new Error(`exit status ${status}`);
    }
    if (log.some((line) => /skipped/.test(line))) {
      throw new Error("the GPU tests were skipped");
    }
  });
}

/** Waits until the playground finishes a build after `count` builds. */
async function waitForBuild(page, count, state) {
  await page.waitForFunction(
    ([builds, wanted]) =>
      window.pxlPlayground?.builds > builds && window.pxlPlayground.state === wanted,
    [count, state],
    { timeout: 180_000 },
  );
}

/** Counts the pixels of the preview that have a color. */
async function countColor(page, [r, g, b]) {
  // Let the program draw a few frames.
  await page.waitForTimeout(500);
  const image = await page.locator("#preview iframe").screenshot();
  return page.evaluate(
    async ([png, color]) => {
      const bitmap = await createImageBitmap(
        await (await fetch(`data:image/png;base64,${png}`)).blob(),
      );
      const canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
      const context = canvas.getContext("2d");
      context.drawImage(bitmap, 0, 0);
      const { data } = context.getImageData(0, 0, bitmap.width, bitmap.height);
      let count = 0;
      for (let i = 0; i < data.length; i += 4) {
        const near = (a, b) => Math.abs(a - b) <= 2;
        if (near(data[i], color[0]) && near(data[i + 1], color[1]) && near(data[i + 2], color[2])) {
          count++;
        }
      }
      return count;
    },
    [image.toString("base64"), [r, g, b]],
  );
}

async function expectColor(page, color) {
  const count = await countColor(page, color);
  if (count < 1000) {
    throw new Error(`the preview has ${count} pixels of rgb(${color})`);
  }
}

/**
 * Replaces text in the code in one change, so that no build sees half of
 * it. Returns the number of builds before the change.
 */
async function edit(page, ...replacements) {
  const builds = await page.evaluate(() => window.pxlPlayground.builds);
  const missing = await page.evaluate((pairs) => {
    let source = window.pxlPlayground.source;
    for (const [from, to] of pairs) {
      if (!source.includes(from)) {
        return from;
      }
      source = source.replace(from, to);
    }
    window.pxlPlayground.setSource(source);
    return null;
  }, replacements);
  if (missing !== null) {
    throw new Error(`the code has no ${JSON.stringify(missing)}`);
  }
  return builds;
}

async function output(page) {
  return page.locator("#output").textContent();
}

await check("playground", async (page) => {
  await page.goto(`${base}/site/pxl/`);
  await waitForBuild(page, 0, "running");
  await expectColor(page, [0x1d, 0x2b, 0x53]);

  let builds = await edit(page, ["pxl_end_frame(a->pxl);", "pxl_end_frame(a->pxl)"]);
  await waitForBuild(page, builds, "error");
  if (!/error: expected ';'/.test(await output(page))) {
    throw new Error(`no syntax error in the output:\n${await output(page)}`);
  }
  if ((await page.locator(".cm-gutter-lint .cm-lint-marker-error").count()) === 0) {
    throw new Error("the editor does not mark the error");
  }
  await expectColor(page, [0x1d, 0x2b, 0x53]);

  builds = await edit(
    page,
    ["pxl_end_frame(a->pxl)\n", "pxl_end_frame(a->pxl);\n  missing();\n"],
    ["#include", "void missing(void);\n#include"],
  );
  await waitForBuild(page, builds, "error");
  if (!/undefined symbol: missing/.test(await output(page))) {
    throw new Error(`no link error in the output:\n${await output(page)}`);
  }

  builds = await edit(
    page,
    ["  missing();\n", ""],
    ["void missing(void);\n", ""],
    ["0x1d2b53", "0x804020"],
  );
  await waitForBuild(page, builds, "running");
  await expectColor(page, [0x80, 0x40, 0x20]);
});

// A program that loops forever. If the browser runs the frame in its own
// process, the watchdog of the page stops it. Else the loop freezes the page,
// and the next visit must not run the program again.
await check("loop guard", async (page) => {
  await page.goto(`${base}/site/pxl/`);
  await waitForBuild(page, 0, "running");
  const builds = await edit(page, [
    "SDL_AppResult SDL_AppIterate(void* state) {",
    "SDL_AppResult SDL_AppIterate(void* state) {\n  volatile int spin = 0;\n  while (spin == 0) {\n  }",
  ]);
  await page.waitForFunction((count) => window.pxlPlayground.builds > count, builds);
  // On a frozen page, the timeout of waitForFunction does not work.
  const stopped = await Promise.race([
    page
      .waitForFunction(
        () => window.pxlPlayground.state === "error" && !document.querySelector("#preview iframe"),
      )
      .then(
        () => true,
        () => false,
      ),
    delay(10_000).then(() => false),
  ]);
  if (stopped) {
    return;
  }
  const next = await page.context().newPage();
  try {
    await next.goto(`${base}/site/pxl/`);
    await waitForBuild(next, 0, "error");
    if ((await next.locator("#preview iframe").count()) !== 0) {
      throw new Error("the next visit runs the program again");
    }
  } finally {
    await next.close();
  }
});

await browser.close();
server.close();
console.log(failures ? `${failures} failed` : "all passed");
process.exit(failures ? 1 : 0);
