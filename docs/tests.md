# Testing Guide

## Quick Start

```bash
# Run all tests (requires cosmocc in PATH)
make test

# Run from tests/ directory
cd tests && make test

# Run a single test binary
cd tests && make test_cutils && ./test_cutils

# Run benchmarks (not part of test suite)
cd tests && make bench
```

## Test Framework

Tests use a minimal framework defined in `tests/testlib.h`. It provides:

- **`TEST(suite, name)`** — Declare a test case. Registration is automatic via `__attribute__((constructor))`.
- **`testlib_run_all()`** — Run all registered tests. Returns 0 on success, 1 on any failure.

### Assertion Macros

| Macro | Description |
|---|---|
| `ASSERT_TRUE(expr)` | Fails if `expr` is false |
| `ASSERT_FALSE(expr)` | Fails if `expr` is true |
| `ASSERT_EQ(a, b)` | Integer equality (cast to `long long`) |
| `ASSERT_NE(a, b)` | Integer inequality |
| `ASSERT_STREQ(a, b)` | String equality via `strcmp` |
| `ASSERT_STRNE(a, b)` | String inequality |
| `ASSERT_MEMEQ(a, b, n)` | Memory equality via `memcmp` |

Assertions do **not** abort the test. Multiple failures are reported per test, but the test is marked as failed on the first one.

## Writing a New Test

### 1. Create the test file

Create `tests/test_yourmodule.c`:

```c
#include "testlib.h"
#include "yourheader.h"  /* use paths relative to project root via -I.. */

TEST(yourmodule, basic_case) {
    ASSERT_EQ(your_function(1, 2), 3);
}

TEST(yourmodule, edge_case) {
    ASSERT_STREQ(your_string_fn(NULL), "");
}

int main(void) {
    return testlib_run_all();
}
```

### 2. Add the build rule to `tests/Makefile`

**Standalone tests** (single `.c` file, no extra dependencies) need no rule — the default rule handles them:

```makefile
# This already exists and covers simple cases:
test_%: test_%.c testlib.h
    $(CC) $(TEST_CFLAGS) -o $@ $<
```

**Tests with source dependencies** need an explicit rule:

```makefile
test_yourmodule: test_yourmodule.c ../yourmodule.c testlib.h ../yourmodule.h
    $(CC) $(TEST_CFLAGS) -o $@ test_yourmodule.c ../yourmodule.c
```

That's it. The `TESTS` variable auto-discovers all `test_*.c` files — no need to add your test name to a list.

### 3. Handling link dependencies

When testing a module that calls functions from other parts of qemacs, you have two options:

**Option A: Link the real sources.** See `test_buffer` for an example that links `buffer.c`, `cutils.c`, `charset.c`, and several others.

**Option B: Stub out dependencies.** Define minimal stubs in your test file. See `test_buffer.c` lines 16-27 for examples:

```c
void qe_put_error(QEmacsState *qs, const char *fmt, ...) { (void)qs; (void)fmt; }
void put_error(EditState *s, const char *fmt, ...) { (void)s; (void)fmt; }
```

**Option C: Include the `.c` file directly.** This gives access to `static` functions. See `test_session.c`:

```c
#include "../session.c"
```

## Existing Test Files

| File | Tests |
|---|---|
| `test_cutils.c` | String utilities (`pstrcpy`, `pstrcat`, `strstart`, `strend`), path helpers, UTF-8 encode/decode, `DynBuf`, OSC parsing |
| `test_buffer.c` | Buffer insert/delete/read, line navigation, character operations |
| `test_session.c` | Session packet protocol, socket paths, lifecycle |
| `test_embed.c` | Resource embedding via `/zip/` paths (APE-specific) |
| `test_cosmo.c` | Cosmopolitan libc features |
| `test_terminal.c` | Terminal handling |
| `test_unix.c` | Unix/POSIX compatibility |
| `test_html.c` | HTML rendering library |
| `bench_buffer.c` | Buffer performance benchmarks (run with `make bench`, not part of test suite) |

## Compiler Flags

Tests are compiled with:

```
CC       = cosmocc
CFLAGS   = -g -O2 -mcosmo -Wall -I.. -DCONFIG_SESSION_DETACH -DCONFIG_HTML
```

The `-I..` flag means `#include "qe.h"` and `#include "cutils.h"` resolve to project-root headers.

## Debugging a Failing Test

### Read the output

Test output shows failures inline with file and line number:

```
FAIL yourmodule.edge_case
  test_yourmodule.c:15: expected true: result != NULL
```

### Build with debug symbols

Tests are built with `-g` by default. Run under gdb:

```bash
cd tests
make test_cutils
gdb ./test_cutils
(gdb) run
# on failure, set a breakpoint at the test function:
(gdb) break test_yourmodule_edge_case
(gdb) run
```

The test function name is `test_<suite>_<name>` (the `TEST(suite, name)` arguments joined with `_`).

### Isolate a single test

There is no built-in test filter. To run one test in isolation, temporarily comment out other `TEST()` blocks or add an early `return` in `main()` before `testlib_run_all()`.

### Common issues

- **Undefined symbol at link time** — You're missing a source file in the Makefile rule, or need to add a stub.
- **`cosmocc: command not found`** — Run `export PATH="/opt/cosmocc/bin:$PATH"` or `make install-cosmocc` first.
- **Test binary doesn't run on macOS/Windows** — The APE binary may need the `ape` loader installed. On first run, cosmocc APE binaries self-extract.

## CI

GitHub Actions runs `make ci` on every push and PR (`.github/workflows/ci.yml`). This installs cosmocc, builds the project, runs `make test`, and verifies the APE binaries. All tests must pass for CI to be green.

## Benchmarks

Benchmarks live alongside tests but are not run by `make test`. Run them explicitly:

```bash
cd tests
make bench
```

Currently there is one benchmark: `bench_buffer.c` for buffer operation performance.
