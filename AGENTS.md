# AGENTS.md — QEmacs Development Guide

## Project Overview

QEmacs is a small but powerful multimode text editor written in C (~100K+ lines).
Started in 2000 by Fabrice Bellard, maintained by Charlie Gordon. Licensed under MIT.

## Repository Layout

```
qe.h, qe.c          Core editor: data structures, entry point, main loop
buffer.c             Buffer management (largest single file)
display.c/h          Screen rendering and display
charset.c/h          Character set and encoding support
cutils.c/h           Utility functions (string, memory, etc.)
color.c/h            Color management and themes
search.c             Search and replace
input.c              Input handling and key bindings
libunicode.c         Unicode tables and algorithms
libregexp.c          Regular expression engine
lang/                Language syntax modules (56 languages: C, Python, Rust, etc.)
modes/               Editor modes (hex, shell, dired, markdown, orgmode, etc.)
libqhtml/            HTML parsing and rendering library
tools/               Build-time code generators (table generators, resource builders)
tests/               Test suite
kmap/                Keyboard mapping files for input methods
cp/                  Character set data files (ISO 8859-x)
fonts/               Bitmap font files (.fbf format)
plugins/             Plugin development examples
.github/workflows/   CI configuration (ci.yml, release.yml)
```

## Building

```bash
./configure          # Generates config.h and config.mak
make                 # Build all configured versions (qe, xqe, tqe)
```

Key build targets:
- `make qe` — terminal version
- `make xqe` — X11 GUI version
- `make tqe` — tiny/embedded version
- `make debug` — unoptimized build with debug symbols
- `make clean` / `make distclean`
- `make install`

Sanitizer builds: `make asan`, `make msan`, `make ubsan`

Static analysis: `make splint`

Cross-platform portable build: `make -f Makefile.cosmo`

## Testing

### Running Tests

```bash
./configure          # Must configure first (generates config.h)
make test            # Run the full test suite
```

This delegates to `tests/Makefile`, which compiles and runs unit test binaries.

### Test Structure

- `tests/test_cutils.c` — Unit tests for `cutils.c` utility functions using `tests/testlib.h`
- `tests/` also contains manual test data files for charset, color, bidi, and terminal tests

### CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and PR:
1. **Unit tests** — `./configure && make test` on Ubuntu
2. **Cosmopolitan build** — `make -f Makefile.cosmo ci` (portable executable)

### Adding Tests

New test files should follow the pattern in `tests/test_cutils.c`:
- Include `testlib.h` for test macros
- Add the new test binary name to `TESTS` in `tests/Makefile`
- Add the compilation rule with dependencies

## Code Style

- C99, compiled with `-Wall` and strict warnings
- No `.editorconfig` — follow existing code conventions
- Use `unsigned char` by default (`-funsigned-char` flag)
- Module system: use `qe_module_init()` / `qe_module_exit()` macros to register modules
- Language modules go in `lang/`, editor modes in `modes/`

## Dependencies

Required: C compiler (gcc, clang, or tcc), GNU make

Optional:
- libX11/libXrender/libXv/libXext — X11 GUI support
- libpng — PNG image support
- FFmpeg (libavformat/libavcodec) — audio/video support

The `./configure` script auto-detects all optional dependencies.

## Common Workflows

**Add a new language syntax module:**
1. Create `lang/yourlang.c` following existing modules as templates
2. Use `qe_module_init()` to register the mode
3. Rebuild — the build system auto-discovers modules in `lang/`

**Debug build with Address Sanitizer:**
```bash
./configure && make asan
```

**Run just the unit tests without a full build:**
```bash
make -C tests test
```
