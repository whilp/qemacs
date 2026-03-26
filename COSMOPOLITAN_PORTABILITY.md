# Cosmopolitan Libc Portability Opportunities for QEmacs

Research based on analysis of `whilp/cosmopolitan` and the current qemacs codebase.

## Current State

QEmacs already uses cosmopolitan in a minimal way:
- Builds with `cosmocc` via `Makefile`
- Uses `ShowCrashReports()` behind `#ifdef __COSMOPOLITAN__` (qe.c:29-31)
- Produces APE binaries that run on Linux, macOS, Windows, FreeBSD, OpenBSD, NetBSD

There is significant untapped potential in cosmopolitan libc.

---

## High-Impact Opportunities

### 1. Security Hardening with `pledge()` and `unveil()`

**What:** Cosmopolitan provides OpenBSD-style `pledge()` and `unveil()` that work
cross-platform (Linux via seccomp, OpenBSD natively, no-op elsewhere).

**Where:** Add to `qe.c:main()` after initialization.

**Why:** A text editor should only need specific syscall categories. This provides
defense-in-depth against code execution vulnerabilities.

```c
#ifdef __COSMOPOLITAN__
#include <libc/calls/pledge.h>

// After initialization, restrict to:
// - stdio: basic I/O
// - rpath/wpath/cpath: filesystem access
// - tty: terminal control
// - proc: fork/exec for shell mode
// - unix: unix domain sockets (for session detach)
pledge("stdio rpath wpath cpath tty proc exec unix", NULL);

// Restrict filesystem visibility
unveil("/", "r");           // read everywhere
unveil(home_dir, "rwc");    // write only in home
unveil("/tmp", "rwc");      // write in tmp
unveil(NULL, NULL);         // lock it down
#endif
```

**Complexity:** Low. Additive change, no existing code modified.

### 2. Zip Asset Embedding for Built-in Resources

**What:** APE files are also zip archives. Files can be embedded in the binary and
accessed via the `/zip/` path prefix using standard `open()`/`read()` calls.

**Where:** Replace file-based resource loading for:
- `kmap/` keyboard mapping files (currently loaded from filesystem at runtime)
- `cp/` character set data files
- Default config/qerc files
- Help text / documentation

**Why:** Makes qemacs truly self-contained. Currently, kmap and cp files must be
installed alongside the binary. With zip embedding, a single `qe` binary contains
everything.

```c
// Current: searches filesystem paths for kmap files
// Proposed: try /zip/ first, fall back to filesystem
static int load_resource(const char *name, ...) {
    char zip_path[256];
    snprintf(zip_path, sizeof(zip_path), "/zip/%s", name);
    int fd = open(zip_path, O_RDONLY);
    if (fd < 0)
        fd = open(name, O_RDONLY);  // filesystem fallback
    ...
}
```

Build integration in `Makefile`:
```makefile
# After building qe, embed resources
zip qe kmap/*.kmap cp/*.cp config/default.qerc
```

**Complexity:** Medium. Requires modifying resource loading paths and build system.

### 3. Eliminate `#ifdef CONFIG_WIN32` Clock Code

**What:** Cosmopolitan provides POSIX-compatible `gettimeofday()` and
`clock_gettime()` on all platforms, including Windows.

**Where:** `unix.c:354-380` — `get_clock_ms()` and `get_clock_usec()`

**Current code:**
```c
int get_clock_ms(void) {
#ifdef CONFIG_WIN32
    struct _timeb tb;
    _ftime(&tb);
    return tb.time * 1000 + tb.millitm;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + (tv.tv_usec / 1000);
#endif
}
```

**Proposed:** Under cosmocc, just use the POSIX path unconditionally. Even better,
use `clock_gettime(CLOCK_MONOTONIC)` which cosmopolitan provides on all platforms
and avoids wall-clock jumps:

```c
int get_clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}
```

**Complexity:** Very low. Direct replacement.

### 4. Replace `select()` with Cosmopolitan's Cross-Platform I/O

**What:** Cosmopolitan provides `select()`, `pselect()`, and `poll()` that work
identically on all platforms (including Windows, where they're polyfilled).

**Where:** `unix.c` event loop (`url_block()`)

**Why:** The current event loop uses `select()` with `#ifdef CONFIG_WIN32` for the
fd type (`u_int fdesc_t` vs `int fdesc_t`). Under cosmopolitan, this ifdef is
unnecessary — just use `int` everywhere.

**Complexity:** Very low. Remove the typedef ifdef.

### 5. Use `GetProgramExecutableName()` for Self-Location

**What:** Cosmopolitan provides `GetProgramExecutableName()` which reliably returns
the path to the running binary on all platforms.

**Where:** Resource loading, help file paths, session management.

**Why:** Currently qemacs has various heuristics for finding its own location
(argv[0], `/proc/self/exe`, etc.). Cosmopolitan handles this portably.

```c
#ifdef __COSMOPOLITAN__
    const char *exe = GetProgramExecutableName();
#endif
```

**Complexity:** Very low.

### 6. `cosmo_cpu_count()` for Parallel Operations

**What:** Returns the number of CPU cores, cross-platform.

**Where:** Could be used for parallel grep, large file operations, or background
tasks in dired mode.

**Current issue:** `modes/dired.c` has a TODO: "should use separate thread to make
scan asynchronous" (line 1251).

**Complexity:** Low for the API call; medium for actually adding parallelism.

---

## Medium-Impact Opportunities

### 7. Crash Reporting Enhancement

**What:** `ShowCrashReports()` is already used, but cosmopolitan also provides:
- `--strace` flag: logs all system calls (built into every cosmo binary)
- `--ftrace` flag: logs all function calls
- `CheckForMemoryLeaks()` / `CheckForFileLeaks()` for debug builds
- `kprintf()` for async-signal-safe formatted output

**Where:** Add `CheckForMemoryLeaks()` and `CheckForFileLeaks()` to debug builds.
Document `--strace` and `--ftrace` as diagnostic tools.

```c
#ifdef __COSMOPOLITAN__
    // Already exists:
    ShowCrashReports();

    // Add for debug:
    atexit(CheckForMemoryLeaks);
    atexit(CheckForFileLeaks);
#endif
```

**Complexity:** Very low.

### 8. Use `cosmo_once()` for Thread-Safe Initialization

**What:** `cosmo_once()` provides thread-safe one-time initialization (like
`pthread_once` but lighter weight).

**Where:** Module initialization, charset table setup, color table setup.

**Why:** If qemacs ever adds threading (e.g., background file loading, async
completion), having thread-safe init patterns ready would help.

**Complexity:** Low.

### 9. Use Cosmopolitan String Extensions

**What:** Cosmopolitan provides useful string functions under `-mcosmo`:
- `startswith()`, `endswith()` — already used in cosmo's own ttyinfo example
- `startswithi()` — case-insensitive prefix check
- `chomp()` — remove trailing newline
- `appendf()`, `appends()`, `appendd()` — efficient string building

**Where:** Replace hand-rolled equivalents in `cutils.c` and throughout the codebase.

**Current code in qemacs:**
- `strstart()` in cutils.c — similar to `startswith()` but also returns suffix pointer
- `strend()` in cutils.c — similar to `endswith()`
- Various manual string building with `snprintf` chains

**Assessment:** qemacs' `strstart()` is actually *more functional* than cosmo's
`startswith()` because it returns the pointer past the match. Keep qemacs' versions
but could use cosmo's where the bool-only version suffices.

**Complexity:** Low, but marginal benefit.

### 10. `cosmo_permalloc()` for Permanent Allocations

**What:** `cosmo_permalloc()` is an allocator for memory that is never freed. It's
more efficient than `malloc()` for permanent allocations (no bookkeeping overhead).

**Where:** Module registration, mode definitions, charset tables — all allocated at
startup and never freed.

**Why:** Slight memory efficiency improvement and makes intent clear.

**Complexity:** Low.

---

## Lower-Impact / Future Opportunities

### 11. DSP/TTY Library

Cosmopolitan has a `dsp/tty/` library with:
- `ttyraw()` — simplified raw mode setup
- `ttyident()` — terminal identification via escape sequences
- `ttymove()` — optimized cursor movement
- `rgb2xterm256()` / `rgb2ansi()` — color quantization

qemacs already has comprehensive TTY handling in `tty.c` (~2500 lines) that is more
feature-complete. The cosmo dsp/tty lib is simpler but less capable. **Not worth
switching**, but individual algorithms (like color quantization) could be studied.

### 12. `-mtiny` Build Mode

**What:** Cosmopolitan's `-mtiny` flag aggressively optimizes for size.

**Where:** `Makefile` for the `tqe` (tiny qemacs) build.

**Why:** Could make `tqe` even smaller. Currently the tiny build uses `-Os`.

```makefile
# For tqe only:
TINY_CFLAGS += -mtiny
```

**Complexity:** Very low. Just a flag change.

### 13. `LoadZipArgs()` for Embedded Default Configuration

**What:** `LoadZipArgs()` reads command-line arguments from a `/zip/.args` file
embedded in the binary.

**Where:** Could embed default editor configuration.

**Why:** Allows distributing a pre-configured qemacs without external files.

**Complexity:** Low.

### 14. `cosmo_stack_alloc()` / `cosmo_stack_free()` for Coroutines

**What:** Efficient stack allocation for user-space threads/coroutines.

**Where:** If qemacs ever implements cooperative multitasking for async operations.

**Complexity:** High. Would require architectural changes.

---

## Summary: Recommended Priority Order

| # | Opportunity | Impact | Effort | Priority |
|---|-----------|--------|--------|----------|
| 1 | pledge/unveil security | High | Low | **Do first** |
| 2 | Zip asset embedding | High | Medium | **Do second** |
| 3 | Monotonic clock | Medium | Very low | **Quick win** |
| 4 | Remove WIN32 ifdefs (under cosmo) | Low | Very low | **Quick win** |
| 5 | GetProgramExecutableName | Medium | Very low | **Quick win** |
| 6 | cosmo_cpu_count for parallelism | Medium | Medium | Plan for later |
| 7 | Enhanced crash/leak reporting | Medium | Very low | **Quick win** |
| 8 | cosmo_once for thread safety | Low | Low | When adding threads |
| 9 | String extensions | Low | Low | Case by case |
| 10 | cosmo_permalloc | Low | Low | Nice to have |
| 11 | DSP/TTY color algorithms | Low | Low | Study only |
| 12 | -mtiny for tqe | Low | Very low | **Quick win** |
| 13 | LoadZipArgs for config | Low | Low | Nice to have |
| 14 | Stack alloc for coroutines | Low | High | Future |

## Key Principle

The biggest wins come from using cosmopolitan features that **eliminate code**
(removing platform ifdefs) or **add capabilities that would otherwise require
significant platform-specific code** (pledge, unveil, zip assets). The string
utilities and allocators are nice but marginal since qemacs already has good
equivalents.

The single most impactful change would be **zip asset embedding** — it transforms
qemacs from "portable binary + data files" to "single self-contained binary,"
which is the core promise of cosmopolitan.
