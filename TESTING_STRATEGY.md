# QEmacs Testing Strategy Analysis

## Current State

QEmacs has **zero automated unit tests**. The `tests/` directory contains only
visual/manual test scripts (terminal color tests, Unicode rendering, bidi text).
The `make test` target delegates to `tests/Makefile` which does not exist.

## Recommended Test Framework

Use a **minimal, self-contained test harness** inspired by cosmopolitan libc's
`testlib`. No external dependencies -- just a single header file (`testlib.h`)
that provides:

- `TEST(suite, name)` macro to define test functions
- `ASSERT_EQ`, `ASSERT_NE`, `ASSERT_TRUE`, `ASSERT_FALSE` for integers
- `ASSERT_STREQ`, `ASSERT_STRNE` for strings
- `ASSERT_MEMEQ` for memory blocks
- Automatic test discovery via `__attribute__((constructor))`
- Colored pass/fail output
- Return code 0 on success, 1 on any failure

Each test file is a standalone executable that links only the module under test.
This keeps compile times fast and avoids pulling in editor state.

## Highest Impact Tests (Priority Order)

### Tier 1: String/Path Utilities (`cutils.c`) -- HIGHEST ROI

These are the foundation of the entire editor. Every buffer operation, file
operation, and mode relies on them. They are pure functions with no global state.

| Function | Why test it | Edge cases |
|---|---|---|
| `pstrcpy` | Used everywhere for safe string copy | `size=0`, `size=1`, truncation, empty src |
| `pstrcat` | Safe concatenation | overflow, empty buf, empty src |
| `pstrncpy` / `pstrncat` | Length-bounded variants | `len=0`, `len > size`, embedded NUL |
| `strstart` | Prefix matching (mode detection, config parsing) | empty prefix, no match, NULL ptr arg |
| `strend` | Suffix matching (extension detection) | empty suffix, full match, no match |
| `get_basename_offset` | Filename extraction from paths | trailing slash, root path, no slash, NULL |
| `get_extension_offset` | Extension detection (mode dispatch) | no extension, dotfiles, multiple dots |
| `get_dirname` | Directory extraction | root `/`, relative path, trailing slash |
| `get_relativename` | Relative path computation | non-relative path, exact dirname match |

**Impact**: A bug in `pstrcpy` or `get_extension_offset` could cause crashes,
buffer overflows, or wrong syntax mode selection across the entire editor.

### Tier 2: UTF-8 Encoding/Decoding (`cutils.c`)

The UTF-8 codec is called on every keystroke and every display refresh.

| Function | Why test it | Edge cases |
|---|---|---|
| `unicode_to_utf8` | Encodes codepoints for display/storage | ASCII, 2/3/4/5/6-byte sequences, U+0000, max codepoint, invalid (>=0x80000000) |
| `unicode_from_utf8` | Decodes bytes from files/input | valid sequences at all lengths, overlong encodings (0xC0 0x80), truncated sequences, invalid continuation bytes, `max_len` boundaries |

**Impact**: Incorrect UTF-8 handling corrupts files and causes display glitches
for every non-ASCII user.

### Tier 3: Dynamic Buffers (`cutils.c`)

DynBuf is the core allocation abstraction used by regex, script engine, and more.

| Function | Why test it | Edge cases |
|---|---|---|
| `dbuf_init` / `dbuf_init2` | Initialization | custom allocator |
| `dbuf_put` / `dbuf_putc` / `dbuf_putstr` | Append operations | empty, single byte, reallocation trigger |
| `dbuf_write` | Random-access write | write past end, write at offset 0 |
| `dbuf_put_self` | Self-referential copy | overlap edge cases |
| `dbuf_printf` | Formatted append | short (< 128 byte) and long (> 128 byte) format strings |
| `dbuf_str` | NUL-terminated access | empty buffer, after error |
| `dbuf_free` | Cleanup | double free safety, NULL buf |

**Impact**: Memory corruption from DynBuf bugs would be extremely hard to debug
and could manifest anywhere.

### Tier 4: Path Canonicalization (`util.c`)

| Function | Why test it | Edge cases |
|---|---|---|
| `canonicalize_path` | Path normalization | `.` removal, `..` traversal, double slashes, protocol prefixes (`http://`), drive specs, overlapping src/dest |
| `makepath` | Path construction | absolute child, empty components |
| `splitpath` | Path decomposition | root path, no extension |
| `match_extension` | Extension matching for mode dispatch | case sensitivity, multiple extensions (`"c\|h\|cpp"`) |

**Impact**: Wrong path canonicalization = opening wrong files, broken relative
includes, incorrect mode detection.

### Tier 5: Color Parsing (`color.c`)

| Function | Why test it | Edge cases |
|---|---|---|
| `css_get_color` | Parses `#RGB`, `#RRGGBB`, `rgb()`, named colors | hex case, short hex, rgb percentages, unknown names |
| `color_dist` | Color distance for palette mapping | identical colors, black/white extremes |
| `qe_map_color` | Map 24-bit color to terminal palette | exact palette match, nearest neighbor |

**Impact**: Broken color parsing = garbled syntax highlighting themes.

### Tier 6: Regular Expressions (`libregexp.c`)

| Area | Why test it | Cases |
|---|---|---|
| Character classes | Foundation of regex matching | `\d`, `\w`, `\s`, negated classes, Unicode properties |
| Quantifiers | Greedy/lazy matching | `*`, `+`, `?`, `{n,m}`, nested |
| Anchors | Position matching | `^`, `$`, multiline |
| Capture groups | Used by search-replace | numbered groups, nested groups |
| Case folding | Case-insensitive search | ASCII and Unicode case folding |

**Impact**: Regex bugs affect every search and search-replace operation.

### Lower Priority (harder to isolate)

- **Buffer operations** (`buffer.c`): `eb_insert`, `eb_delete`, `eb_read` --
  high impact but require `EditBuffer` initialization with page management
- **Charset detection** (`charset.c`): important but requires I/O fixtures
- **Unicode normalization** (`libunicode.c`): important for correctness but
  complex setup

## Proposed File Structure

```
tests/
  testlib.h          # minimal test framework header
  test_cutils.c      # Tier 1+2+3: string, UTF-8, DynBuf
  test_path.c        # Tier 4: path operations
  test_color.c       # Tier 5: color parsing
  test_regexp.c      # Tier 6: regex
  Makefile           # build and run all test executables
```

## Build Integration

Each test is a standalone binary:

```makefile
# tests/Makefile
test: test_cutils test_path test_color test_regexp
	./test_cutils
	./test_path
	./test_color
	./test_regexp

test_cutils: test_cutils.c ../cutils.c testlib.h
	$(CC) $(CFLAGS) -I.. -o $@ test_cutils.c ../cutils.c

test_path: test_path.c ../util.c ../cutils.c testlib.h
	$(CC) $(CFLAGS) -I.. -o $@ test_path.c ../util.c ../cutils.c
```

This integrates with the existing `make test` target which already delegates
to `tests/Makefile`.

## Proof of Concept

See `tests/testlib.h` and `tests/test_cutils.c` for a working implementation
of Tier 1+2+3 tests.
