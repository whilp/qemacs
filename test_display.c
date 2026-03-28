/*
 * Headless test display driver for QEmacs
 *
 * This display driver runs without a real terminal. It maintains
 * an in-memory character grid (with colors and attributes) and
 * reads keystrokes from a pipe or file descriptor.
 *
 * Activation: set QE_TEST_DISPLAY=1 in the environment.
 * The driver's dpy_probe returns 100, beating the TTY driver (1).
 *
 * Input: reads raw key events from the file descriptor specified
 * by QE_TEST_INPUT_FD (default: stdin if not a tty).
 *
 * Screen dumps: on each flush, if QE_TEST_DUMP_FILE is set, the
 * screen is appended to that file with color annotations.
 *
 * Copyright (c) 2026 Contributors.
 * MIT License (same as QEmacs).
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "qe.h"

/*--- Screen cell encoding ---*/

/* Each cell stores a Unicode codepoint, fg color index, bg color index,
   and attribute flags. We use a simple struct for clarity. */
typedef struct TestCell {
    char32_t ch;
    uint16_t fg;       /* foreground color index */
    uint16_t bg;       /* background color index */
    uint16_t attrs;    /* TESTATTR_xxx flags */
} TestCell;

#define TESTATTR_BOLD      0x01
#define TESTATTR_UNDERLINE 0x02
#define TESTATTR_ITALIC    0x04
#define TESTATTR_BLINK     0x08

typedef struct TestDisplayState {
    TestCell *screen;          /* width * height cells */
    int screen_size;           /* width * height */
    int cursor_x, cursor_y;
    int input_fd;              /* fd to read key events from */
    int dump_fd;               /* fd to write screen dumps to (-1 = none) */
    int flush_count;           /* number of flushes so far */
    FILE *dump_file;           /* file for screen dumps */
    char dump_path[256];       /* path to dump file (for reopen in "last" mode) */
    int dump_mode_last;        /* 1 = rewrite file each flush */
    char resize_path[256];     /* path to file containing "WxH" for resize */
} TestDisplayState;

static QEditScreen *test_screen;  /* for signal handler */

/*--- Forward declarations ---*/
static void test_dpy_invalidate(QEditScreen *s);

/*--- SIGWINCH handler for mid-session resize ---*/
static void test_term_resize(qe__unused__ int sig)
{
    QEditScreen *s = test_screen;
    if (s) {
        test_dpy_invalidate(s);
        url_redisplay();
    }
}

/*--- Probe: activate when QE_TEST_DISPLAY=1 ---*/

static int test_dpy_probe(void)
{
    const char *p = getenv("QE_TEST_DISPLAY");
    if (p && *p == '1')
        return 100;  /* beat TTY driver (probe score 1) */
    return 0;
}

/*--- Input handling ---*/

/* Try to parse an SGR mouse sequence starting at buf[start].
 * SGR format: ESC [ < button ; x ; y M  (press)
 *             ESC [ < button ; x ; y m  (release)
 * Returns number of bytes consumed, or 0 if not a complete SGR sequence. */
static int test_parse_sgr_mouse(const unsigned char *buf, int len, int start,
                                QEEvent *ev)
{
    int pos = start;
    int button, x, y;
    char terminator;

    /* Need at least ESC [ < N ; N ; N M = 10 bytes minimum */
    if (pos + 9 > len) return 0;
    if (buf[pos] != 0x1b) return 0;
    if (buf[pos + 1] != '[') return 0;
    if (buf[pos + 2] != '<') return 0;
    pos += 3;

    /* Parse button number */
    button = 0;
    while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
        button = button * 10 + (buf[pos] - '0');
        pos++;
    }
    if (pos >= len || buf[pos] != ';') return 0;
    pos++;

    /* Parse x coordinate */
    x = 0;
    while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
        x = x * 10 + (buf[pos] - '0');
        pos++;
    }
    if (pos >= len || buf[pos] != ';') return 0;
    pos++;

    /* Parse y coordinate */
    y = 0;
    while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
        y = y * 10 + (buf[pos] - '0');
        pos++;
    }
    if (pos >= len) return 0;
    terminator = buf[pos];
    if (terminator != 'M' && terminator != 'm') return 0;
    pos++;

    /* Build mouse event */
    memset(ev, 0, sizeof(*ev));
    if (button & 32)
        ev->button_event.type = QE_MOTION_EVENT;
    else if (terminator == 'M')
        ev->button_event.type = QE_BUTTON_PRESS_EVENT;
    else
        ev->button_event.type = QE_BUTTON_RELEASE_EVENT;

    ev->button_event.x = x - 1;
    ev->button_event.y = y - 1;

    switch (button & ~(4|8|16|32)) {
    case 0:  ev->button_event.button = QE_BUTTON_LEFT; break;
    case 1:  ev->button_event.button = QE_BUTTON_MIDDLE; break;
    case 2:  ev->button_event.button = QE_BUTTON_RIGHT; break;
    case 64: ev->button_event.button = QE_WHEEL_UP; break;
    case 65: ev->button_event.button = QE_WHEEL_DOWN; break;
    default: ev->button_event.button = 0; break;
    }

    return pos - start;
}

static void test_read_handler(void *opaque)
{
    QEditScreen *s = opaque;
    QEmacsState *qs = s->qs;
    TestDisplayState *ts = s->priv_data;
    QEEvent ev1, *ev = &ev1;
    unsigned char buf[64];
    int n, i;

    n = read(ts->input_fd, buf, sizeof(buf));
    if (n <= 0) {
        /* EOF or error on input — request exit */
        url_exit();
        return;
    }

    for (i = 0; i < n; i++) {
        /* Try to parse SGR mouse escape sequence */
        if (buf[i] == 0x1b && i + 2 < n && buf[i + 1] == '[' && buf[i + 2] == '<') {
            int consumed = test_parse_sgr_mouse(buf, n, i, ev);
            if (consumed > 0) {
                qe_handle_event(qs, ev);
                i += consumed - 1;  /* -1 because loop increments */
                continue;
            }
        }
        memset(ev, 0, sizeof(*ev));
        ev->key_event.type = QE_KEY_EVENT;
        ev->key_event.key = buf[i];
        qe_handle_event(qs, ev);
    }
}

/*--- Init / Close ---*/

static int test_dpy_init(QEditScreen *s, QEmacsState *qs,
                         int w, int h)
{
    TestDisplayState *ts;
    const char *p;

    ts = qe_mallocz(TestDisplayState);
    if (!ts)
        return 1;

    s->qs = qs;
    s->STDIN = stdin;
    s->STDOUT = stdout;
    s->priv_data = ts;
    s->media = CSS_MEDIA_TTY;
    s->charset = &charset_utf8;

    /* Determine screen size from env or defaults */
    p = getenv("QE_TEST_WIDTH");
    s->width = p ? atoi(p) : 80;
    p = getenv("QE_TEST_HEIGHT");
    s->height = p ? atoi(p) : 24;

    if (s->width < 10) s->width = 80;
    if (s->height < 3) s->height = 24;
    if (s->width > MAX_SCREEN_WIDTH) s->width = MAX_SCREEN_WIDTH;
    if (s->height > MAX_SCREEN_LINES) s->height = MAX_SCREEN_LINES;

    /* Set up input fd */
    p = getenv("QE_TEST_INPUT_FD");
    if (p) {
        ts->input_fd = atoi(p);
    } else {
        ts->input_fd = fileno(stdin);
    }
    fcntl(ts->input_fd, F_SETFL, O_NONBLOCK);
    set_read_handler(ts->input_fd, test_read_handler, s);

    /* Set up dump file.
     * QE_TEST_DUMP_MODE controls behavior:
     *   "all"  (default) - append every flush to the file
     *   "last" - rewrite the file on each flush (only latest snapshot)
     */
    ts->dump_fd = -1;
    ts->dump_file = NULL;
    ts->dump_mode_last = 0;
    p = getenv("QE_TEST_DUMP_FILE");
    if (p) {
        ts->dump_file = fopen(p, "w");
        pstrcpy(ts->dump_path, sizeof(ts->dump_path), p);
        const char *mode = getenv("QE_TEST_DUMP_MODE");
        if (mode && strcmp(mode, "last") == 0)
            ts->dump_mode_last = 1;
    }

    ts->flush_count = 0;

    /* Set up resize file path (parent writes "WxH\n", we read on SIGWINCH) */
    ts->resize_path[0] = '\0';
    p = getenv("QE_TEST_RESIZE_FILE");
    if (p)
        pstrcpy(ts->resize_path, sizeof(ts->resize_path), p);

    /* Install SIGWINCH handler for resize support */
    test_screen = s;
    {
        struct sigaction sig;
        sig.sa_handler = test_term_resize;
        sigemptyset(&sig.sa_mask);
        sig.sa_flags = 0;
        sigaction(SIGWINCH, &sig, NULL);
    }

    /* Allocate screen buffer */
    ts->screen_size = s->width * s->height;
    ts->screen = qe_mallocz_array(TestCell, ts->screen_size);
    if (!ts->screen) {
        qe_free(&s->priv_data);
        return 1;
    }

    /* Fill with spaces */
    for (int i = 0; i < ts->screen_size; i++) {
        ts->screen[i].ch = ' ';
        ts->screen[i].fg = 7;  /* default white */
        ts->screen[i].bg = 0;  /* default black */
        ts->screen[i].attrs = 0;
    }

    return 0;
}

static void test_dpy_close(QEditScreen *s)
{
    TestDisplayState *ts = s->priv_data;
    if (ts) {
        if (ts->dump_file) {
            fclose(ts->dump_file);
        }
        qe_free(&ts->screen);
        qe_free(&s->priv_data);
    }
}

/*--- Screen dump output ---*/

/* Write a plain-text "screenshot" of the current screen state.
 * Format:
 *   --- flush N (WxH) ---
 *   |line of text here          |    <- plain text (no color)
 *   ...
 *   --- colors ---
 *   row,col: ch=U+XXXX fg=N bg=N attrs=N   <- only for non-default cells
 *   --- end ---
 */
static void test_dump_screen(QEditScreen *s)
{
    TestDisplayState *ts = s->priv_data;
    FILE *f = ts->dump_file;
    int x, y;

    if (!f)
        return;

    fprintf(f, "--- flush %d (%dx%d) cursor=(%d,%d) ---\n",
            ts->flush_count, s->width, s->height,
            ts->cursor_x, ts->cursor_y);

    /* Plain text representation */
    for (y = 0; y < s->height; y++) {
        fputc('|', f);
        for (x = 0; x < s->width; x++) {
            TestCell *cell = &ts->screen[y * s->width + x];
            char32_t ch = cell->ch;
            if (ch < 32 || ch == 127) ch = '.';
            /* Output UTF-8 */
            if (ch < 0x80) {
                fputc(ch, f);
            } else if (ch < 0x800) {
                fputc(0xC0 | (ch >> 6), f);
                fputc(0x80 | (ch & 0x3F), f);
            } else if (ch < 0x10000) {
                fputc(0xE0 | (ch >> 12), f);
                fputc(0x80 | ((ch >> 6) & 0x3F), f);
                fputc(0x80 | (ch & 0x3F), f);
            } else {
                fputc(0xF0 | (ch >> 18), f);
                fputc(0x80 | ((ch >> 12) & 0x3F), f);
                fputc(0x80 | ((ch >> 6) & 0x3F), f);
                fputc(0x80 | (ch & 0x3F), f);
            }
        }
        fputc('|', f);
        fputc('\n', f);
    }

    /* Color/attribute annotations for non-default cells */
    fprintf(f, "--- colors ---\n");
    for (y = 0; y < s->height; y++) {
        for (x = 0; x < s->width; x++) {
            TestCell *cell = &ts->screen[y * s->width + x];
            /* Skip default cells (space, fg=7, bg=0, no attrs) */
            if (cell->ch == ' ' && cell->fg == 7 && cell->bg == 0 && cell->attrs == 0)
                continue;
            fprintf(f, "  %d,%d: ch=U+%04X fg=%d bg=%d",
                    y, x, (unsigned)cell->ch, cell->fg, cell->bg);
            if (cell->attrs) {
                fprintf(f, " attrs=");
                if (cell->attrs & TESTATTR_BOLD) fprintf(f, "B");
                if (cell->attrs & TESTATTR_UNDERLINE) fprintf(f, "U");
                if (cell->attrs & TESTATTR_ITALIC) fprintf(f, "I");
                if (cell->attrs & TESTATTR_BLINK) fprintf(f, "K");
            }
            fprintf(f, "\n");
        }
    }
    fprintf(f, "--- end ---\n\n");
    fflush(f);
}

/*--- Drawing functions ---*/

static void test_dpy_fill_rectangle(QEditScreen *s,
                                    int x1, int y1, int w, int h,
                                    QEColor color)
{
    TestDisplayState *ts = s->priv_data;
    int x, y;
    int x2 = x1 + w;
    int y2 = y1 + h;

    /* Clip */
    if (x1 < s->clip_x1) x1 = s->clip_x1;
    if (y1 < s->clip_y1) y1 = s->clip_y1;
    if (x2 > s->clip_x2) x2 = s->clip_x2;
    if (y2 > s->clip_y2) y2 = s->clip_y2;

    /* Map QEColor to a simple color index (0-15 range) */
    int bgcolor = qe_map_color(color, xterm_colors, 16, NULL);

    for (y = y1; y < y2; y++) {
        for (x = x1; x < x2; x++) {
            TestCell *cell = &ts->screen[y * s->width + x];
            cell->ch = ' ';
            cell->fg = 7;
            cell->bg = bgcolor;
            cell->attrs = 0;
        }
    }
}

static void test_dpy_xor_rectangle(QEditScreen *s,
                                   int x1, int y1, int w, int h,
                                   QEColor color)
{
    TestDisplayState *ts = s->priv_data;
    int x, y;
    int x2 = x1 + w;
    int y2 = y1 + h;

    if (x1 < s->clip_x1) x1 = s->clip_x1;
    if (y1 < s->clip_y1) y1 = s->clip_y1;
    if (x2 > s->clip_x2) x2 = s->clip_x2;
    if (y2 > s->clip_y2) y2 = s->clip_y2;

    for (y = y1; y < y2; y++) {
        for (x = x1; x < x2; x++) {
            TestCell *cell = &ts->screen[y * s->width + x];
            /* Swap fg and bg to simulate XOR/reverse video */
            uint16_t tmp = cell->fg;
            cell->fg = cell->bg;
            cell->bg = tmp;
        }
    }
}

static QEFont *test_dpy_open_font(qe__unused__ QEditScreen *s,
                                  qe__unused__ int style,
                                  qe__unused__ int size)
{
    QEFont *font;
    font = qe_mallocz(QEFont);
    if (!font) return NULL;
    font->ascent = 0;
    font->descent = 1;
    font->priv_data = NULL;
    return font;
}

static void test_dpy_close_font(qe__unused__ QEditScreen *s, QEFont **fontp)
{
    qe_free(fontp);
}

static void test_dpy_text_metrics(QEditScreen *s, QEFont *font,
                                  QECharMetrics *metrics,
                                  const char32_t *str, int len)
{
    int i, x;
    metrics->font_ascent = font->ascent;
    metrics->font_descent = font->descent;
    x = 0;
    for (i = 0; i < len; i++)
        x += qe_wcwidth(str[i]);
    metrics->width = x;
}

static void test_dpy_draw_text(QEditScreen *s, QEFont *font,
                               int x, int y, const char32_t *str, int len,
                               QEColor color)
{
    TestDisplayState *ts = s->priv_data;
    int fgcolor;
    uint16_t attrs = 0;

    if (y < s->clip_y1 || y >= s->clip_y2 || x >= s->clip_x2)
        return;

    fgcolor = qe_map_color(color, xterm_colors, 16, NULL);

    if (font->style & QE_FONT_STYLE_BOLD)      attrs |= TESTATTR_BOLD;
    if (font->style & QE_FONT_STYLE_UNDERLINE)  attrs |= TESTATTR_UNDERLINE;
    if (font->style & QE_FONT_STYLE_ITALIC)     attrs |= TESTATTR_ITALIC;
    if (font->style & QE_FONT_STYLE_BLINK)      attrs |= TESTATTR_BLINK;

    for (int i = 0; i < len; i++) {
        char32_t ch = str[i];
        int w = qe_wcwidth(ch);

        if (w == 0) continue;  /* skip combining characters for now */
        if (x < s->clip_x1) { x += w; continue; }
        if (x + w > s->clip_x2) break;

        TestCell *cell = &ts->screen[y * s->width + x];
        cell->ch = ch;
        cell->fg = fgcolor;
        /* Preserve existing bg color (set by fill_rectangle) */
        cell->attrs = attrs;
        x += w;

        /* For wide chars, fill the second cell with a space */
        if (w == 2 && x < s->clip_x2) {
            cell = &ts->screen[y * s->width + x];
            cell->ch = ' ';
            cell->fg = fgcolor;
            cell->attrs = attrs;
            /* x already incremented by w */
        }
    }
}

static void test_dpy_set_clip(QEditScreen *s, int x, int y, int w, int h)
{
    s->clip_x1 = x;
    s->clip_y1 = y;
    s->clip_x2 = x + w;
    s->clip_y2 = y + h;
}

/*--- Flush: dump screen state ---*/

static void test_dpy_flush(QEditScreen *s)
{
    TestDisplayState *ts = s->priv_data;
    ts->flush_count++;

    /* In "last" mode, reopen the file to overwrite with latest snapshot */
    if (ts->dump_mode_last && ts->dump_file && ts->dump_path[0]) {
        fclose(ts->dump_file);
        ts->dump_file = fopen(ts->dump_path, "w");
    }

    test_dump_screen(s);
}

/*--- Cursor tracking ---*/

static void test_dpy_cursor_at(QEditScreen *s, int x1, int y1,
                               qe__unused__ int w, qe__unused__ int h)
{
    TestDisplayState *ts = s->priv_data;
    ts->cursor_x = x1;
    ts->cursor_y = y1;
}

/*--- Input pending check ---*/

static int test_dpy_is_user_input_pending(QEditScreen *s)
{
    TestDisplayState *ts = s->priv_data;
    fd_set rfds;
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = 0;
    FD_ZERO(&rfds);
    FD_SET(ts->input_fd, &rfds);
    return select(ts->input_fd + 1, &rfds, NULL, NULL, &tv) > 0;
}

/*--- Stubs for optional/unused functions ---*/

static int test_dpy_bmp_alloc(qe__unused__ QEditScreen *s,
                              qe__unused__ QEBitmap *b)
{ return -1; }

static void test_dpy_bmp_free(qe__unused__ QEditScreen *s,
                              qe__unused__ QEBitmap *b) {}

static void test_dpy_bmp_draw(qe__unused__ QEditScreen *s,
                              qe__unused__ QEBitmap *b,
                              qe__unused__ int dst_x, qe__unused__ int dst_y,
                              qe__unused__ int dst_w, qe__unused__ int dst_h,
                              qe__unused__ int offset_x, qe__unused__ int offset_y,
                              qe__unused__ int flags) {}

static void test_dpy_bmp_lock(qe__unused__ QEditScreen *s,
                              qe__unused__ QEBitmap *b,
                              qe__unused__ QEPicture *pict,
                              qe__unused__ int x1, qe__unused__ int y1,
                              qe__unused__ int w1, qe__unused__ int h1) {}

static void test_dpy_bmp_unlock(qe__unused__ QEditScreen *s,
                                qe__unused__ QEBitmap *b) {}

static int test_dpy_draw_picture(qe__unused__ QEditScreen *s,
                                 qe__unused__ int dst_x, qe__unused__ int dst_y,
                                 qe__unused__ int dst_w, qe__unused__ int dst_h,
                                 qe__unused__ const QEPicture *ip,
                                 qe__unused__ int src_x, qe__unused__ int src_y,
                                 qe__unused__ int src_w, qe__unused__ int src_h,
                                 qe__unused__ int flags)
{ return -1; }

static void test_dpy_describe(QEditScreen *s, EditBuffer *b)
{
    TestDisplayState *ts = s->priv_data;
    eb_printf(b, "  test display: %dx%d, flushes=%d\n",
              s->width, s->height, ts->flush_count);
}

static void test_dpy_sound_bell(qe__unused__ QEditScreen *s) {}

static void test_dpy_suspend(qe__unused__ QEditScreen *s) {}

static void test_dpy_invalidate(QEditScreen *s)
{
    TestDisplayState *ts = s->priv_data;
    int new_w, new_h, new_size, i;

    /* Read new dimensions from resize file if available */
    new_w = s->width;
    new_h = s->height;
    if (ts->resize_path[0]) {
        FILE *f = fopen(ts->resize_path, "r");
        if (f) {
            if (fscanf(f, "%dx%d", &new_w, &new_h) == 2) {
                if (new_w >= 10 && new_w <= MAX_SCREEN_WIDTH)
                    s->width = new_w;
                if (new_h >= 3 && new_h <= MAX_SCREEN_LINES)
                    s->height = new_h;
            }
            fclose(f);
        }
    }

    new_size = s->width * s->height;
    if (new_size != ts->screen_size) {
        qe_free(&ts->screen);
        ts->screen = qe_mallocz_array(TestCell, new_size);
        ts->screen_size = new_size;
    }

    if (ts->screen) {
        for (i = 0; i < ts->screen_size; i++) {
            ts->screen[i].ch = ' ';
            ts->screen[i].fg = 7;
            ts->screen[i].bg = 0;
            ts->screen[i].attrs = 0;
        }
    }

    /* Update clip region to match new dimensions */
    s->clip_x1 = 0;
    s->clip_y1 = 0;
    s->clip_x2 = s->width;
    s->clip_y2 = s->height;
}

/*--- Display driver vtable ---*/

static QEDisplay test_dpy = {
    "test", 1, 2,
    test_dpy_probe,
    test_dpy_init,
    test_dpy_close,
    test_dpy_flush,
    test_dpy_is_user_input_pending,
    test_dpy_fill_rectangle,
    test_dpy_xor_rectangle,
    test_dpy_open_font,
    test_dpy_close_font,
    test_dpy_text_metrics,
    test_dpy_draw_text,
    test_dpy_set_clip,
    NULL, /* dpy_selection_activate */
    NULL, /* dpy_selection_request */
    NULL, /* dpy_set_clipboard */
    NULL, /* dpy_request_clipboard */
    NULL, /* dpy_set_cwd */
    test_dpy_invalidate,
    test_dpy_cursor_at,
    test_dpy_bmp_alloc,
    test_dpy_bmp_free,
    test_dpy_bmp_draw,
    test_dpy_bmp_lock,
    test_dpy_bmp_unlock,
    test_dpy_draw_picture,
    NULL, /* dpy_full_screen */
    test_dpy_describe,
    test_dpy_sound_bell,
    test_dpy_suspend,
    qe_dpy_error,
    NULL, /* next */
};

static int test_display_init(QEmacsState *qs)
{
    return qe_register_display(qs, &test_dpy);
}

qe_module_init(test_display_init);
