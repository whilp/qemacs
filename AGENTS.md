# AGENTS.md — QEmacs Development Guide

## Project Overview

QEmacs is a small but powerful multimode text editor written in C (~100K+ lines).
Originally by Fabrice Bellard (2000), maintained by Charlie Gordon. Licensed under MIT.

This fork builds exclusively with **cosmocc** (Cosmopolitan C compiler) to produce
Actually Portable Executables (APE) that run natively on Linux, macOS, Windows,
FreeBSD, OpenBSD, and NetBSD from a single binary. No X11, no libpng, no FFmpeg,
no dynamic plugins — just a lean, maximally portable terminal editor.

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
tty.c                Terminal display driver
unix.c               Unix/POSIX compatibility layer
libunicode.c         Unicode tables and algorithms
libregexp.c          Regular expression engine
lang/                Language syntax modules (56 languages: C, Python, Rust, etc.)
modes/               Editor modes (hex, shell, dired, markdown, orgmode, etc.)
tools/               Build-time code generators (table generators, resource builders)
tests/               Test suite
kmap/                Keyboard mapping files for input methods
cp/                  Character set data files (ISO 8859-x)
Makefile.cosmo       Primary build file for this fork
.github/workflows/   CI configuration (ci.yml, release.yml)
```

## Building

This fork uses `Makefile.cosmo` as the primary build entry point. The cosmocc
toolchain is auto-installed to `/opt/cosmocc` if not already present.

```bash
make -f Makefile.cosmo          # Configure and build with cosmocc
```

This runs `./configure` with cosmocc-appropriate flags (`--disable-x11`,
`--disable-html`, `--disable-plugins`, `--disable-png`, `--disable-ffmpeg`)
and then builds `qe` and `tqe` as APE binaries.

Key targets in `Makefile.cosmo`:
- `make -f Makefile.cosmo` — full build (configure + compile)
- `make -f Makefile.cosmo ci` — install cosmocc + build + verify binaries
- `make -f Makefile.cosmo release` — build + create GitHub release with checksums
- `make -f Makefile.cosmo clean` — clean build artifacts

You can point to a custom cosmocc installation:
```bash
make -f Makefile.cosmo COSMOCC=/path/to/cosmocc/bin
```

Output binaries:
- `qe` — full-featured terminal editor (APE)
- `tqe` — tiny/minimal variant (APE)

Both are single-file executables that run on all supported platforms without
recompilation.

### Local development with system compiler

For fast iteration you can also build with your system compiler:
```bash
./configure && make              # Uses gcc/clang
make debug                       # Debug build with symbols
make asan                        # Address Sanitizer build
make ubsan                       # Undefined Behavior Sanitizer build
```

## Testing

### Running Tests

```bash
./configure          # Must configure first (generates config.h)
make test            # Run the full test suite
```

This delegates to `tests/Makefile`, which compiles and runs unit test binaries.

### Test Structure

- `tests/testlib.h` — Minimal test framework inspired by cosmopolitan's testlib.
  Provides `TEST(suite, name)`, `ASSERT_EQ`, `ASSERT_STREQ`, `ASSERT_TRUE`, etc.
- `tests/test_cutils.c` — Unit tests for `cutils.c` utility functions
- `tests/` also contains manual test data for charset, color, bidi, and terminal tests

### CI

GitHub Actions (`.github/workflows/ci.yml`) runs on every push and PR:
1. **Unit tests** — `./configure && make test` on Ubuntu
2. **Cosmopolitan build** — `make -f Makefile.cosmo ci` (installs cosmocc, builds, verifies)

Releases (`.github/workflows/release.yml`):
- Nightly pre-releases at 6 AM UTC
- Manual full releases via workflow_dispatch
- Produces `qe`, `tqe`, and `SHA256SUMS` as release assets

### Adding Tests

Follow the pattern in `tests/test_cutils.c`:
1. Create `tests/test_yourmodule.c`
2. `#include "testlib.h"` and write tests using `TEST(suite, name)` macro
3. Add the binary name to `TESTS` in `tests/Makefile`
4. Add the compilation rule with its dependencies

Example:
```c
#include "testlib.h"

TEST(example, addition) {
    ASSERT_EQ(1 + 1, 2);
}

int main() { return testlib_run_all(); }
```

## Code Style

- C99, compiled with `-Wall` and strict warnings
- Use `unsigned char` by default (`-funsigned-char` flag)
- Module system: use `qe_module_init()` / `qe_module_exit()` macros
- Language modules go in `lang/`, editor modes in `modes/`
- Follow existing code conventions — no `.editorconfig`

## Dependencies

**Required:** cosmocc toolchain (auto-installed by `make -f Makefile.cosmo`)

For local development: any C compiler (gcc, clang, tcc) + GNU make.

This fork intentionally has **no optional library dependencies**. The cosmocc
build disables X11, PNG, FFmpeg, HTML rendering, and dynamic plugins. Everything
the editor needs is compiled into a single portable binary.

## Cosmopolitan Features

This fork should take full advantage of cosmopolitan libc capabilities:
- **APE format** — single binary runs on 6+ operating systems
- **Built-in platform abstractions** — filesystem, terminal, signals work cross-platform
- **cosmocc toolchain** — pinned version from `whilp/cosmopolitan` releases
- **No external runtime dependencies** — fully self-contained executables

## Common Workflows

**Build and test the portable binary:**
```bash
make -f Makefile.cosmo ci
```

**Add a new language syntax module:**
1. Create `lang/yourlang.c` following existing modules as templates
2. Use `qe_module_init()` to register the mode
3. Rebuild — the build system auto-discovers modules in `lang/`

**Run just the unit tests (fast, uses system compiler):**
```bash
./configure && make -C tests test
```

**Create a release locally:**
```bash
make -f Makefile.cosmo release
```
