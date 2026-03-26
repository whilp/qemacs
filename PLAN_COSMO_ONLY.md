# Plan: Cosmopolitan-Only Build — COMPLETED

Drop all non-cosmopolitan platform support. Use cosmo features to simplify code.

## Phase 1: Clock — Replace platform-specific timing with CLOCK_MONOTONIC

- [x] Write test for get_clock_ms / get_clock_usec (RED)
- [x] Remove CONFIG_WIN32 ifdefs, use clock_gettime(CLOCK_MONOTONIC) (GREEN)
- [x] Commit

## Phase 2: Remove CONFIG_WIN32 throughout

- [x] unix.c: Remove winsock/timeb includes, fdesc_t typedef
- [x] unix.c: Remove __TINYC__ stat wrapper
- [x] unix.c: Remove #ifndef CONFIG_WIN32 guard around waitpid block
- [x] qe.c: Remove path_win_to_unix() and its call sites
- [x] qe.c: Remove CONFIG_WIN32 main1/main split
- [x] qe.c: Remove CONFIG_WIN32 qe_event_init stub
- [x] qe.h: Remove CONFIG_WIN32 is_user_input_pending stub
- [x] cutils.c: Remove CONFIG_WIN32 backslash path handling
- [x] buffer.c: Remove #ifndef CONFIG_WIN32 around chmod()
- [x] util.c: Remove CONFIG_WIN32 fnmatch() implementation
- [x] util.h: Remove CONFIG_WIN32 snprintf/vsnprintf aliases
- [x] tqe.c: Remove CONFIG_WIN32 branch
- [x] extras.c: Remove CONFIG_CYGWIN guard around environ declaration
- [x] Commit

## Phase 3: Remove CONFIG_DARWIN platform-specific code

- [x] qe.c: Replace hardcoded home paths with getpwnam()
- [x] tty.c: Remove CONFIG_DARWIN clipboard (pbcopy/pbpaste)
- [x] modes/shell.c: Remove CONFIG_DARWIN setsid() ordering
- [x] modes/archive.c: Unconditionally include dylib entry
- [x] Commit

## Phase 4: Remove CONFIG_DLL (no dynamic loading in APE)

- [x] qe.c: Remove qe_load_all_modules() and CONFIG_DLL includes
- [x] qe.h: Remove CONFIG_DLL undef
- [x] Commit

## Phase 5: Remove CONFIG_UNLOCKIO — always use unlocked I/O

- [x] tty.c: Replace TTY_PUTC/TTY_FWRITE macros — always use unlocked variants
- [x] Commit

## Phase 6: Clean up session.c platform PTY headers

- [x] session.c: Replace platform-specific pty.h maze with single #include <pty.h>
- [x] Commit

## Phase 7: Add cosmopolitan features

- [x] Write test for cosmo feature detection (test_cosmo.c)
- [x] Add pledge() after init in qe.c
- [x] Declare CheckForMemoryLeaks/CheckForFileLeaks
- [x] Use -mcosmo flag in Makefile.cosmo
- [x] Commit

## Phase 8: Clean up configure / build system

- [x] Strip dead platform detection from configure
- [x] Remove dead files: win32.c, x11.c, haiku.cpp, cfb.c/h, fbfrender.c/h, etc.
- [x] Update Makefile to remove platform conditionals
- [x] Fix recursive make to use -f Makefile
- [x] Commit

## Phase 9: Final verification

- [x] make test passes (121/121 tests)
- [x] make -f Makefile builds qe + tqe successfully
- [x] All CONFIG_WIN32/DARWIN/CYGWIN/HAIKU/DLL/UNLOCKIO references gone
- [x] Commit final state

## Summary

- **~8,700 lines deleted** across all changes
- **12 dead files removed** (win32.c, x11.c, haiku.cpp, cfb.c/h, fbfrender.c/h, libfbf.c/h, html2png.c, modes/video.c, modes/image.c)
- **8 new tests added** (test_unix.c, test_cosmo.c)
- **Security hardened** with pledge() syscall restriction
- **Monotonic clock** replaces gettimeofday for robust timing
- **getpwnam()** replaces hardcoded home directory paths
- **Unlocked I/O** used unconditionally for performance
