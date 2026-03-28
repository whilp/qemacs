/*
 * Benchmark for qemacs buffer and rendering operations.
 *
 * Measures the critical path for keystroke responsiveness:
 *   1. Buffer insert/delete at various sizes
 *   2. Buffer read (simulating display refresh reads)
 *   3. Sequential character insertion (typing simulation)
 *   4. Line-oriented operations (goto, count)
 *
 * Build: make bench  (from tests/)
 * Run:   ./bench_buffer
 *
 * Results are printed as operations/sec and usec/op.
 * Target: < 1ms total for a single keystroke cycle (insert + redisplay read).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "cutils.h"
#include "qe.h"

/* ---- stubs for symbols referenced by buffer.c/util.c but not needed ---- */

void qe_put_error(QEmacsState *qs, const char *fmt, ...) { (void)qs; (void)fmt; }
void put_error(EditState *s, const char *fmt, ...) { (void)s; (void)fmt; }
void put_status(EditState *s, const char *fmt, ...) { (void)s; (void)fmt; }
void do_refresh(EditState *s) { (void)s; }
void qe_display(QEmacsState *qs) { (void)qs; }
int qe_register_transient_binding(QEmacsState *qs, const char *cmd, const char *keys) {
    (void)qs; (void)cmd; (void)keys;
    return 0;
}

/* trace stubs */
struct QETraceDef const qe_trace_defs[] = {};
size_t const qe_trace_defs_count = 0;

/* ---- timing helpers ---- */

static int64_t now_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* Minimum benchmark duration in microseconds (100ms) */
#define MIN_BENCH_USEC  100000

typedef struct {
    const char *name;
    int64_t total_usec;
    int64_t iterations;
    int64_t min_usec;  /* min single-op time (for latency benchmarks) */
} BenchResult;

#define MAX_RESULTS 64
static BenchResult results[MAX_RESULTS];
static int result_count = 0;

static void record_result(const char *name, int64_t total_usec, int64_t iterations, int64_t min_usec) {
    if (result_count < MAX_RESULTS) {
        results[result_count].name = strdup(name);
        results[result_count].total_usec = total_usec;
        results[result_count].iterations = iterations;
        results[result_count].min_usec = min_usec;
        result_count++;
    }
}

static void print_results(void) {
    int i;
    printf("\n%-45s %12s %12s %12s\n", "Benchmark", "ops/sec", "usec/op", "min_usec");
    printf("%-45s %12s %12s %12s\n", "---------", "-------", "-------", "--------");
    for (i = 0; i < result_count; i++) {
        BenchResult *r = &results[i];
        double usec_per_op = (double)r->total_usec / r->iterations;
        double ops_per_sec = r->iterations * 1000000.0 / r->total_usec;
        printf("%-45s %12.0f %12.2f %12lld\n",
               r->name, ops_per_sec, usec_per_op, (long long)r->min_usec);
    }
    printf("\n");
}

/* ---- minimal editor state setup ---- */

static QEmacsState qs_storage;
static QEmacsState *qs = &qs_storage;

static void bench_init(void) {
    memset(qs, 0, sizeof(*qs));
    qs->default_tab_width = 8;
    qs->default_fill_column = 80;
    qs->default_eol_type = EOL_UNIX;
    charset_init(qs);
}

static int bench_buf_id = 0;

static EditBuffer *bench_new_buffer(const char *name) {
    char uname[64];
    snprintf(uname, sizeof uname, "*%s-%d*", name, bench_buf_id++);
    return qe_new_buffer(qs, uname, BF_UTF8 | BF_SAVELOG);
}

static void bench_free_buffer(EditBuffer **bp) {
    eb_free(bp);
}

/* Fill buffer with N lines of text */
static void fill_buffer_lines(EditBuffer *b, int nlines, int line_len) {
    char line[256];
    int i;

    if (line_len > (int)sizeof(line) - 2)
        line_len = (int)sizeof(line) - 2;

    memset(line, 'x', line_len);
    line[line_len] = '\n';
    line[line_len + 1] = '\0';

    for (i = 0; i < nlines; i++) {
        eb_insert(b, b->total_size, line, line_len + 1);
    }
}

/* ---- benchmarks ---- */

/*
 * Benchmark: insert a single character at end of buffer.
 * This is the hot path for typing.
 */
static void bench_insert_char_at_end(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "insert_char_end_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        eb_insert(b, b->total_size, "a", 1);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: insert a single character at beginning of buffer.
 * Worst case — forces page splits and data movement.
 */
static void bench_insert_char_at_start(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "insert_char_start_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        eb_insert(b, 0, "a", 1);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: insert a single character at middle of buffer.
 */
static void bench_insert_char_at_middle(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "insert_char_mid_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int mid = b->total_size / 2;
        int64_t t0 = now_usec();
        eb_insert(b, mid, "a", 1);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: delete a single character at middle of buffer.
 */
static void bench_delete_char_at_middle(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "delete_char_mid_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int mid = b->total_size / 2;
        /* re-insert so we don't run out of content */
        eb_insert(b, mid, "a", 1);
        int64_t t0 = now_usec();
        eb_delete(b, mid, 1);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: sequential character typing simulation.
 * Insert characters one at a time at a moving cursor, like real typing.
 */
static void bench_typing_simulation(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "typing_sim_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int cursor = b->total_size / 2;  /* start typing in the middle */
    const char *text = "The quick brown fox jumps over the lazy dog. ";
    int textlen = strlen(text);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        char ch = text[iters % textlen];
        int64_t t0 = now_usec();
        eb_insert(b, cursor, &ch, 1);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        cursor++;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: read a screen's worth of data from buffer.
 * Simulates what the display code does to render visible lines.
 * Reads ~80 cols x 50 rows = 4000 bytes from middle of buffer.
 */
static void bench_read_screen(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "read_screen_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    char screen_buf[4096];
    int mid = b->total_size / 2;

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        eb_read(b, mid, screen_buf, sizeof screen_buf);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: eb_goto_offset for line navigation.
 * Simulates scrolling / cursor movement to a line number.
 */
static void bench_goto_line(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "goto_line_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int target_line = buf_lines / 2;

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int line = (target_line + (iters % 100)) % buf_lines;
        int64_t t0 = now_usec();
        eb_goto_pos(b, line, 0);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: eb_get_pos (offset → line/col conversion).
 * Used heavily during display to compute line numbers.
 */
static void bench_get_pos(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "get_pos_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int mid = b->total_size / 2;

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int line, col;
        int offset = mid + (iters % 1000);
        if (offset >= b->total_size) offset = mid;
        int64_t t0 = now_usec();
        eb_get_pos(b, &line, &col, offset);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: full keystroke cycle simulation.
 * Insert char + read screen worth of data (simulates insert + redisplay).
 */
static void bench_keystroke_cycle(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "keystroke_cycle_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    int cursor = b->total_size / 2;
    char screen_buf[4096];
    const char *text = "Hello, world! ";
    int textlen = strlen(text);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        char ch = text[iters % textlen];
        int64_t t0 = now_usec();
        /* insert */
        eb_insert(b, cursor, &ch, 1);
        cursor++;
        /* simulate reading visible screen area around cursor */
        int read_start = cursor - 2000;
        if (read_start < 0) read_start = 0;
        eb_read(b, read_start, screen_buf, sizeof screen_buf);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/*
 * Benchmark: bulk insert (paste-like operation).
 * Insert a 4KB block at middle of buffer.
 */
static void bench_bulk_insert(int buf_lines) {
    char name[64];
    snprintf(name, sizeof name, "bulk_insert_4k_%dk_lines", buf_lines / 1000);

    EditBuffer *b = bench_new_buffer(name);
    fill_buffer_lines(b, buf_lines, 72);

    char block[4096];
    memset(block, 'y', sizeof block);

    int64_t start = now_usec();
    int64_t elapsed = 0;
    int64_t iters = 0;
    int64_t min_op = INT64_MAX;

    while (elapsed < MIN_BENCH_USEC) {
        int mid = b->total_size / 2;
        int64_t t0 = now_usec();
        eb_insert(b, mid, block, sizeof block);
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
        /* delete what we inserted to keep buffer size stable */
        eb_delete(b, mid, sizeof block);
    }

    record_result(name, elapsed, iters, min_op);
    bench_free_buffer(&b);
}

/* ---- main ---- */

int main(int argc, char **argv) {
    int sizes[] = { 1000, 10000, 100000 };
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);
    int i;

    bench_init();

    printf("qemacs buffer performance benchmark\n");
    printf("====================================\n");
    printf("Each benchmark runs for at least %d ms.\n", MIN_BENCH_USEC / 1000);
    printf("Buffer sizes: 1k, 10k, 100k lines (73 bytes/line)\n\n");

    for (i = 0; i < nsizes; i++) {
        int n = sizes[i];
        printf("--- %dk lines ---\n", n / 1000);

        bench_insert_char_at_end(n);
        bench_insert_char_at_start(n);
        bench_insert_char_at_middle(n);
        bench_delete_char_at_middle(n);
        bench_typing_simulation(n);
        bench_read_screen(n);
        bench_goto_line(n);
        bench_get_pos(n);
        bench_keystroke_cycle(n);
        bench_bulk_insert(n);
    }

    print_results();

    /* Summary: flag anything over 100 usec/op for single-char operations */
    printf("Latency targets (for responsive keystrokes):\n");
    printf("  Single char insert:  < 10 usec\n");
    printf("  Screen read (4KB):   < 50 usec\n");
    printf("  Keystroke cycle:     < 100 usec  (insert + read)\n");
    printf("  Line navigation:     < 1000 usec\n");
    printf("\n");

    int warnings = 0;
    for (i = 0; i < result_count; i++) {
        double usec_per_op = (double)results[i].total_usec / results[i].iterations;
        if (strstr(results[i].name, "keystroke_cycle") && usec_per_op > 100) {
            printf("  WARNING: %s: %.0f usec/op exceeds 100 usec target\n",
                   results[i].name, usec_per_op);
            warnings++;
        }
        if (strstr(results[i].name, "typing_sim") && usec_per_op > 10) {
            printf("  WARNING: %s: %.0f usec/op exceeds 10 usec target\n",
                   results[i].name, usec_per_op);
            warnings++;
        }
    }
    if (!warnings)
        printf("  All benchmarks within latency targets.\n");
    printf("\n");

    return 0;
}
