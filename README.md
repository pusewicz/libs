# libs

Single-header C23 libraries for game development. Copy one header into your
project. Define `<NAME>_IMPLEMENTATION` in one C file before you include it.
All libraries use the [zlib license](LICENSE).

| Library                    | Description                                   |
| -------------------------- | --------------------------------------------- |
| [aseprite.h](aseprite.h)   | Reads Aseprite files (`.ase`, `.aseprite`).   |

## Development

Install Ruby, clang and gcc 15 or newer. Then run `rake check`.
