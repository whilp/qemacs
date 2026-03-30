# Fork Differences: whilp/qemacs vs Upstream

This document describes the major differences between the `whilp/qemacs` fork
and Charlie Gordon's upstream QEmacs. The fork transforms QEmacs from a
multi-platform graphical editor into a **terminal-only, single-binary portable
editor** built exclusively with [Cosmopolitan libc](https://github.com/jart/cosmopolitan).

## Philosophy

Upstream QEmacs supports multiple display backends (X11, Win32, terminal),
optional multimedia via FFmpeg, dynamic plugins, and a flexible `./configure`
build system. This fork strips all of that away in favor of:

- **One build target**: a single APE (Actually Portable Executable) binary
- **One display backend**: TTY only (optimized for Ghostty)
- **One toolchain**: cosmocc (Cosmopolitan C compiler)
- **Zero external dependencies**: everything compiles into one file

The result is an editor binary that runs unmodified on Linux, macOS, Windows,
FreeBSD, OpenBSD, and NetBSD.

---

## Major Features Added

### Session Detach/Reattach (`session.c`, ~1100 lines)

A tmux/screen-like session layer built directly into the editor. Uses a
PTY-proxy architecture inspired by [abduco](https://github.com/martanne/abduco)
with Unix domain sockets and a custom binary packet protocol.

- `qe --session-resume NAME` — resume or create a named session (default)
- `qe --session-create NAME` — create a new session
- `qe --session-attach NAME` — attach to an existing session
- `qe --session-list` — list active sessions
- `Ctrl-\` — detach from the current session
- `M-x session-list` — browse sessions from within the editor

Sessions survive terminal disconnects and support multiple concurrent clients.
The server process manages the editor while lightweight client processes proxy
terminal I/O over the socket.

### Clipboard via OSC 52 (`tty.c`)

System clipboard integration through terminal escape sequences, replacing
the X11 selection mechanism. The kill ring syncs bidirectionally with the
system clipboard using asynchronous, non-blocking OSC 52 requests. Works in
remote SSH sessions and containers where no windowing system is available.

### 24-bit True Color (`tty.c`)

`TTYChar` expanded from `uint32_t` to `__uint128_t` to store full 24-bit RGB
foreground and background colors per cell. Upstream QEmacs had 24-bit color
support via X11; this brings equivalent color fidelity to the terminal.

### Modern Terminal UI (`tty.c`, `qe.c`)

- Unicode box-drawing characters for window borders (thin lines: `─│┌┐└┘`)
  replacing the ASCII `|` and `-` separators from upstream
- OSC 7 for terminal working directory tracking
- Back Color Erase (BCE) support
- Terminal title updates

### Markdown Render Mode (`modes/mkd_render.c`, ~800 lines)

A read-only rendered view of markdown files toggled with `M-x mkd-render-mode`
or `C-c C-r`. Displays styled headings, concealed markup characters, Unicode
list bullets, blockquote bars, and horizontal rules — all within the terminal.

### Resource Embedding

Build-time resources (keyboard maps, ligatures, documentation) are embedded
into the APE binary via zip. At runtime they're accessible through
Cosmopolitan's `/zip/` virtual filesystem, making the editor fully
self-contained with no need for a `$PREFIX/share/qe/` data directory.

---

## Major Features Removed

### X11 Display Backend

The entire X11 graphics driver is gone. This removes:

- Xlib/Xft font rendering (including proportional fonts)
- X Input Method support
- Window manager integration (fullscreen, borderless)
- Graphical image display
- Mouse pointer integration via X11 events

The TTY driver is now the sole display backend.

### Win32 Display Backend

No native Windows console or GDI rendering. Windows support comes
exclusively through Cosmopolitan's POSIX emulation layer running in the
terminal.

### FFmpeg/Multimedia

The `CONFIG_FFMPEG` code paths are disabled. No video or audio playback.
Image viewing is limited to `stb_image.h` decoded to terminal-compatible
output.

### Dynamic Plugin Loading

No `dlopen()` or `CONFIG_DLL` support. The `plugins/` directory exists as
examples only. All functionality is statically linked into the binary.

### Configure Script

The `./configure` step is eliminated entirely. The single `Makefile` hardcodes
all feature flags for the cosmocc build. There are no build-time choices to
make — you get one configuration.

### Tiny Build Variant

The `CONFIG_TINY` / `tqe` stripped-down build was removed. Only the
full-featured `qe` binary is produced.

---

## Build System Overhaul

### Before (Upstream)

```
./configure [options]   # detect platform, libraries, features
make                    # build with system cc
```

Feature flags were toggled via `./configure --enable-*` and `--disable-*`.
The build could target gcc, clang, or MSVC with platform-specific backends.

### After (This Fork)

```
make                    # auto-fetches cosmocc, builds APE binary
```

Key changes:

- **Auto-fetch toolchain**: cosmocc is downloaded and SHA256-verified on first
  build. No manual setup required beyond `make`.
- **Parallel by default**: `MAKEFLAGS += -j$(nproc)` is set automatically.
- **Auto-generated dependencies**: `-MMD -MP` flags produce `.d` files so
  incremental builds only recompile what changed.
- **Module auto-discovery**: `tools/gen-modules.sh` scans source files for
  `qe_module_init` calls and generates `qe_modules.c` automatically, replacing
  the hand-maintained module list.
- **Single output directory**: all build artifacts go to `o/` with clean
  separation of object files, generated code, and binaries.

### Feature Flags

All features are compile-time constants (no runtime detection):

| Flag | Status | Notes |
|------|--------|-------|
| `CONFIG_SESSION_DETACH` | **Enabled** | New in this fork |
| `CONFIG_HTML` | **Enabled** | libqhtml compiled in |
| `CONFIG_ALL_MODES` | **Enabled** | All editor modes |
| `CONFIG_ALL_KMAPS` | **Enabled** | All keyboard maps |
| `CONFIG_NETWORK` | **Enabled** | Basic socket support |
| `CONFIG_MMAP` | **Enabled** | Memory-mapped I/O |
| `CONFIG_UNICODE_JOIN` | **Enabled** | Arabic/Indic joining |
| `CONFIG_X11` | **Removed** | — |
| `CONFIG_WIN32` | **Removed** | — |
| `CONFIG_FFMPEG` | **Removed** | — |
| `CONFIG_DLL` | **Removed** | — |
| `CONFIG_TINY` | **Removed** | — |

---

## Testing Infrastructure

Upstream QEmacs had no test suite. This fork adds comprehensive testing:

### Test Framework (`tests/testlib.h`)

Minimal C test framework inspired by Cosmopolitan's testlib. Provides
`TEST(suite, name)` macros with auto-registration via GCC constructor
attributes, plus assertions (`ASSERT_EQ`, `ASSERT_STREQ`, `ASSERT_MEMEQ`,
etc.) and filtering via `TESTLIB_FILTER` environment variable.

### Test Coverage

| Test File | What It Tests |
|-----------|---------------|
| `test_cutils.c` | String utilities, UTF-8 handling, OSC parsing |
| `test_buffer.c` | Buffer operations |
| `test_session.c` | Session protocol, attach/detach/list |
| `test_terminal.c` | Terminal I/O and rendering |
| `test_tty_input.c` | TTY input parsing |
| `test_ttychar.c` | TTYChar packing/unpacking |
| `test_embed.c` | Resource embedding via `/zip/` paths |
| `test_html.c` | HTML mode rendering |
| `test_mkd_render.c` | Markdown render mode |
| `test_unix.c` | Unix integration layer |
| `test_split_window.c` | Window splitting behavior |
| `test_cosmo.c` | Cosmopolitan-specific behavior |
| `bench_buffer.c` | Buffer performance benchmarks |

### Headless Display Driver (`test_display.c`)

A ~700-line dummy display driver that allows the full editor to run in tests
without a terminal, enabling integration tests for rendering, window
management, and terminal sessions.

### Lint Target

`make lint` detects unbounded blocking I/O patterns (like `poll(..., -1)`) that
cause deadlocks, with an allowlist for the session event loop.

---

## CI/CD

### Continuous Integration (`.github/workflows/ci.yml`)

Runs `make ci` (build + test + lint + binary verification) on every push to
master and every pull request.

### Automated Releases (`.github/workflows/release.yml`)

- Nightly pre-releases at 06:00 UTC
- Manual full releases via `workflow_dispatch`
- Produces `qe` (APE binary) and `SHA256SUMS` as release assets

---

## Terminal Driver Simplifications

The TTY driver assumes [Ghostty](https://ghostty.org) as the primary terminal
target (commit ffb286c). This allowed removing cross-terminal compatibility
hacks:

- Always assumes true color support (no 256-color fallback negotiation)
- Always assumes UTF-8 encoding
- Simplified escape sequence handling
- Non-ASCII binary character display disabled to fix alignment in screen
  sessions

---

## Summary

This fork trades breadth of platform support (X11, Win32, SDL) for depth of
terminal integration (sessions, OSC 52 clipboard, true color, modern UI) and
deployment simplicity (single portable binary, zero dependencies, one-command
build). The resulting editor is optimized for terminal-centric workflows —
SSH sessions, containers, remote development — where a lightweight, instantly
available, self-contained editor is more valuable than graphical features.
