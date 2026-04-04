/*
 * Shell buffer stress benchmarks.
 *
 * Profiles the operations that matter for interactive shell performance
 * when running programs with heavy output (e.g. Claude Code streaming
 * responses). Key finding: qe_term_get_pos() walks from screen_top to
 * the cursor on every LF/TAB/BS, which is O(bytes between screen_top
 * and cursor). With SF_INFINITE (the default shell mode), screen_top
 * only advances after 10 000 rows, so every subsequent position query
 * scans the entire accumulated output — O(total output size).
 *
 * Benchmarks here reproduce that hot path at the buffer level and
 * compare it against a hypothetical O(1) variant.
 *
 * Build: make -C tests bench   (via the bench target)
 * Run:   ./o/tests/bench_shell_buffer
 */

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cutils.h"
#include "qe.h"

/* ---- stubs ---- */
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

/* ---- timing ---- */

static int64_t now_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

#define MIN_BENCH_USEC  150000   /* run each bench at least 150 ms */

typedef struct {
    const char *name;
    int64_t total_usec;
    int64_t iterations;
    int64_t min_usec;
} BenchResult;

#define MAX_RESULTS 128
static BenchResult results[MAX_RESULTS];
static int result_count = 0;

static void record(const char *name, int64_t total_usec,
                   int64_t iters, int64_t min_usec) {
    if (result_count < MAX_RESULTS) {
        results[result_count].name      = strdup(name);
        results[result_count].total_usec = total_usec;
        results[result_count].iterations = iters;
        results[result_count].min_usec   = min_usec;
        result_count++;
    }
}

static void print_results(void) {
    int i;
    printf("\n%-55s %10s %10s %10s\n",
           "Benchmark", "ops/sec", "usec/op", "min_usec");
    printf("%-55s %10s %10s %10s\n",
           "---------", "-------", "-------", "--------");
    for (i = 0; i < result_count; i++) {
        BenchResult *r = &results[i];
        double uop = (double)r->total_usec / r->iterations;
        double ops = r->iterations * 1e6 / r->total_usec;
        printf("%-55s %10.0f %10.2f %10lld\n",
               r->name, ops, uop, (long long)r->min_usec);
    }
    printf("\n");
}

/* ---- setup ---- */

static QEmacsState qs_storage;
static QEmacsState *qs = &qs_storage;
static int buf_id = 0;

static void bench_init(void) {
    memset(qs, 0, sizeof(*qs));
    qs->default_tab_width = 8;
    qs->default_fill_column = 80;
    qs->default_eol_type = EOL_UNIX;
    charset_init(qs);
}

static EditBuffer *new_buf(void) {
    char name[64];
    snprintf(name, sizeof name, "*bench-%d*", buf_id++);
    return qe_new_buffer(qs, name, BF_UTF8);
}

/* Append n lines of shell-like output (20 bytes each) */
static void append_lines(EditBuffer *b, int n) {
    char line[64];
    int i;
    for (i = 0; i < n; i++) {
        int len = snprintf(line, sizeof line, "$ output line %05d\n", i);
        eb_insert(b, b->total_size, line, len);
    }
}

/* Append n lines of Claude-Code-like output:
 * longer lines with ANSI color codes, resembling code block output */
static void append_claude_lines(EditBuffer *b, int n) {
    /* Mix of short and long lines, ANSI escapes, continuation */
    static const char *templates[] = {
        "\033[1;32m✓\033[0m Writing tests for buffer module\n",
        "    fn test_insert_large() {\033[0m\n",
        "        let mut buf = Buffer::new();\033[0m\n",
        "        for i in 0..10_000 { buf.push(i as u8); }\033[0m\n",
        "        assert_eq!(buf.len(), 10_000);\n",
        "\033[2m// benchmark: 1.23 ms per 1k lines\033[0m\n",
        "\033[33mWarning:\033[0m Large file detected (>1MB)\n",
        "│ \033[36minfo\033[0m Processing chunk 042/128 ...\n",
    };
    int ntpl = sizeof(templates) / sizeof(templates[0]);
    int i;
    for (i = 0; i < n; i++) {
        const char *t = templates[i % ntpl];
        eb_insert(b, b->total_size, t, strlen(t));
    }
}

/* =========================================================
 * Core hot-path: qe_term_get_pos analogue.
 *
 * This is the innermost loop of shell.c:qe_term_get_pos().
 * It walks from 'from_offset' to 'to_offset' counting
 * terminal rows (wrapping at 'cols' columns).
 * Called on every LF, TAB, and BS character processed.
 * ========================================================= */
static int position_walk(EditBuffer *b, int from_offset, int to_offset,
                         int cols) {
    int offset = from_offset;
    int x = 0, y = 0;
    while (offset < to_offset) {
        int next;
        char32_t c = eb_nextc(b, offset, &next);
        if (c == '\n') {
            y++;
            x = 0;
        } else {
            /* approximate: treat everything as width-1 (ASCII output) */
            x++;
            if (x >= cols) {
                y++;
                x = 0;
            }
        }
        offset = next;
    }
    return y;
}

/* =========================================================
 * Benchmark 1: position_walk as a function of buffer size.
 *
 * Models: LF handler calls qe_term_get_pos(screen_top, cursor).
 * With SF_INFINITE, screen_top stays near 0; cursor is at end.
 * Each call walks the entire accumulated buffer.
 * ========================================================= */
static void bench_position_walk(int scrollback_lines, int line_len, int cols) {
    char name[96];
    snprintf(name, sizeof name,
             "position_walk  %5dk lines x %2d chars", scrollback_lines / 1000, line_len);

    EditBuffer *b = new_buf();

    /* Build scrollback with lines of known length */
    char line[256];
    if (line_len > (int)sizeof(line) - 2) line_len = (int)sizeof(line) - 2;
    memset(line, 'x', line_len);
    line[line_len] = '\n';

    int i;
    for (i = 0; i < scrollback_lines; i++) {
        eb_insert(b, b->total_size, line, line_len + 1);
    }

    int cursor = b->total_size;
    int screen_top = 0;          /* SF_INFINITE: starts at 0, stays there */

    int64_t start = now_usec(), elapsed = 0, iters = 0, min_op = INT64_MAX;
    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        volatile int r = position_walk(b, screen_top, cursor, cols);
        (void)r;
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record(name, elapsed, iters, min_op);
    eb_free(&b);
}

/* =========================================================
 * Benchmark 2: LF streaming simulation.
 *
 * Simulates the shell processing a stream of output one byte
 * at a time. On every LF, we call position_walk(screen_top, cursor)
 * just as qe_term_get_pos is called in qe_term_emulate().
 *
 * 'scrollback_lines' lines of pre-existing history are built
 * first (untimed); then we measure processing 'new_lines' more.
 * ========================================================= */
static void bench_lf_streaming(int scrollback_lines, int new_lines,
                                int line_len, int cols) {
    char name[96];
    snprintf(name, sizeof name,
             "lf_stream  %5dk hist + %dk new, %2d-char lines",
             scrollback_lines / 1000, new_lines / 1000, line_len);

    EditBuffer *b = new_buf();

    char line[256];
    if (line_len > (int)sizeof(line) - 2) line_len = (int)sizeof(line) - 2;
    memset(line, 'x', line_len);
    line[line_len] = '\n';

    /* build history (untimed) */
    int i;
    for (i = 0; i < scrollback_lines; i++) {
        eb_insert(b, b->total_size, line, line_len + 1);
    }

    int screen_top = 0;

    /* now time: process new_lines more lines, doing position_walk on each LF */
    int64_t t0 = now_usec();
    volatile int rows_total = 0;
    for (i = 0; i < new_lines; i++) {
        eb_insert(b, b->total_size, line, line_len);          /* text */
        eb_insert(b, b->total_size, "\n", 1);                 /* LF   */
        int cursor = b->total_size;
        rows_total += position_walk(b, screen_top, cursor, cols);
    }
    int64_t elapsed = now_usec() - t0;
    (void)rows_total;

    record(name, elapsed, new_lines, elapsed > 0 ? elapsed / new_lines : 0);
    eb_free(&b);
}

/* =========================================================
 * Benchmark 3: same as bench_lf_streaming but screen_top
 * is kept close to the cursor (hypothetical fix: always track
 * the last 'rows' lines).
 *
 * Shows what performance WOULD be if screen_top advanced
 * promptly (e.g. limited scrollback or eager update).
 * ========================================================= */
static void bench_lf_streaming_tracked(int scrollback_lines, int new_lines,
                                       int line_len, int cols, int visible_rows) {
    char name[96];
    snprintf(name, sizeof name,
             "lf_stream_tracked  %5dk hist, %dk new, screen_top=%d rows back",
             scrollback_lines / 1000, new_lines / 1000, visible_rows);

    EditBuffer *b = new_buf();

    char line[256];
    if (line_len > (int)sizeof(line) - 2) line_len = (int)sizeof(line) - 2;
    memset(line, 'x', line_len);
    line[line_len] = '\n';

    int i;
    for (i = 0; i < scrollback_lines; i++) {
        eb_insert(b, b->total_size, line, line_len + 1);
    }

    /* screen_top = last visible_rows lines (as if screen_top tracks cursor) */
    int screen_top = b->total_size;
    {
        int nl = 0;
        int off = b->total_size;
        while (off > 0 && nl < visible_rows) {
            int prev;
            if (eb_prevc(b, off, &prev) == '\n') nl++;
            off = prev;
        }
        screen_top = off;
    }

    int64_t t0 = now_usec();
    volatile int rows_total = 0;
    for (i = 0; i < new_lines; i++) {
        eb_insert(b, b->total_size, line, line_len);
        eb_insert(b, b->total_size, "\n", 1);
        int cursor = b->total_size;
        /* screen_top advances by 1 line on each new LF */
        screen_top = eb_next_line(b, screen_top);
        rows_total += position_walk(b, screen_top, cursor, cols);
    }
    int64_t elapsed = now_usec() - t0;
    (void)rows_total;

    record(name, elapsed, new_lines, elapsed > 0 ? elapsed / new_lines : 0);
    eb_free(&b);
}

/* =========================================================
 * Benchmark 4: Claude Code-like output pattern.
 *
 * Mixes ANSI-colored lines of varying length, then measures
 * how position_walk scales after each "response chunk"
 * (a burst of ~200 lines simulating one code block output).
 * ========================================================= */
static void bench_claude_code_burst(int n_bursts) {
    char name[96];
    snprintf(name, sizeof name,
             "claude_code_burst  %d bursts x 200 lines", n_bursts);

    EditBuffer *b = new_buf();
    int screen_top = 0;
    int cols = 120;             /* Claude Code typically runs in wide terminals */
    int burst_lines = 200;

    int64_t t0 = now_usec();
    int64_t pos_walk_usec = 0;
    int i;
    for (i = 0; i < n_bursts; i++) {
        /* append one burst of output (untimed insert) */
        append_claude_lines(b, burst_lines);

        /* time the position walk that follows the last LF of the burst */
        int cursor = b->total_size;
        int64_t pw0 = now_usec();
        volatile int r = position_walk(b, screen_top, cursor, cols);
        (void)r;
        pos_walk_usec += now_usec() - pw0;
    }
    int64_t total_usec = now_usec() - t0;
    (void)total_usec;

    record(name, pos_walk_usec, n_bursts,
           pos_walk_usec > 0 ? pos_walk_usec / n_bursts : 0);
    eb_free(&b);
}

/* =========================================================
 * Benchmark 5: raw append throughput (control baseline).
 * Shows that the buffer itself is not the bottleneck.
 * ========================================================= */
static void bench_append_baseline(int scrollback_lines) {
    char name[96];
    snprintf(name, sizeof name,
             "append_baseline    %5dk lines already in buf",
             scrollback_lines / 1000);

    EditBuffer *b = new_buf();
    append_lines(b, scrollback_lines);

    char line[] = "$ hello world\n";
    int len = strlen(line);

    int64_t start = now_usec(), elapsed = 0, iters = 0, min_op = INT64_MAX;
    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        eb_insert(b, b->total_size, line, len);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record(name, elapsed, iters, min_op);
    eb_free(&b);
}

/* ---- main ---- */

int main(void) {
    bench_init();

    printf("qemacs shell buffer stress benchmark\n");
    printf("=====================================\n");
    printf(
        "Models the hot path in qe_term_emulate(): every LF/TAB/BS character\n"
        "triggers qe_term_get_pos(screen_top, cursor) — a full linear walk\n"
        "through all content between screen_top and the cursor.\n"
        "With SF_INFINITE (default shell mode), screen_top stays near the\n"
        "beginning until 10 000 rows accumulate, so the walk is O(output size).\n"
        "\n"
    );

    /* --- 1. Position walk cost vs buffer size --- */
    printf("=== 1. position_walk cost vs total history (80-char lines) ===\n");
    bench_position_walk(  1000, 79, 80);
    bench_position_walk(  5000, 79, 80);
    bench_position_walk( 10000, 79, 80);
    bench_position_walk( 50000, 79, 80);

    /* --- 2. Position walk with line lengths that don't wrap --- */
    printf("\n=== 2. position_walk, short lines (no wrapping) ===\n");
    bench_position_walk(  1000, 20, 80);
    bench_position_walk( 10000, 20, 80);
    bench_position_walk( 50000, 20, 80);

    /* --- 3. LF streaming: existing history + more output --- */
    printf("\n=== 3. LF streaming: cost per new line as history grows ===\n");
    printf("    (each row = buffer insert + position_walk from screen_top)\n\n");
    bench_lf_streaming(    0,  1000, 79, 80);
    bench_lf_streaming( 1000,  1000, 79, 80);
    bench_lf_streaming( 5000,  1000, 79, 80);
    bench_lf_streaming(10000,  1000, 79, 80);

    /* --- 4. Same scenario but screen_top tracks cursor --- */
    printf("\n=== 4. LF streaming with screen_top tracking (hypothetical fix) ===\n");
    printf("    (screen_top advances 1 line per LF; position_walk is O(visible_rows))\n\n");
    bench_lf_streaming_tracked(    0,  1000, 79, 80, 25);
    bench_lf_streaming_tracked( 1000,  1000, 79, 80, 25);
    bench_lf_streaming_tracked( 5000,  1000, 79, 80, 25);
    bench_lf_streaming_tracked(10000,  1000, 79, 80, 25);

    /* --- 5. Claude Code burst pattern --- */
    printf("\n=== 5. Claude Code burst pattern (200-line bursts, ANSI color) ===\n");
    printf("    (time shown is for position_walk after each burst only)\n\n");
    bench_claude_code_burst( 5);
    bench_claude_code_burst(15);
    bench_claude_code_burst(30);
    bench_claude_code_burst(50);

    /* --- 6. Append baseline (shows the buffer itself is O(1)) --- */
    printf("\n=== 6. Raw append baseline (buffer is not the bottleneck) ===\n");
    bench_append_baseline(  1000);
    bench_append_baseline( 10000);
    bench_append_baseline( 50000);

    print_results();

    /* Summary */
    printf("Key findings:\n");
    printf("  - position_walk scales linearly with history depth\n");
    printf("  - With screen_top tracking (bench 4), cost is nearly constant\n");
    printf("  - Raw buffer append (bench 6) is O(1) — not the bottleneck\n");
    printf("  - Claude Code slowdown = position_walk overhead per output line\n\n");

    /* Compute and print speedup ratios */
    {
        int i, j;
        double walk_1k = -1, walk_50k = -1;
        double stream_0 = -1, stream_10k = -1;
        double tracked_0 = -1, tracked_10k = -1;

        for (i = 0; i < result_count; i++) {
            double uop = (double)results[i].total_usec / results[i].iterations;
            if (strstr(results[i].name, "position_walk") &&
                strstr(results[i].name, "1k") &&
                strstr(results[i].name, "79"))   walk_1k  = uop;
            if (strstr(results[i].name, "position_walk") &&
                strstr(results[i].name, "50k") &&
                strstr(results[i].name, "79"))   walk_50k = uop;
            if (strstr(results[i].name, "lf_stream") &&
                !strstr(results[i].name, "tracked") &&
                strstr(results[i].name, "    0k")) stream_0 = uop;
            if (strstr(results[i].name, "lf_stream") &&
                !strstr(results[i].name, "tracked") &&
                strstr(results[i].name, "10k"))   stream_10k = uop;
            if (strstr(results[i].name, "lf_stream_tracked") &&
                strstr(results[i].name, "    0k")) tracked_0 = uop;
            if (strstr(results[i].name, "lf_stream_tracked") &&
                strstr(results[i].name, "10k"))   tracked_10k = uop;
        }
        (void)j;

        if (walk_1k > 0 && walk_50k > 0)
            printf("  position_walk slowdown  1k→50k lines: %.1fx\n",
                   walk_50k / walk_1k);
        if (stream_0 > 0 && stream_10k > 0)
            printf("  LF streaming slowdown   0k→10k hist:  %.1fx\n",
                   stream_10k / stream_0);
        if (tracked_0 > 0 && stream_0 > 0)
            printf("  Tracked vs untracked at 0k hist:       %.1fx faster\n",
                   stream_0 / tracked_0);
        if (tracked_10k > 0 && stream_10k > 0)
            printf("  Tracked vs untracked at 10k hist:      %.1fx faster\n",
                   stream_10k / tracked_10k);
        printf("\n");
    }

    return 0;
}
