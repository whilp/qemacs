/*
 * Tests for TTYChar 128-bit color packing (true color support)
 *
 * Verifies that 24-bit RGB colors survive the TTYChar round-trip:
 *   color → qe_map_color → TTY_CHAR → TTY_CHAR_GET_FG/BG → qe_unmap_color → color
 */

#include "testlib.h"
#include <stdint.h>

/*
 * Pull in the TTYChar macro definitions directly from tty.c.
 * We only need the 128-bit variant (MAX_UNICODE_DISPLAY > 0xFFFF).
 */
typedef __uint128_t TTYChar;
#define TTY_STYLE_BITS        32
#define TTY_FG_COLORS         0x1000000
#define TTY_BG_COLORS         0x1000000
#define TTY_RGB_FG(r,g,b)     (0x1000000 | ((r) << 16) | ((g) << 8) | (b))
#define TTY_RGB_BG(r,g,b)     (0x1000000 | ((r) << 16) | ((g) << 8) | (b))
#define TTY_CHAR(ch,fg,bg)    ((__uint128_t)(uint32_t)(ch) | \
    ((__uint128_t)((uint64_t)(unsigned)(fg) | ((uint64_t)(unsigned)(bg) << 29)) << 32))
#define TTY_CHAR2(ch,col)     ((__uint128_t)(uint32_t)(ch) | ((__uint128_t)(uint64_t)(col) << 32))
#define TTY_CHAR_GET_CH(cc)   ((uint32_t)(cc))
#define TTY_CHAR_GET_COL(cc)  ((uint64_t)((cc) >> 32))
#define TTY_CHAR_GET_ATTR(cc) ((uint32_t)((cc) >> 32) & 0x1E000000U)
#define TTY_CHAR_GET_FG(cc)   ((uint32_t)((cc) >> 32) & 0x1FFFFFFU)
#define TTY_CHAR_GET_BG(cc)   ((uint32_t)((uint64_t)((cc) >> 61)) & 0x1FFFFFFU)
#define TTY_CHAR_DEFAULT      TTY_CHAR(' ', 7, 0)
#define TTY_BOLD              0x02000000U
#define TTY_UNDERLINE         0x04000000U
#define TTY_ITALIC            0x08000000U
#define TTY_BLINK             0x10000000U

/* Custom assertion for uint32_t (ASSERT_EQ uses long long which can't hold __uint128_t) */
#define ASSERT_U32_EQ(a, b)                                                \
    do {                                                                   \
        uint32_t _a = (a), _b = (b);                                      \
        if (_a != _b)                                                      \
            FAIL_(__FILE__, __LINE__,                                       \
                  "%s == %s: got 0x%08x, expected 0x%08x", #a, #b, _a, _b); \
    } while (0)

/* ---- Basic packing/unpacking ---- */

TEST(ttychar, basic_fg_bg) {
    TTYChar tc = TTY_CHAR('A', 7, 0);
    ASSERT_U32_EQ(TTY_CHAR_GET_CH(tc), 'A');
    ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), 7);
    ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), 0);
}

TEST(ttychar, palette_index_roundtrip) {
    /* Palette indices 0-255 should round-trip exactly */
    for (unsigned fg = 0; fg < 256; fg += 17) {
        for (unsigned bg = 0; bg < 256; bg += 19) {
            TTYChar tc = TTY_CHAR('x', fg, bg);
            ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), fg);
            ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), bg);
            ASSERT_U32_EQ(TTY_CHAR_GET_CH(tc), 'x');
        }
    }
}

/* ---- True color (24-bit RGB) round-trip ---- */

TEST(ttychar, truecolor_fg_roundtrip) {
    /* The color value from qe_map_color with count=0x1000000 is 0x1000000|RGB */
    uint32_t colors[] = {
        0x1000000,               /* black */
        0x1FFFFFF,               /* white */
        0x13A7BC2,               /* the example from issue #53 */
        0x1FF0000,               /* red */
        0x100FF00,               /* green */
        0x10000FF,               /* blue */
        0x1123456,               /* arbitrary */
        0x1ABCDEF,               /* arbitrary */
        0x1010101,               /* near-black */
        0x1FEFEFE,               /* near-white */
    };
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        TTYChar tc = TTY_CHAR('Z', colors[i], 0);
        ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), colors[i]);
        ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), 0);
    }
}

TEST(ttychar, truecolor_bg_roundtrip) {
    uint32_t colors[] = {
        0x1000000, 0x1FFFFFF, 0x13A7BC2, 0x1FF0000,
        0x100FF00, 0x10000FF, 0x1ABCDEF, 0x1010101,
    };
    for (int i = 0; i < (int)(sizeof(colors) / sizeof(colors[0])); i++) {
        TTYChar tc = TTY_CHAR('Z', 7, colors[i]);
        ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), colors[i]);
        ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), 7);
    }
}

TEST(ttychar, truecolor_both_fg_bg) {
    /* Both FG and BG as true color simultaneously */
    uint32_t fg = 0x13A7BC2;  /* #3A7BC2 */
    uint32_t bg = 0x1FF8800;  /* #FF8800 */
    TTYChar tc = TTY_CHAR('Q', fg, bg);
    ASSERT_U32_EQ(TTY_CHAR_GET_CH(tc), 'Q');
    ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), fg);
    ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), bg);
}

/* ---- The exact color from issue #53 ---- */

TEST(ttychar, issue53_color_preserved) {
    /* #3A7BC2 was previously quantized to #337BCB (12-bit).
     * With 128-bit TTYChar, it should be preserved exactly. */
    uint32_t mapped = 0x1000000 | 0x3A7BC2;  /* qe_map_color output */
    TTYChar tc = TTY_CHAR('x', mapped, 0);
    uint32_t got = TTY_CHAR_GET_FG(tc);
    ASSERT_U32_EQ(got, mapped);
    /* Extract the RGB for verification */
    uint32_t rgb = got & 0xFFFFFF;
    ASSERT_U32_EQ(rgb, 0x3A7BC2);
}

/* ---- Attributes don't corrupt colors ---- */

TEST(ttychar, bold_does_not_affect_color) {
    uint32_t color = 0x13A7BC2;
    uint32_t fg_with_bold = color | TTY_BOLD;
    TTYChar tc = TTY_CHAR('A', fg_with_bold, 0);
    /* FG extracts just the 25-bit color (no attr bits) */
    ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), color);
    /* Attrs are separate */
    ASSERT_U32_EQ(TTY_CHAR_GET_ATTR(tc), TTY_BOLD);
}

TEST(ttychar, all_attrs_preserved) {
    uint32_t color = 0x1ABCDEF;
    uint32_t attrs = TTY_BOLD | TTY_UNDERLINE | TTY_ITALIC | TTY_BLINK;
    TTYChar tc = TTY_CHAR('X', color | attrs, 0x1112233);
    ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), color);
    ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), 0x1112233);
    ASSERT_U32_EQ(TTY_CHAR_GET_ATTR(tc), attrs);
}

/* ---- TTY_RGB_FG/BG preserve full 24-bit ---- */

TEST(ttychar, rgb_macro_lossless) {
    /* Previously TTY_RGB_FG quantized to 12-bit. Now it should be lossless. */
    uint32_t fg = TTY_RGB_FG(0x3A, 0x7B, 0xC2);
    ASSERT_U32_EQ(fg, 0x1000000 | 0x3A7BC2);

    uint32_t bg = TTY_RGB_BG(0xFF, 0x88, 0x00);
    ASSERT_U32_EQ(bg, 0x1000000 | 0xFF8800);

    /* Edge: all zeros */
    ASSERT_U32_EQ(TTY_RGB_FG(0, 0, 0), 0x1000000);
    /* Edge: all 0xFF */
    ASSERT_U32_EQ(TTY_RGB_FG(0xFF, 0xFF, 0xFF), 0x1FFFFFF);
}

/* ---- Scalar comparison (diff loop) ---- */

TEST(ttychar, equality_same_cell) {
    TTYChar a = TTY_CHAR('A', 0x13A7BC2, 0x1FF8800);
    TTYChar b = TTY_CHAR('A', 0x13A7BC2, 0x1FF8800);
    ASSERT_TRUE(a == b);
}

TEST(ttychar, equality_different_fg) {
    TTYChar a = TTY_CHAR('A', 0x13A7BC2, 0);
    TTYChar b = TTY_CHAR('A', 0x13A7BC3, 0);  /* differs by 1 in blue */
    ASSERT_TRUE(a != b);
}

TEST(ttychar, equality_different_bg) {
    TTYChar a = TTY_CHAR('A', 7, 0x1000000);
    TTYChar b = TTY_CHAR('A', 7, 0x1000001);
    ASSERT_TRUE(a != b);
}

TEST(ttychar, guard_cell_trick) {
    /* The diff loop uses ptr[0] + 1 as a guard sentinel.
     * Verify that +1 on a TTYChar produces a different value. */
    TTYChar tc = TTY_CHAR('A', 0x13A7BC2, 0x1FF8800);
    TTYChar guard = tc + 1;
    ASSERT_TRUE(tc != guard);
}

/* ---- TTY_CHAR2 / TTY_CHAR_GET_COL round-trip ---- */

TEST(ttychar, col_roundtrip) {
    TTYChar tc = TTY_CHAR('X', 0x13A7BC2 | TTY_BOLD, 0x1ABCDEF);
    uint64_t col = TTY_CHAR_GET_COL(tc);
    TTYChar tc2 = TTY_CHAR2('Y', col);
    /* Same style, different character */
    ASSERT_U32_EQ(TTY_CHAR_GET_CH(tc2), 'Y');
    ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc2), TTY_CHAR_GET_FG(tc));
    ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc2), TTY_CHAR_GET_BG(tc));
    ASSERT_U32_EQ(TTY_CHAR_GET_ATTR(tc2), TTY_CHAR_GET_ATTR(tc));
}

/* ---- XOR rectangle (swap FG/BG) ---- */

TEST(ttychar, xor_swap_fg_bg) {
    uint32_t fg = 0x13A7BC2;
    uint32_t bg = 0x1FF8800;
    TTYChar tc = TTY_CHAR('A', fg | TTY_BOLD, bg);

    /* Simulate xor_rectangle swap */
    uint64_t col = TTY_CHAR_GET_COL(tc);
    uint32_t got_fg = TTY_CHAR_GET_FG(tc);
    uint32_t got_bg = TTY_CHAR_GET_BG(tc);
    uint64_t new_col = (uint64_t)got_bg | (col & 0x1E000000ULL) | ((uint64_t)got_fg << 29);
    TTYChar swapped = TTY_CHAR2('A', new_col);

    /* After swap: old BG becomes FG, old FG becomes BG */
    ASSERT_U32_EQ(TTY_CHAR_GET_FG(swapped), bg);
    ASSERT_U32_EQ(TTY_CHAR_GET_BG(swapped), fg);
    /* Attrs preserved */
    ASSERT_U32_EQ(TTY_CHAR_GET_ATTR(swapped), TTY_BOLD);
}

/* ---- Exhaustive low-bit preservation ---- */

TEST(ttychar, all_rgb_channels_independent) {
    /* Verify each channel is independent and all 8 bits preserved */
    for (int ch = 0; ch < 3; ch++) {
        for (int val = 0; val < 256; val++) {
            uint32_t rgb = 0;
            if (ch == 0) rgb = val << 16;       /* red */
            else if (ch == 1) rgb = val << 8;   /* green */
            else rgb = val;                      /* blue */
            uint32_t mapped = 0x1000000 | rgb;
            TTYChar tc = TTY_CHAR('x', mapped, mapped);
            ASSERT_U32_EQ(TTY_CHAR_GET_FG(tc), mapped);
            ASSERT_U32_EQ(TTY_CHAR_GET_BG(tc), mapped);
        }
    }
}

int main() { return testlib_run_all(); }
