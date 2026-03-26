# Plan: Cosmopolitan-Only Build

Drop all non-cosmopolitan platform support. Use cosmo features to simplify code.

## Phase 1: Clock — Replace platform-specific timing with CLOCK_MONOTONIC

- [ ] Write test for get_clock_ms / get_clock_usec (RED)
- [ ] Remove CONFIG_WIN32 ifdefs, use clock_gettime(CLOCK_MONOTONIC) (GREEN)
- [ ] Commit

## Phase 2: Remove CONFIG_WIN32 throughout

- [ ] unix.c: Remove winsock/timeb includes, fdesc_t typedef → just int
- [ ] unix.c: Remove __TINYC__ stat wrapper
- [ ] unix.c: Remove #ifndef CONFIG_WIN32 guard around waitpid block (keep the code)
- [ ] qe.c: Remove path_win_to_unix() and its call sites
- [ ] qe.c: Remove CONFIG_WIN32 main1/main split
- [ ] qe.c: Remove CONFIG_WIN32 qe_event_init stub
- [ ] qe.h: Remove CONFIG_WIN32 is_user_input_pending stub (keep Unix version)
- [ ] cutils.c: Remove CONFIG_WIN32 backslash path handling
- [ ] buffer.c: Remove #ifndef CONFIG_WIN32 around chmod()
- [ ] util.c: Remove CONFIG_WIN32 fnmatch() implementation
- [ ] util.h: Remove CONFIG_WIN32 snprintf/vsnprintf aliases
- [ ] tqe.c: Remove CONFIG_WIN32 branch (always include unix.c + tty.c)
- [ ] extras.c: Remove CONFIG_CYGWIN guard around environ declaration
- [ ] Commit

## Phase 3: Remove CONFIG_DARWIN platform-specific code

- [ ] qe.c: Remove CONFIG_DARWIN home dir paths — use getenv("HOME") or /home/
- [ ] tty.c: Remove CONFIG_DARWIN fwrite handling (use fwrite_unlocked unconditionally via cosmo)
- [ ] tty.c: Remove CONFIG_DARWIN clipboard (pbcopy/pbpaste) — keep OSC 52 only
- [ ] modes/shell.c: Remove CONFIG_DARWIN setsid() ordering
- [ ] modes/archive.c: Remove CONFIG_DARWIN dylib entry
- [ ] Commit

## Phase 4: Remove CONFIG_DLL (no dynamic loading in APE)

- [ ] qe.c: Remove qe_load_all_modules() and CONFIG_DLL includes
- [ ] qe.h: Already undefs in tiny — remove entirely
- [ ] Commit

## Phase 5: Remove CONFIG_UNLOCKIO — always use unlocked I/O

- [ ] tty.c: Replace TTY_PUTC/TTY_FWRITE macros — always use unlocked variants
- [ ] Commit

## Phase 6: Clean up session.c platform PTY headers

- [ ] session.c: Replace linux/cygwin/freebsd/openbsd/apple pty.h maze with cosmo's
- [ ] Commit

## Phase 7: Add cosmopolitan features

- [ ] Write test for cosmo feature detection (__COSMOPOLITAN__ defined) (RED/GREEN)
- [ ] Add pledge()/unveil() after init in qe.c
- [ ] Add CheckForMemoryLeaks/CheckForFileLeaks in debug
- [ ] Use GetProgramExecutableName() where useful
- [ ] Use -mcosmo flag in Makefile.cosmo
- [ ] Commit

## Phase 8: Clean up configure / build system

- [ ] Strip platform detection from configure (or simplify for cosmo-only)
- [ ] Remove dead files: win32.c, x11.c, haiku.cpp, cfb.c/h, fbfrender.c/h, etc.
- [ ] Update Makefile to remove platform conditionals
- [ ] Commit

## Phase 9: Final verification

- [ ] make test passes
- [ ] make -f Makefile.cosmo builds successfully
- [ ] All CONFIG_WIN32/DARWIN/CYGWIN/HAIKU references gone
- [ ] Commit final state
