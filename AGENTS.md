# AGENTS.md — QEmacs Development Guide

## Project Overview

QEmacs is a small but powerful multimode text editor written in C (~100K+ lines).
Originally by Fabrice Bellard (2000), maintained by Charlie Gordon. Licensed under MIT.

This fork builds exclusively with **cosmocc** (Cosmopolitan C compiler) to produce
Actually Portable Executables (APE) that run natively on Linux, macOS, Windows,
FreeBSD, OpenBSD, and NetBSD from a single binary. We should make full use of
cosmopolitan libc features for cross-platform portability.

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
tty.c                Terminal display driver (primary display backend)
unix.c               Unix/POSIX compatibility layer
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
plugin.c             Lua 5.4 plugin/config/eval system (embeds Lua, exposes qe.* API)
third_party/lua/     Vendored Lua 5.4.6 amalgamation from whilp/cosmopolitan
plugins/             Example Lua plugins (.lua files installed to ~/.qe/)
docs/                Developer documentation (testing, debugging, plugins)
Makefile             Single build file (cosmocc-only)
.github/workflows/   CI configuration (ci.yml, release.yml)
```

The HTML rendering library (`libqhtml/`) is present but not compiled by default
(requires `CONFIG_HTML=yes`). Terminal modes, shell, dired, hex, and all
language modules are built.

## Building

Requires **cosmocc** in PATH. Everything is in a single `Makefile`. No
configure step needed — `config.h` is generated automatically.

```bash
make install-cosmocc    # Install cosmocc to /opt/cosmocc (one-time)
export PATH="/opt/cosmocc/bin:$PATH"

make                    # Build qe + tqe (APE binaries)
make test               # Run unit tests
make ci                 # Install cosmocc + build + test + verify
make release            # Build + create GitHub release
make debug              # Debug build
make clean              # Clean build artifacts
```

Output binaries:
- `qe` — full-featured terminal editor (APE — runs on Linux/macOS/Windows/BSD)
- `tqe` — tiny/minimal variant (APE)

## Testing

### Running Tests

```bash
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
- `make ci` — installs cosmocc, builds with cosmocc, runs tests, verifies APE binaries

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

**Required:** cosmocc toolchain + GNU make. Install cosmocc via `make install-cosmocc`
or from the [cosmopolitan releases](https://github.com/whilp/cosmopolitan/releases).
No other external dependencies — everything is compiled into the APE binary.

## Common Workflows

**Build and test the portable binary:**
```bash
make ci
```

**Add a new language syntax module:**
1. Create `lang/yourlang.c` following existing modules as templates
2. Use `qe_module_init()` to register the mode
3. Rebuild — the build system auto-discovers modules in `lang/`

**Run the unit tests:**
```bash
make test
```

**Write a Lua plugin:**
1. Create a `.lua` file (see `plugins/hello.lua` for an example)
2. Use the `qe.*` API to register commands, manipulate buffers, etc.
3. Install to `~/.qe/` for auto-loading, or load with `M-x load-plugin`
4. See `docs/plugin.md` for the full API reference

**Create a release locally:**
```bash
make release
```
