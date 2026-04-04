/*
 * bench_kill_region.c — benchmark for shell-backward-kill-region latency.
 *
 * ROOT CAUSE
 * ----------
 * shell-backward-kill-region (C-w) calls do_kill_region() in non-interactive
 * mode, which reaches shell_delete_bytes().  shell_delete_bytes() calls
 * eb_get_char_offset() THREE times — once each for start, cur_offset, and end.
 *
 * eb_get_char_offset() scans from page 0 to the target offset, summing
 * per-page character counts.  For a buffer with 10 000 lines of Claude Code
 * output (~500 KB, ~125 pages of 4 KB), every C-w triggers three full-buffer
 * scans.  At 50 000 lines (~2.5 MB, ~625 pages) the command takes several
 * milliseconds — clearly perceptible.
 *
 * THE FIX
 * -------
 * start, cur_offset, and end are all >= cur_prompt.  Walking forward from
 * cur_prompt with eb_nextc is O(input_line_length), typically < 200 chars
 * regardless of scrollback depth.
 *
 * BENCHMARKS
 * ----------
 * 1. slow_path  — eb_get_char_offset(b, cur_offset)  [called 3× per C-w]
 * 2. fast_path  — eb_count_chars_fwd(b, cur_prompt, cur_offset) [the fix]
 * 3. full_kill  — models the complete shell_delete_bytes hot-path
 *                 (3 × slow vs 3 × fast at several scrollback depths)
 *
 * Build: make -C tests bench
 * Run:   ./o/tests/bench_kill_region
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

#define MIN_BENCH_USEC  150000   /* run each bench for at least 150 ms */

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
                   int64_t iters, int64_t min_usec)
{
    if (result_count < MAX_RESULTS) {
        results[result_count].name       = strdup(name);
        results[result_count].total_usec = total_usec;
        results[result_count].iterations = iters;
        results[result_count].min_usec   = min_usec;
        result_count++;
    }
}

static void print_results(void) {
    int i;
    printf("\n%-58s %10s %10s %10s\n",
           "Benchmark", "ops/sec", "usec/op", "min_usec");
    printf("%-58s %10s %10s %10s\n",
           "---------", "-------", "-------", "--------");
    for (i = 0; i < result_count; i++) {
        BenchResult *r = &results[i];
        double uop = (double)r->total_usec / r->iterations;
        double ops = r->iterations * 1e6 / r->total_usec;
        printf("%-58s %10.0f %10.2f %10lld\n",
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
    snprintf(name, sizeof name, "*bench-kr-%d*", buf_id++);
    return qe_new_buffer(qs, name, BF_UTF8);
}

/* Append n lines of Claude-Code-like output */
static void append_claude_output(EditBuffer *b, int n) {
    static const char *lines[] = {
        "\033[1;32m✓\033[0m Writing unit tests for buffer module\n",
        "    fn test_insert_large() {\033[0m\n",
        "        let mut buf = Buffer::new();\n",
        "        for i in 0..10_000 { buf.push(i as u8); }\n",
        "        assert_eq!(buf.len(), 10_000);\n",
        "\033[2m// benchmark: 1.23 ms per 1k lines\033[0m\n",
        "\033[33mWarning:\033[0m large file detected (>1 MB)\n",
        "│ \033[36minfo\033[0m Processing chunk 042/128 ...\n",
    };
    int ntpl = (int)(sizeof(lines) / sizeof(lines[0]));
    int i;
    for (i = 0; i < n; i++) {
        const char *t = lines[i % ntpl];
        eb_insert(b, b->total_size, t, strlen(t));
    }
}

/* The fast replacement for eb_get_char_offset in shell_delete_bytes.
 * Counts characters from 'from' to 'to' by walking forward with eb_nextc.
 * O(chars in range), not O(total buffer pages). */
static int eb_count_chars_fwd(EditBuffer *b, int from, int to)
{
    int count = 0, next;
    while (from < to) {
        eb_nextc(b, from, &next);
        count++;
        from = next;
    }
    return count;
}

/*
 * Benchmark setup helper.
 *
 * Builds a buffer that looks like a shell buffer after heavy Claude Code use:
 *   [scrollback_lines of Claude output]  [user input of input_len chars]
 *
 * Returns the buffer; sets *cur_prompt_out to the byte offset of the start
 * of user input (end of the last prompt), and *cur_offset_out to the end.
 */
static EditBuffer *make_shell_buf(int scrollback_lines, int input_len,
                                  int *cur_prompt_out, int *cur_offset_out)
{
    EditBuffer *b = new_buf();
    append_claude_output(b, scrollback_lines);

    /* Simulate a prompt line */
    const char *prompt = "user@host:~$ ";
    eb_insert(b, b->total_size, prompt, strlen(prompt));

    *cur_prompt_out = b->total_size;   /* end of prompt = start of user input */

    /* User has typed input_len ASCII characters */
    char input[256];
    if (input_len > (int)sizeof(input)) input_len = (int)sizeof(input);
    memset(input, 'x', input_len);
    eb_insert(b, b->total_size, input, input_len);

    *cur_offset_out = b->total_size;   /* shell cursor at end of input */
    return b;
}

/* =========================================================
 * Benchmark 1: SLOW — eb_get_char_offset(b, cur_offset)
 *
 * This is what shell_delete_bytes calls today.  It scans from
 * page 0 to cur_offset, which is near the end of the buffer.
 * Called three times per C-w.
 * ========================================================= */
static void bench_slow_get_char_offset(int scrollback_lines, int input_len)
{
    char name[96];
    snprintf(name, sizeof name,
             "SLOW eb_get_char_offset   %5dk scrollback, %3d-char input",
             scrollback_lines / 1000, input_len);

    int cur_prompt, cur_offset;
    EditBuffer *b = make_shell_buf(scrollback_lines, input_len,
                                   &cur_prompt, &cur_offset);
    (void)cur_prompt;

    int64_t start = now_usec(), elapsed = 0, iters = 0, min_op = INT64_MAX;
    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        /* Three calls per C-w: start_char, cur_char, end_char */
        volatile int a = eb_get_char_offset(b, cur_prompt);
        volatile int c = eb_get_char_offset(b, cur_offset);
        volatile int e2 = eb_get_char_offset(b, cur_offset);
        (void)a; (void)c; (void)e2;
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record(name, elapsed, iters, min_op);
    eb_free(&b);
}

/* =========================================================
 * Benchmark 2: FAST — eb_count_chars_fwd(b, cur_prompt, cur_offset)
 *
 * The fix: walk forward from cur_prompt (start of user input).
 * All three targets are within the current input line, so the
 * walk is O(input_line_length), not O(total scrollback pages).
 * ========================================================= */
static void bench_fast_count_chars_fwd(int scrollback_lines, int input_len)
{
    char name[96];
    snprintf(name, sizeof name,
             "FAST eb_count_chars_fwd   %5dk scrollback, %3d-char input",
             scrollback_lines / 1000, input_len);

    int cur_prompt, cur_offset;
    EditBuffer *b = make_shell_buf(scrollback_lines, input_len,
                                   &cur_prompt, &cur_offset);

    int64_t start = now_usec(), elapsed = 0, iters = 0, min_op = INT64_MAX;
    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        /* Three calls per C-w: start_char (0), cur_char, end_char */
        volatile int a = eb_count_chars_fwd(b, cur_prompt, cur_prompt); /* 0 */
        volatile int c = eb_count_chars_fwd(b, cur_prompt, cur_offset);
        volatile int e2 = eb_count_chars_fwd(b, cur_prompt, cur_offset);
        (void)a; (void)c; (void)e2;
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - start;
    }

    record(name, elapsed, iters, min_op);
    eb_free(&b);
}

/* =========================================================
 * Benchmark 3: full kill-region simulation.
 *
 * Models the complete shell_delete_bytes hot-path as triggered
 * by do_kill_region():
 *   SLOW: three eb_get_char_offset calls (current code)
 *   FAST: three eb_count_chars_fwd calls (the fix)
 *
 * Reports latency in usec — directly answers "how slow is C-w?"
 * ========================================================= */
static void bench_kill_slow(int scrollback_lines)
{
    char name[96];
    snprintf(name, sizeof name,
             "kill_slow (3x eb_get_char_offset)  %5dk lines",
             scrollback_lines / 1000);

    int cur_prompt, cur_offset;
    EditBuffer *b = make_shell_buf(scrollback_lines, 50,
                                   &cur_prompt, &cur_offset);

    /* Simulate: kill from cur_prompt to cur_offset */
    int start = cur_prompt;
    int end   = cur_offset;

    int64_t t_start = now_usec(), elapsed = 0, iters = 0, min_op = INT64_MAX;
    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        volatile int sc = eb_get_char_offset(b, start);
        volatile int cc = eb_get_char_offset(b, cur_offset);
        volatile int ec = eb_get_char_offset(b, end);
        (void)sc; (void)cc; (void)ec;
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - t_start;
    }

    record(name, elapsed, iters, min_op);
    eb_free(&b);
}

static void bench_kill_fast(int scrollback_lines)
{
    char name[96];
    snprintf(name, sizeof name,
             "kill_FIXED (3x count_chars_fwd)    %5dk lines",
             scrollback_lines / 1000);

    int cur_prompt, cur_offset;
    EditBuffer *b = make_shell_buf(scrollback_lines, 50,
                                   &cur_prompt, &cur_offset);

    int start = cur_prompt;
    int end   = cur_offset;

    int64_t t_start = now_usec(), elapsed = 0, iters = 0, min_op = INT64_MAX;
    while (elapsed < MIN_BENCH_USEC) {
        int64_t t0 = now_usec();
        /* start_char is always 0 (start == cur_prompt after adjustment) */
        volatile int sc = eb_count_chars_fwd(b, start, start);   /* 0 */
        volatile int cc = eb_count_chars_fwd(b, start, cur_offset);
        volatile int ec = eb_count_chars_fwd(b, start, end);
        (void)sc; (void)cc; (void)ec;
        int64_t dt = now_usec() - t0;
        if (dt < min_op) min_op = dt;
        iters++;
        elapsed = now_usec() - t_start;
    }

    record(name, elapsed, iters, min_op);
    eb_free(&b);
}

/* ---- main ---- */

int main(void) {
    bench_init();

    printf("shell-backward-kill-region latency benchmark\n");
    printf("============================================\n");
    printf(
        "Models do_kill_region() -> shell_delete_bytes().\n"
        "shell_delete_bytes calls eb_get_char_offset() three times;\n"
        "each call scans from page 0, making C-w O(scrollback_size).\n"
        "\n"
        "Fix: walk forward from cur_prompt with eb_count_chars_fwd().\n"
        "All targets (start/cur_offset/end) lie in the current input line,\n"
        "so the walk is O(input_line_length) regardless of scrollback depth.\n"
        "\n"
    );

    /* --- 1. Per-call cost: slow vs fast as scrollback grows --- */
    printf("=== 1. Per-call cost: SLOW (eb_get_char_offset x3) vs FAST (count_chars_fwd x3) ===\n\n");

    int depths[] = { 1000, 5000, 10000, 50000 };
    int nd = (int)(sizeof(depths) / sizeof(depths[0]));
    int i;
    for (i = 0; i < nd; i++) {
        bench_slow_get_char_offset(depths[i], 50);
        bench_fast_count_chars_fwd(depths[i], 50);
        printf("\n");
    }

    /* --- 2. Effect of input length on the fast path --- */
    printf("=== 2. Fast path cost vs input line length (10k scrollback) ===\n\n");
    int ilens[] = { 10, 50, 200, 500 };
    int ni = (int)(sizeof(ilens) / sizeof(ilens[0]));
    for (i = 0; i < ni; i++) {
        bench_fast_count_chars_fwd(10000, ilens[i]);
    }
    printf("\n");

    /* --- 3. Full kill-region simulation: BEFORE vs AFTER fix --- */
    printf("=== 3. Full kill-region hot-path: BEFORE vs AFTER fix ===\n\n");
    for (i = 0; i < nd; i++) {
        bench_kill_slow(depths[i]);
        bench_kill_fast(depths[i]);
        printf("\n");
    }

    print_results();

    /* Print speedup ratios */
    printf("Speedup summary (SLOW usec/op / FAST usec/op):\n");
    {
        double slow_v[4] = {-1,-1,-1,-1}, fast_v[4] = {-1,-1,-1,-1};
        int j;
        for (j = 0; j < result_count; j++) {
            double uop = (double)results[j].total_usec / results[j].iterations;
            const char *n = results[j].name;
            for (i = 0; i < nd; i++) {
                char tag[32];
                snprintf(tag, sizeof tag, "%5dk", depths[i] / 1000);
                if (strstr(n, tag)) {
                    if (strstr(n, "SLOW eb_get_char_offset"))  slow_v[i] = uop;
                    if (strstr(n, "FAST eb_count_chars_fwd"))  fast_v[i] = uop;
                }
            }
        }
        for (i = 0; i < nd; i++) {
            printf("  %5dk lines: ", depths[i] / 1000);
            if (slow_v[i] > 0 && fast_v[i] > 0)
                printf("%.0fx  (%.2f us -> %.2f us)\n",
                       slow_v[i] / fast_v[i], slow_v[i], fast_v[i]);
            else
                printf("n/a\n");
        }
    }
    printf("\n");

    return 0;
}
