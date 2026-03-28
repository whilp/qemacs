# Debugging QEmacs

## Decoding Crash Dumps (Cosmopolitan APE)

When a cosmopolitan binary crashes, it prints a stack trace with raw addresses:

```
error: Uncaught SIGSEGV (SEGV_MAPERR) at 0 on hostname pid 5012 tid 5012
cosmoaddr2line /path/to/qe 42354bc 429d310 429d9fb ...
```

The addresses are useless without symbols. Standard `addr2line` cannot read APE
binaries. Use the debug ELF produced during the build:

```bash
# The build produces qe_g.com.dbg — an ELF with full debug symbols
o/cosmocc/bin/x86_64-linux-cosmo-addr2line -e qe_g.com.dbg -f \
    0x42354bc 0x429d310 0x429d9fb 0x4224bd7
```

This outputs function names and source locations:

```
qe_strstart
cutils.c:127
qe_new_shell_buffer
shell.c:2644
do_shell
shell.c:2738
```

The `.com.dbg` file must come from the **same build** as the crashing binary.
In CI, you can either:
- Download the `.com.dbg` artifact, or
- Copy the `cosmoaddr2line` command from the crash output and run it locally
  against a `.com.dbg` built from the same commit.

### Key files

| File | Description |
|------|-------------|
| `qe_g` | APE binary with some debug info (not readable by standard tools) |
| `qe_g.com.dbg` | x86_64 ELF with full DWARF symbols — use this for addr2line |
| `qe_g.aarch64.elf` | ARM64 ELF with symbols |

## Debugging CI Test Failures

### Adding diagnostics to tests

The test framework (`tests/testlib.h`) has no built-in skip mechanism. When a
test might fail due to environment differences (CI vs local), add diagnostic
output before the assertion:

```c
if (!got_expected_result) {
    fprintf(stderr, "DEBUG: description of what failed\n");
    // print relevant system state
}
ASSERT_TRUE(got_expected_result);
```

### Common CI vs local differences

| Issue | Local | CI (GitHub Actions) |
|-------|-------|---------------------|
| `$TERM` | Set (e.g. `xterm-256color`) | **Not set** (NULL) |
| `/dev/ptmx` | Available | Available |
| Kernel | Varies | Azure kernel (e.g. 6.17.0-1008-azure) |
| seccomp/pledge | May behave differently | May crash (SIGSEGV) instead of returning EPERM |

### Debugging shell mode specifically

Shell mode (`modes/shell.c`) allocates a PTY and forks a child process. Common
failure points:

1. **`getenv("TERM")` returns NULL** — any code passing the result to string
   functions without a NULL check will crash. Always default: `if (!term) term = "";`

2. **PTY allocation fails** — `posix_openpt()` or `ptsname()` may fail. The
   `get_pty()` function handles this gracefully and returns -1.

3. **pledge/seccomp blocks ioctls** — cosmopolitan's `pledge()` installs a
   seccomp-bpf filter. The `pty` promise is needed for `TIOCGPTN` (used by
   `ptsname()`). On some kernels, blocked ioctls cause SIGSEGV instead of EPERM.

### PTY diagnostic snippet

Add this to test code to diagnose PTY issues:

```c
#include <sys/stat.h>
#include <sys/utsname.h>

static void diagnose_pty(void) {
    struct stat st;
    fprintf(stderr, "PTY diagnostics:\n");
    fprintf(stderr, "  /dev/ptmx exists: %s\n",
            stat("/dev/ptmx", &st) == 0 ? "yes" : "no");
    fprintf(stderr, "  /dev/pts exists: %s\n",
            stat("/dev/pts", &st) == 0 ? "yes" : "no");
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    fprintf(stderr, "  posix_openpt: fd=%d errno=%d (%s)\n",
            fd, fd < 0 ? errno : 0, fd < 0 ? strerror(errno) : "ok");
    if (fd >= 0) {
        const char *name = ptsname(fd);
        fprintf(stderr, "  ptsname: %s\n", name ? name : "(null)");
        fprintf(stderr, "  grantpt: %d\n", grantpt(fd));
        fprintf(stderr, "  unlockpt: %d\n", unlockpt(fd));
        close(fd);
    }
    struct utsname uts;
    if (uname(&uts) == 0)
        fprintf(stderr, "  kernel: %s %s\n", uts.sysname, uts.release);
}
```

## Example: The TERM=NULL Crash

**Symptom:** qe crashes with `SIGSEGV (SEGV_MAPERR) at 0` when M-x shell is
invoked on CI, but works locally.

**Debugging steps:**

1. PTY diagnostics showed PTY works fine on CI — ruled out PTY/seccomp issues.

2. Decoded the crash address using `cosmoaddr2line` with `qe_g.com.dbg`:
   ```
   qe_strstart     <- crash here (NULL dereference)
   qe_new_shell_buffer
   do_shell
   ```

3. Found `qe_term_init()` calls `getenv("TERM")` which returns NULL on CI,
   then passes it to `strstart()` which dereferences it.

4. Fix: `if (!term) term = "";`

**Lesson:** Always NULL-check `getenv()` results before passing to string
functions. CI environments often lack variables that are always set in
interactive terminals.
