# AGENTS.md

## Project

This repository is a collection of single-header C23 libraries for game
development. Each library is one header file.

## Rules

- Keep comments and instructions short, concise, to-the-point and use
  'Simplified Technical English'.
- Make small changes. Each change does one thing.
- Write code for humans first. Make names, structure and format clear.
- Do not add comments that repeat the code.

## Layout

- `<name>.h` - the library. A user needs only this file.
- `tests/<name>/` - the tests for the library.
- `examples/<name>/` - the examples for the library.
- `third_party/` - code from other projects, for tests and examples only.
- `compile_flags.txt` - the compiler flags.
- `build/` - the build output. Do not commit it.

## Commands

Rake drives the build. `rake -T` lists the tasks. Do not use CMake.

- `rake check` - the definition of done: format check, clang-tidy, tests
  and examples. Run it before you finish a change.
- `rake test` - build and run the tests with clang and gcc, with ASan and
  UBSan.
- `rake format` - format the sources.
- `rake fuzz[SECONDS]` - fuzz the parsers with libFuzzer. Clang only.
- `rake sweep[DIRS]` - load all `.ase` and `.aseprite` files in DIRS. Use
  `:` between directories. Do not give it `build/`: some test files there
  are not valid on purpose.

## Build

- Compile with the flags in `compile_flags.txt`, for example
  `$CC @compile_flags.txt -o build/x tests/<name>/x.c`.
- Compile with clang and with gcc. Both must give zero warnings.
- clang-tidy and clangd read `compile_flags.txt` automatically.
- On macOS, use Homebrew LLVM clang and `gcc-16`. Do not use Apple clang. It
  does not support all of C23. Set `CLANG` or `GCC` to use other compilers.
- Run `clang-tidy` on the files that define `<NAME>_IMPLEMENTATION`. On the
  header alone, clang-tidy does not check the implementation.

## Tests

- Use pico_unit (`third_party/pico_unit.h`).
- Do not commit binary test files. A Ruby script in `tests/<name>/` writes
  them to `build/`.
- Test the error paths too: truncated data, bad values and failed
  allocations.

## Header structure

Use the STB pattern:

1. A banner comment: name, purpose, version, `SPDX-License-Identifier: Zlib`
   and short usage.
2. The public API inside the include guard `<NAME>_H`.
3. The implementation inside `#ifdef <NAME>_IMPLEMENTATION`. The user defines
   this macro in one translation unit only.

- Stop the build with `#error` if `__STDC_VERSION__` is less than `202311L`.
- Prefix all public identifiers with `<name>_`. Prefix all macros with
  `<NAME>_`.
- Make all internal functions and data `static`.
- Write a short Doxygen comment for each public declaration. Give the
  purpose, the parameters, the return value and who owns the memory.
- Use only the C standard library. Put optional dependencies behind a
  `<NAME>_` macro.
- Let the user replace the allocation functions that the library uses and
  the assertions, for example with `<NAME>_MALLOC`, `<NAME>_FREE` and
  `<NAME>_ASSERT`.
- Do not use global mutable state. Keep state in a context that the user
  owns.

## C23

Use:

- `nullptr`, not `NULL` or `0`.
- `constexpr` objects for typed constants, not `#define`.
- `enum <name> : <type>` when the size of the enum is important.
- The keywords `bool`, `static_assert`, `alignas`, `alignof` and
  `thread_local`, not the `_Bool` forms.
- `= {}` to set a value to zero.
- Designated initializers.
- `[[nodiscard]]`, `[[maybe_unused]]`, `[[fallthrough]]` and `[[noreturn]]`.
- `ckd_add` and `ckd_mul` from `<stdckdint.h>` to check sizes for overflow.
- `size_t` for sizes and indices.
- `()` for an empty parameter list. In C23 it is the same as `(void)`.
- `typeof` and `_Generic` only when they make the types clearer.

Do not use:

- Variable length arrays. This includes parameters such as `a[static n]`.
  `a[static 1]` is correct.
- `realloc(p, 0)`. It is undefined behavior in C23.
- `<stdbit.h>`, `<threads.h>` and `memset_explicit`. Apple libc does not
  have them.
- `[[unsequenced]]`, `[[reproducible]]` and storage-class compound literals.
  Clang does not support them.
- Compiler extensions. Exception: `[[gnu::...]]` attributes behind
  `__has_c_attribute`, for example `[[gnu::format]]`.
