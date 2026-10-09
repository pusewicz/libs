// Loads Aseprite files and checks the result. Usage: sweep FILE...

#define ASEPRITE_IMPLEMENTATION
#include "aseprite.h"
#include "walk.h"

int main(int argc, char** argv) {
  int failed = 0;
  for (int i = 1; i < argc; i++) {
    aseprite_sprite sprite;
    aseprite_result result = aseprite_load_file(argv[i], nullptr, &sprite);
    if (result != ASEPRITE_OK) {
      printf("%s: %s\n", argv[i], aseprite_result_string(result));
      failed++;
      continue;
    }
    volatile uint64_t sum = walk_sprite(&sprite);
    (void)sum;
    aseprite_free(&sprite);
  }
  printf("%d files, %d failed\n", argc - 1, failed);
  return failed > 0 ? 1 : 0;
}
