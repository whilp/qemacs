# QEmacs Codebase Cleanup Plan

Status: **In Progress**

## Overview

Analysis of the ~119K LOC codebase identified 984 TODO/FIXME/XXX comments and
several systemic patterns worth addressing. This document tracks cleanup work
organized by priority.

---

## 1. Extract Shared Colorize Helpers
**Priority:** High | **Effort:** Medium | **Status:** Not Started

The 57 language modules in `lang/` duplicate identical parsing logic:

- **String escape handling** — `if (c == '\\') { if (i < n) i++; }` duplicated 57 times
- **Comment parsing** — C-style `/* */`, line comments `#`, `//` duplicated 45+ times
- **Number literal parsing** — hex/octal/binary/decimal/exponent duplicated 15+ times

### Plan
- Create `lang/lang-utils.h` with inline helpers:
  - `colorize_skip_escape(str, i, n)` — advance past backslash escape
  - `colorize_skip_string(str, i, n, sep)` — skip to end of quoted string
  - `colorize_match_c_comment_start(str, i, n)` — detect `/*`
  - `colorize_skip_to_c_comment_end(str, i, n)` — skip to `*/`
- Migrate a few representative modules first (lua, python, ruby), then sweep remaining

---

## 2. Add Unit Tests for buffer.c and search.c
**Priority:** High | **Effort:** Medium | **Status:** Not Started

Only 4 test files exist for ~100K LOC. Core modules have zero coverage:
- `buffer.c` (2,918 LOC) — buffer insert/delete/encoding
- `search.c` (1,684 LOC) — search/replace
- `charset.c` (1,622 LOC) — character encoding
- `color.c` (1,556 LOC) — color management

### Plan
- Add `tests/test_buffer.c` — test eb_insert, eb_delete, eb_read, encoding round-trips
- Add `tests/test_search.c` — test search_string, replace patterns
- Update `tests/Makefile` with new test targets

---

## 3. Fix Unchecked malloc and strcpy Consistency
**Priority:** Medium | **Effort:** Low | **Status:** Not Started

### Specific issues
- `qe.c:6409` — `qe_malloc_array(char, size)` used without null check
- `qe.c:6840` — `strcpy(c->buf, "Describe key: ")` should use `pstrcpy`
- `qe.c:8755` — `strcpy(cwd, ".")` should use `pstrcpy`
- `qe.c:11065` — `strcpy(path, ".")` should use `pstrcpy`
- `qe.c:2056` — malloc failure silently returns (FIXME comment)

### Plan
- Add null check after malloc at line 6409
- Replace 3 strcpy calls with pstrcpy
- Add `put_status` error message for malloc failure at line 2056

---

## 4. Module Init Macro
**Priority:** Medium | **Effort:** Low | **Status:** Not Started

55+ files repeat identical 5-line init boilerplate:
```c
static int lang_init(QEmacsState *qs) {
    qe_register_mode(qs, &lang_mode, MODEF_SYNTAX);
    return 0;
}
qe_module_init(lang_init);
```

### Plan
- Define `qe_module_init_mode(mode, flags)` macro in `qe.h`
- Replace boilerplate in all single-mode lang/ files
- Keep custom init functions for multi-mode files (clang.c, lisp.c, script.c)

---

## 5. Split qe.c into Logical Modules
**Priority:** Medium | **Effort:** High | **Status:** Not Started

`qe.c` is 12,282 LOC with 182 static globals and 178 TODO/FIXME comments.

### Plan
- Identify natural boundaries (window management, file I/O, command dispatch, key handling)
- Extract in phases to avoid breakage
- Deferred to after items 1-4 are complete

---

## 6. Split clang.c
**Priority:** Low | **Effort:** Medium | **Status:** Not Started

`lang/clang.c` is 4,812 LOC defining 53 language modes.

### Plan
- Split into `clang-c.c` (C/C++/ObjC core), `clang-js.c` (JS/TS), `clang-misc.c`
- Deferred to after items 1-4 are complete

---

## 7. Remove #if 0 Dead Code Blocks
**Priority:** Low | **Effort:** Low | **Status:** Not Started

13+ `#if 0` blocks across core files. These are dead code that obscures the
actual logic.

### Plan
- Audit each block: remove if obsolete, convert to proper `#ifdef` if conditional
- Files: qe.c (13 blocks), buffer.c (2), cutils.c (1), charset.c (2), color.c (4), display.c (2)

---

## 8. Group Globals into Structs
**Priority:** Low | **Effort:** High | **Status:** Not Started

~850 static globals across the codebase. Worst offenders:
- `qe.c`: 182 static globals
- `charsetmore.c`: 75
- `extras.c`: 68
- `tty.c`: 51

### Plan
- Group related globals into structs (e.g., `TtyState`, `SearchState`)
- Deferred to after items 1-6 are complete

---

## Changelog

- **2026-03-26**: Initial analysis and plan created
