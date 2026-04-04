/*
 * Stress test for shell buffer with large scrollback.
 *
 * Shell mode stores all output in a single EditBuffer and tracks
 * 'screen_top' as a byte offset into that buffer. All operations
 * on the visible screen walk forward from screen_top — so appending
 * output and rendering the current screen must stay O(1) in the
 * size of the scrollback history.
 *
 * This file tests both correctness (the buffer stays valid and
 * consistent as scrollback grows) and performance (operations
 * do not degrade with scrollback size).
 *
 * Build: make -C tests  (auto-discovered via test_*.c wildcard)
 * Run:   ./o/tests/test_shell_buffer
 */

#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "testlib.h"
#include "cutils.h"
#include "qe.h"

/* ---- stubs for symbols referenced by buffer.c/util.c ---- */

void qe_put_error(QEmacsState *qs, const char *fmt, ...) { (void)qs; (void)fmt; }
void put_error(EditState *s, const char *fmt, ...) { (void)s; (void)fmt; }
void put_status(EditState *s, const char *fmt, ...) { (void)s; (void)fmt; }
void do_refresh(EditState *s) { (void)s; }
void qe_display(QEmacsState *qs) { (void)qs; }
int qe_register_transient_binding(QEmacsState *qs, const char *cmd, const char *keys) {
    (void)qs; (void)cmd; (void)keys;
    return 0;
}
struct QETraceDef const qe_trace_defs[] = {};
size_t const qe_trace_defs_count = 0;

/* ---- test setup ---- */

static QEmacsState qs_storage;
static QEmacsState *qs = &qs_storage;
static int sbuf_id = 0;

static void test_init(void) {
    memset(qs, 0, sizeof(*qs));
    qs->default_tab_width = 8;
    qs->default_fill_column = 80;
    qs->default_eol_type = EOL_UNIX;
    charset_init(qs);
}

static EditBuffer *new_shell_buf(void) {
    char name[64];
    snprintf(name, sizeof name, "*shell-stress-%d*", sbuf_id++);
    return qe_new_buffer(qs, name, BF_UTF8);
}

static int64_t now_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* ---- shell buffer simulation helpers ---- */

/*
 * Append nlines of shell-like output to the buffer.
 * Each line: "$ output line NNNNN\n" (21 bytes).
 */
static void append_shell_lines(EditBuffer *b, int nlines) {
    char line[64];
    int i;
    for (i = 0; i < nlines; i++) {
        int len = snprintf(line, sizeof line, "$ output line %05d\n", i);
        eb_insert(b, b->total_size, line, len);
    }
}

/*
 * Append nlines of ANSI-colored output (longer lines with escape sequences).
 * Simulates a colored PS1 prompt + command output.
 */
static void append_ansi_lines(EditBuffer *b, int nlines) {
    char line[256];
    int i;
    for (i = 0; i < nlines; i++) {
        int len = snprintf(line, sizeof line,
                           "\033[01;32muser@host\033[0m:\033[01;34m~/work\033[0m$ "
                           "grep -r pattern /tmp/dir%05d\n", i);
        eb_insert(b, b->total_size, line, len);
    }
}

/*
 * Walk backward from the end of the buffer to find the offset that
 * marks the start of the visible screen (rows lines from the bottom).
 *
 * This mirrors what ShellState.screen_top represents: a raw byte
 * offset into the buffer. In the real shell mode screen_top is
 * updated via eb_offset_callback; here we recompute it to simulate
 * the position after a large amount of scrollback.
 */
static int find_screen_top(EditBuffer *b, int rows) {
    int offset = b->total_size;
    int newlines = 0;
    /*
     * Walk backward counting rows+1 newlines. The (rows+1)th newline
     * from the end is the boundary between scrollback and the visible
     * screen. screen_top = first character after that newline.
     *
     * If the buffer has <= rows lines we run out of content and
     * return offset 0 (show everything from the start).
     */
    while (offset > 0 && newlines <= rows) {
        int prev;
        char32_t c = eb_prevc(b, offset, &prev);
        if (c == '\n')
            newlines++;
        offset = prev;
    }
    /* Step past the boundary newline */
    if (offset > 0) {
        int next;
        eb_nextc(b, offset, &next);
        offset = next;
    }
    return offset;
}

/*
 * Simulate the shell display pass: walk forward from screen_top,
 * reading 'rows' lines of up to 'cols' bytes each.
 * Returns the number of lines successfully read.
 */
static int read_screen_window(EditBuffer *b, int screen_top, int rows, int cols) {
    char line_buf[256];
    int read_cols = cols < (int)sizeof(line_buf) - 1 ? cols : (int)sizeof(line_buf) - 1;
    int offset = screen_top;
    int lines_read = 0;
    while (lines_read < rows && offset < b->total_size) {
        eb_read(b, offset, line_buf, read_cols);
        offset = eb_next_line(b, offset);
        lines_read++;
    }
    return lines_read;
}

/*
 * Simulate one keystroke cycle for the shell:
 *   - append one line of output (new terminal output arrives)
 *   - advance screen_top by one line (terminal scrolled by one row)
 *   - read the visible window (display refresh)
 */
static void shell_keystroke_cycle(EditBuffer *b, int *screen_top, int rows, int cols) {
    char line[64];
    int len = snprintf(line, sizeof line, "$ new output\n");
    eb_insert(b, b->total_size, line, len);
    *screen_top = eb_next_line(b, *screen_top);
    read_screen_window(b, *screen_top, rows, cols);
}

/* ---- correctness tests ---- */

TEST(shell_buf, basic_append_grows_buffer) {
    EditBuffer *b = new_shell_buf();
    ASSERT_EQ(b->total_size, 0);
    append_shell_lines(b, 100);
    /* "$ output line NNNNN\n" = 20 bytes */
    ASSERT_TRUE(b->total_size >= 100 * 20);
    eb_free(&b);
}

TEST(shell_buf, screen_top_valid_after_large_scrollback) {
    EditBuffer *b = new_shell_buf();
    int rows = 25;

    append_shell_lines(b, 10000);
    int screen_top = find_screen_top(b, rows);

    ASSERT_TRUE(screen_top >= 0);
    ASSERT_TRUE(screen_top < b->total_size);

    int lines = read_screen_window(b, screen_top, rows, 80);
    ASSERT_EQ(lines, rows);

    eb_free(&b);
}

TEST(shell_buf, screen_top_advances_with_output) {
    EditBuffer *b = new_shell_buf();
    int rows = 25;

    append_shell_lines(b, 1000);
    int top1 = find_screen_top(b, rows);

    append_shell_lines(b, 100);
    int top2 = find_screen_top(b, rows);

    /* screen_top must have moved forward */
    ASSERT_TRUE(top2 > top1);
    eb_free(&b);
}

TEST(shell_buf, ansi_lines_readable_with_large_scrollback) {
    EditBuffer *b = new_shell_buf();
    int rows = 25;

    append_ansi_lines(b, 5000);
    int screen_top = find_screen_top(b, rows);

    ASSERT_TRUE(screen_top >= 0);
    ASSERT_TRUE(screen_top < b->total_size);

    int lines = read_screen_window(b, screen_top, rows, 80);
    ASSERT_EQ(lines, rows);

    eb_free(&b);
}

TEST(shell_buf, newline_count_exact) {
    EditBuffer *b = new_shell_buf();
    int nlines = 2000;
    append_shell_lines(b, nlines);

    /* Count newlines by walking the full buffer */
    int count = 0;
    int offset = 0;
    while (offset < b->total_size) {
        int next;
        char32_t c = eb_nextc(b, offset, &next);
        if (c == '\n')
            count++;
        offset = next;
    }
    ASSERT_EQ(count, nlines);
    eb_free(&b);
}

TEST(shell_buf, screen_window_content_stable) {
    EditBuffer *b = new_shell_buf();
    int rows = 10;
    char before[512], after[512];
    int before_len, after_len;

    /* Fill 1000 lines of scrollback */
    append_shell_lines(b, 1000);
    int screen_top = find_screen_top(b, rows);

    /* Capture exactly 'rows' lines from screen_top */
    before_len = eb_read(b, screen_top, before, sizeof before - 1);
    before[before_len] = '\0';

    /* Append 500 more lines AFTER screen_top.
     * The bytes at and beyond screen_top that we already read must
     * still be there unchanged — eb_insert at the end never touches
     * existing data. The total readable content grows, so after_len
     * will be >= before_len; we only check the original bytes match. */
    append_shell_lines(b, 500);
    after_len = eb_read(b, screen_top, after, sizeof after - 1);
    after[after_len] = '\0';

    ASSERT_TRUE(after_len >= before_len);
    ASSERT_MEMEQ(before, after, before_len);

    eb_free(&b);
}

TEST(shell_buf, next_line_walks_forward_correctly) {
    EditBuffer *b = new_shell_buf();

    /* Insert 5 known lines */
    eb_insert(b, b->total_size, "line0\n", 6);
    eb_insert(b, b->total_size, "line1\n", 6);
    eb_insert(b, b->total_size, "line2\n", 6);

    int off0 = 0;
    int off1 = eb_next_line(b, off0);
    int off2 = eb_next_line(b, off1);

    ASSERT_EQ(off0, 0);
    ASSERT_EQ(off1, 6);
    ASSERT_EQ(off2, 12);

    char buf[8];
    eb_read(b, off0, buf, 5); buf[5] = '\0';
    ASSERT_STREQ(buf, "line0");

    eb_read(b, off1, buf, 5); buf[5] = '\0';
    ASSERT_STREQ(buf, "line1");

    eb_free(&b);
}

/* ---- performance degradation tests ---- */

/*
 * Measure the cost of appending one line at the end of a buffer
 * that already has 'scrollback_lines' of history.
 * Returns usec per line appended.
 */
static double measure_append_usec_per_line(int scrollback_lines) {
    EditBuffer *b = new_shell_buf();
    char line[64];

    /* Build scrollback (untimed) */
    append_shell_lines(b, scrollback_lines);

    /* Time appending 2000 more lines */
    int iters = 2000;
    int i;
    int64_t t0 = now_usec();
    for (i = 0; i < iters; i++) {
        int len = snprintf(line, sizeof line, "appended line %05d\n", i);
        eb_insert(b, b->total_size, line, len);
    }
    int64_t elapsed = now_usec() - t0;

    eb_free(&b);
    return (double)elapsed / iters;
}

/*
 * Measure the cost of reading the visible screen window (rows lines
 * from screen_top) for a buffer with 'scrollback_lines' of history.
 */
static double measure_read_window_usec(int scrollback_lines, int rows) {
    EditBuffer *b = new_shell_buf();

    append_shell_lines(b, scrollback_lines);
    int screen_top = find_screen_top(b, rows);

    int iters = 2000;
    int i;
    volatile int result = 0;
    int64_t t0 = now_usec();
    for (i = 0; i < iters; i++) {
        result += read_screen_window(b, screen_top, rows, 80);
    }
    int64_t elapsed = now_usec() - t0;
    (void)result;

    eb_free(&b);
    return (double)elapsed / iters;
}

/*
 * Measure the full shell keystroke cycle:
 *   append one line + advance screen_top + read visible window.
 */
static double measure_keystroke_cycle_usec(int scrollback_lines, int rows) {
    EditBuffer *b = new_shell_buf();

    append_shell_lines(b, scrollback_lines);
    int screen_top = find_screen_top(b, rows);

    int iters = 2000;
    int i;
    int64_t t0 = now_usec();
    for (i = 0; i < iters; i++) {
        shell_keystroke_cycle(b, &screen_top, rows, 80);
    }
    int64_t elapsed = now_usec() - t0;

    eb_free(&b);
    return (double)elapsed / iters;
}

/*
 * Performance test: appending output must not slow down as scrollback grows.
 *
 * eb_insert at the end of a page-based buffer is O(1): it extends the
 * last page (if room) or allocates a new one. The total_size counter
 * grows but no existing data is touched.
 */
TEST(shell_perf, append_does_not_degrade) {
    double usec_1k   = measure_append_usec_per_line(1000);
    double usec_10k  = measure_append_usec_per_line(10000);
    double usec_100k = measure_append_usec_per_line(100000);

    printf("\n  Append usec/line: 1k=%.2f  10k=%.2f  100k=%.2f\n",
           usec_1k, usec_10k, usec_100k);

    /*
     * Allow up to 20x variance for measurement noise on slow/shared CI
     * machines. Real O(1) append should be within ~2x.
     */
    double baseline = usec_1k > 0.001 ? usec_1k : 0.001;
    ASSERT_TRUE(usec_10k  / baseline < 20.0);
    ASSERT_TRUE(usec_100k / baseline < 20.0);
}

/*
 * Performance test: reading the visible window must not slow down
 * as scrollback grows.
 *
 * The display reads rows lines forward from screen_top, which is a
 * fixed byte offset. The read cost is O(rows * line_len), independent
 * of how much history precedes screen_top.
 */
TEST(shell_perf, read_window_does_not_degrade) {
    int rows = 25;
    double usec_1k   = measure_read_window_usec(1000,   rows);
    double usec_10k  = measure_read_window_usec(10000,  rows);
    double usec_100k = measure_read_window_usec(100000, rows);

    printf("\n  Read window usec/op: 1k=%.2f  10k=%.2f  100k=%.2f\n",
           usec_1k, usec_10k, usec_100k);

    double baseline = usec_1k > 0.001 ? usec_1k : 0.001;
    ASSERT_TRUE(usec_10k  / baseline < 20.0);
    ASSERT_TRUE(usec_100k / baseline < 20.0);
}

/*
 * Performance test: the full keystroke cycle (append + advance
 * screen_top + display refresh) must not slow down with scrollback.
 *
 * This is the critical path for interactive shell responsiveness.
 * All three operations are O(1) in scrollback size.
 */
TEST(shell_perf, keystroke_cycle_does_not_degrade) {
    int rows = 25;
    double usec_1k   = measure_keystroke_cycle_usec(1000,   rows);
    double usec_10k  = measure_keystroke_cycle_usec(10000,  rows);
    double usec_100k = measure_keystroke_cycle_usec(100000, rows);

    printf("\n  Keystroke cycle usec/op: 1k=%.2f  10k=%.2f  100k=%.2f\n",
           usec_1k, usec_10k, usec_100k);

    double baseline = usec_1k > 0.001 ? usec_1k : 0.001;
    ASSERT_TRUE(usec_10k  / baseline < 20.0);
    ASSERT_TRUE(usec_100k / baseline < 20.0);
}

/* ---- main ---- */

int main(void) {
    test_init();
    return testlib_run_all();
}
