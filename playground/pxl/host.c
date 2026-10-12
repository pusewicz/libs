// The host of the pxl playground: runs a program that the playground
// compiled.
//
// The playground compiles each program into a side module and writes it to
// /program.wasm. The host has SDL, the GPU layer (gpu.c) and the
// implementation of pxl.h, so the program does not compile them. The host
// loads the program, finds its SDL main callbacks and runs them.

#define SDL_MAIN_HANDLED
#define PXL_IMPLEMENTATION
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <emscripten.h>
#include <stdio.h>

#include "pxl.h"

static const char program_path[] = "/program.wasm";
/** After this many frames, the program runs: it does not loop forever. */
static constexpr int running_frames = 30;

static SDL_AppIterate_func program_iterate;
static int frames;

// Tells the preview that the program runs. See running_frames.
// clang-format off
EM_JS(void, report_running, (), {
  Module["onRunning"]?.();
})
// clang-format on

static SDL_AppResult iterate(void* state) {
  SDL_AppResult result = program_iterate(state);
  if (++frames == running_frames) {
    report_running();
  }
  return result;
}

/** Writes the SDL log to stdout and stderr, which go to the playground. */
static void write_log([[maybe_unused]] void* userdata,
                      [[maybe_unused]] int category, SDL_LogPriority priority,
                      const char* message) {
  static const char* const prefixes[] = {
      [SDL_LOG_PRIORITY_WARN]     = "WARN: ",
      [SDL_LOG_PRIORITY_ERROR]    = "ERROR: ",
      [SDL_LOG_PRIORITY_CRITICAL] = "CRITICAL: ",
  };
  const char* prefix =
      (size_t)priority < SDL_arraysize(prefixes) && prefixes[priority]
          ? prefixes[priority]
          : "";
  FILE* stream = priority >= SDL_LOG_PRIORITY_WARN ? stderr : stdout;
  (void)fprintf(stream, "%s%s\n", prefix, message);
}

/**
 * Reports a failed assertion and stops the program. The default handler
 * asks in a dialog, which the sandbox of the preview does not allow.
 */
static SDL_AssertState report_assertion(const SDL_AssertData* data,
                                        [[maybe_unused]] void* userdata) {
  (void)fprintf(stderr, "Assertion failed: %s\n  at %s:%d in %s()\n",
                data->condition, data->filename, data->linenum, data->function);
  return SDL_ASSERTION_ABORT;
}

int main(int argc, char* argv[]) {
  SDL_SetLogOutputFunction(write_log, nullptr);
  SDL_SetAssertionHandler(report_assertion, nullptr);

  SDL_SharedObject* program = SDL_LoadObject(program_path);
  if (!program) {
    (void)fprintf(stderr, "Cannot load the program: %s\n", SDL_GetError());
    return 1;
  }
  SDL_AppInit_func init =
      (SDL_AppInit_func)SDL_LoadFunction(program, "SDL_AppInit");
  program_iterate =
      (SDL_AppIterate_func)SDL_LoadFunction(program, "SDL_AppIterate");
  SDL_AppEvent_func event =
      (SDL_AppEvent_func)SDL_LoadFunction(program, "SDL_AppEvent");
  SDL_AppQuit_func quit =
      (SDL_AppQuit_func)SDL_LoadFunction(program, "SDL_AppQuit");
  if (!init || !program_iterate || !event || !quit) {
    (void)fprintf(stderr,
                  "The program must define SDL_AppInit, SDL_AppIterate, "
                  "SDL_AppEvent and SDL_AppQuit. Define "
                  "SDL_MAIN_USE_CALLBACKS before you include "
                  "<SDL3/SDL_main.h>.\n");
    return 1;
  }
  // On the web, this starts the main loop and returns.
  return SDL_EnterAppMainCallbacks(argc, argv, init, iterate, event, quit);
}
