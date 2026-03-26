# QEmacs Codebase Cleanup Plan

Status: **In Progress** (5 of 8 items complete)

## Overview

Analysis of the ~119K LOC codebase identified 984 TODO/FIXME/XXX comments and
several systemic patterns worth addressing. This document tracks cleanup work
organized by priority.

---

## 1. Extract Shared Colorize Helpers
**Priority:** High | **Effort:** Medium | **Status:** Done

The 57 language modules in `lang/` duplicated identical parsing logic.

### Completed
- Added three inline helpers in `util.h`:
  - `colorize_skip_escape(str, i, n)` — advance past backslash escape
  - `colorize_skip_block_comment(str, i, n, statep, bit)` — scan for `*/` end
  - `colorize_parse_number(str, i, n, allow_sep)` — parse number literals
    with 0x/0o/0b radix prefixes, decimal point, and exponent
- Applied escape helpers to 25 modules: scad, rust, nim, falcon, elm, icon,
  swift, jai, virgil, coffee, julia, groovy, crystal, magpie, python, ruby,
  sql, clang (8 instances)
- Applied number parsing to: rust, elm, jai, haskell
- Applied block comment helper to: scad, rust, falcon, crystal, groovy,
  virgil, ruby, sql
- Net result: -139 lines

### Remaining opportunity
- Several lang/ files have `parse_decimal:` goto labels preventing clean
  replacement (ruby, crystal, python, falcon, coffee)
- Nested comment variants (magpie, swift, tiger) need custom logic

---

## 2. Add Unit Tests for buffer.c and search.c
**Priority:** High | **Effort:** Medium | **Status:** Partial (buffer done)

### Completed
- Added `tests/test_buffer.c` with 22 tests covering:
  - Insert (beginning, middle, end, large buffers)
  - Delete (beginning, middle, all, in large buffers)
  - Read (partial, past-end, single byte)
  - Line navigation (goto_bol, goto_eol, next_line, prev_line)
  - Character operations (insert_char32, nextc, prevc, insert_str)
  - Replace (same size, different size)
  - UTF-8 insert/read, clear, get_line_length
- Updated `tests/Makefile` with compilation rule

### Remaining
- `search.c` tests (search_string, replace patterns)
- `charset.c` tests (encoding round-trips)
- `color.c` tests (color parsing, matching)

---

## 3. Fix Unchecked malloc and strcpy Consistency
**Priority:** Medium | **Effort:** Low | **Status:** Done

### Completed
- Added null check after `qe_malloc_array` in `do_define_kbd_macro`
- Replaced 3 `strcpy` calls with bounded `pstrcpy` for consistency
- Added `put_status` error message for malloc failure in `do_quoted_insert`

---

## 4. Module Init Macro
**Priority:** Medium | **Effort:** Low | **Status:** Done

### Completed
- Defined `qe_module_init_mode(mode, flags)` macro in `qe.h`
- Applied to 45 lang/ files and 3 modes/ files (48 total)
- Updated Makefile module generation to handle the new macro
- Net result: -265 lines of boilerplate removed

### Files kept with custom init (multi-mode or extra setup)
- clang.c (61 modes + commands), lisp.c (8 modes), script.c (6 modes),
  python.c (3 modes), sharp.c (3 modes), lua.c, makemode.c, ebnf.c,
  jai.c, ocaml.c, arm.c

---

## 5. Split qe.c into Logical Modules
**Priority:** Medium | **Effort:** High | **Status:** Not Started

`qe.c` is ~12K LOC with 182 static globals and 178 TODO/FIXME comments.

### Plan
- Identify natural boundaries (window management, file I/O, command dispatch)
- Extract in phases to avoid breakage

---

## 6. Split clang.c
**Priority:** Low | **Effort:** Medium | **Status:** Not Started

`lang/clang.c` is 4,812 LOC defining 53+ language modes.

### Plan
- Split into `clang-c.c` (C/C++/ObjC core), `clang-js.c` (JS/TS), `clang-misc.c`

---

## 7. Remove #if 0 Dead Code Blocks
**Priority:** Low | **Effort:** Low | **Status:** Done

### Completed
Removed 19 dead code blocks (~250 lines) from core files:
- **qe.c**: Debug printf blocks (4), async callbacks (2), unused functions
  (`do_space`, `qe_find_file_window`, `detect_binary`), broken buffer data
  cleanup, unused color completion code
- **buffer.c**: Async I/O infrastructure (`BufferIOState`, `load_buffer`,
  callbacks), unused `eb_line_pad`
- **cutils.c**: Exploratory `utf8_min_code` constant array
- **display.c**: Bitmap cache stubs (`QECachedBitmap` and functions)
- **qe.h**: Unused struct fields (`io_state`, `probed`, `font`, `style_cache`)

### Blocks intentionally kept
- `#if 0`/`#else` blocks with active alternatives (color.c algorithm choices,
  charset.c detection fallbacks, display.c interpolation) — these document
  design decisions

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

---

## Summary of Changes

| Item | Lines Removed | Lines Added | Net |
|------|--------------|-------------|-----|
| 1. Colorize helpers | 280 | 141 | -139 |
| 2. Buffer tests | 0 | 425 | +425 |
| 3. Safety fixes | 4 | 8 | +4 |
| 4. Module init macro | 333 | 68 | -265 |
| 7. Dead code removal | 318 | 4 | -314 |
| **Total** | **935** | **646** | **-289** |

## Changelog

- **2026-03-26**: Initial analysis and plan created
- **2026-03-26**: Completed items 1, 3, 4, 7. Removed 644 net lines.
- **2026-03-26**: Added 22 buffer unit tests. Expanded colorize helpers to
  clang.c (8 escape patterns), jai.c and haskell.c (number parsing).
