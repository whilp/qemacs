/*
 * Terminal testing framework for QEmacs
 *
 * Launches qe with the headless test display driver, sends keystroke
 * sequences through a pipe, and captures screen dumps (with colors)
 * for verification.
 *
 * Usage:
 *   ./test_terminal
 *
 * The test harness:
 * 1. Creates a pipe for sending keystrokes to qe
 * 2. Sets QE_TEST_DISPLAY=1 to activate the headless driver
 * 3. Runs qe as a child process
 * 4. Sends keystrokes, waits for processing, reads screen dumps
 * 5. Compares screen contents against expected values
 *
 * Copyright (c) 2026 Contributors.
 * MIT License (same as QEmacs).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "testlib.h"

/*--- Configuration ---*/

#ifndef QE_BINARY
#define QE_BINARY  "../qe"
#endif

#define SCREEN_WIDTH   80
#define SCREEN_HEIGHT  24
#define MAX_DUMP_SIZE  (256 * 1024)  /* 256 KB max dump file */

/*--- Test session management ---*/

typedef struct TestSession {
    pid_t child_pid;
    int input_pipe_write;   /* write end: we send keystrokes here */
    int input_pipe_read;    /* read end: child reads from here */
    char dump_path[256];    /* path to screen dump file */
    char test_file[256];    /* path to temp file being edited */
    char *dump_buf;         /* contents of dump file after session ends */
    size_t dump_len;
} TestSession;

/* Send raw bytes as keystrokes to qe */
static void session_send_keys(TestSession *sess, const char *keys, int len)
{
    if (len <= 0) len = strlen(keys);
    write(sess->input_pipe_write, keys, len);
    /* Small delay to let qe process the input and redraw */
    usleep(50000);  /* 50ms */
}

/* Send a string to be typed literally (each byte as a keystroke) */
static void session_type(TestSession *sess, const char *text)
{
    session_send_keys(sess, text, strlen(text));
}

/* Send a control character: session_ctrl(sess, 'x') sends C-x */
static void session_ctrl(TestSession *sess, char c)
{
    char ch = c & 0x1F;
    session_send_keys(sess, &ch, 1);
}

/* Start a qe session with the test display driver.
 * If initial_file is non-NULL, qe opens that file.
 * Returns 0 on success. */
static int session_start(TestSession *sess, const char *initial_file)
{
    int pipefds[2];
    pid_t pid;
    char fd_str[16];
    char width_str[16];
    char height_str[16];

    memset(sess, 0, sizeof(*sess));

    /* Create temp file for screen dumps */
    snprintf(sess->dump_path, sizeof(sess->dump_path),
             "/tmp/qe_test_dump_%d.txt", (int)getpid());

    /* Create pipe for keystroke input */
    if (pipe(pipefds) < 0) {
        perror("pipe");
        return -1;
    }
    sess->input_pipe_read = pipefds[0];
    sess->input_pipe_write = pipefds[1];

    /* If no initial file, create a temp file */
    if (!initial_file) {
        snprintf(sess->test_file, sizeof(sess->test_file),
                 "/tmp/qe_test_file_%d.txt", (int)getpid());
        FILE *f = fopen(sess->test_file, "w");
        if (f) {
            fprintf(f, "Hello, World!\n");
            fclose(f);
        }
        initial_file = sess->test_file;
    } else {
        snprintf(sess->test_file, sizeof(sess->test_file), "%s", initial_file);
    }

    snprintf(fd_str, sizeof(fd_str), "%d", sess->input_pipe_read);
    snprintf(width_str, sizeof(width_str), "%d", SCREEN_WIDTH);
    snprintf(height_str, sizeof(height_str), "%d", SCREEN_HEIGHT);

    pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipefds[0]);
        close(pipefds[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: run qe */
        close(sess->input_pipe_write);

        /* Set environment for test display driver */
        setenv("QE_TEST_DISPLAY", "1", 1);
        setenv("QE_TEST_INPUT_FD", fd_str, 1);
        setenv("QE_TEST_WIDTH", width_str, 1);
        setenv("QE_TEST_HEIGHT", height_str, 1);
        setenv("QE_TEST_DUMP_FILE", sess->dump_path, 1);
        setenv("QE_TEST_DUMP_MODE", "last", 1);
        /* Suppress config file loading */
        setenv("HOME", "/nonexistent", 1);

        execlp(QE_BINARY, "qe", "-q", "-nw", initial_file, NULL);
        perror("execlp");
        _exit(127);
    }

    /* Parent */
    close(sess->input_pipe_read);
    sess->child_pid = pid;

    /* Let qe start up and do its initial render */
    usleep(200000);  /* 200ms */

    return 0;
}

/* Stop the qe session: send C-x C-c to quit, then collect output */
static int session_stop(TestSession *sess)
{
    int status = 0;

    /* Send quit command: C-x C-c, then "yes\r" to confirm unsaved changes */
    session_ctrl(sess, 'x');
    usleep(20000);
    session_ctrl(sess, 'c');
    usleep(100000);
    session_type(sess, "yes\r");
    usleep(100000);

    /* Close write end to signal EOF */
    close(sess->input_pipe_write);

    /* Wait for child with timeout */
    int waited = 0;
    while (waited < 3000) {  /* 3 second timeout */
        int ret = waitpid(sess->child_pid, &status, WNOHANG);
        if (ret > 0) break;
        if (ret < 0) break;
        usleep(50000);
        waited += 50;
    }

    if (waited >= 3000) {
        /* Force kill */
        kill(sess->child_pid, SIGKILL);
        waitpid(sess->child_pid, &status, 0);
    }

    /* Read the dump file */
    sess->dump_buf = NULL;
    sess->dump_len = 0;
    FILE *f = fopen(sess->dump_path, "r");
    if (f) {
        sess->dump_buf = malloc(MAX_DUMP_SIZE);
        if (sess->dump_buf) {
            sess->dump_len = fread(sess->dump_buf, 1, MAX_DUMP_SIZE - 1, f);
            sess->dump_buf[sess->dump_len] = '\0';
        }
        fclose(f);
    }

    /* Clean up temp files */
    unlink(sess->dump_path);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* Clean up session resources */
static void session_cleanup(TestSession *sess)
{
    if (sess->test_file[0])
        unlink(sess->test_file);
    free(sess->dump_buf);
    sess->dump_buf = NULL;
}

/*--- Screen dump parsing ---*/

/* A parsed screen snapshot (one flush) */
typedef struct ScreenSnap {
    int flush_num;
    int width, height;
    int cursor_x, cursor_y;
    char **lines;       /* array of height lines (plain text, null-terminated) */
    int num_lines;
} ScreenSnap;

/* Parse one screen snapshot from the dump buffer starting at *pos.
 * Returns the snapshot or NULL if no more snapshots.
 * Advances *pos past the snapshot. */
static ScreenSnap *parse_next_snapshot(const char *buf, size_t len, size_t *pos)
{
    const char *p = buf + *pos;
    const char *end = buf + len;
    ScreenSnap *snap;
    int w, h, cx, cy, flush;

    /* Find next "--- flush N (WxH) cursor=(X,Y) ---" */
    while (p < end) {
        if (sscanf(p, "--- flush %d (%dx%d) cursor=(%d,%d) ---",
                   &flush, &w, &h, &cx, &cy) == 5) {
            break;
        }
        /* Skip to next line */
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    if (p >= end) return NULL;

    /* Skip past the header line */
    while (p < end && *p != '\n') p++;
    if (p < end) p++;

    snap = calloc(1, sizeof(ScreenSnap));
    snap->flush_num = flush;
    snap->width = w;
    snap->height = h;
    snap->cursor_x = cx;
    snap->cursor_y = cy;
    snap->lines = calloc(h, sizeof(char *));
    snap->num_lines = 0;

    /* Read |...|  lines */
    for (int i = 0; i < h && p < end; i++) {
        if (*p != '|') break;
        p++;  /* skip leading | */
        const char *line_start = p;
        /* Find trailing | */
        while (p < end && *p != '|' && *p != '\n') p++;
        int line_len = p - line_start;
        snap->lines[i] = malloc(line_len + 1);
        memcpy(snap->lines[i], line_start, line_len);
        snap->lines[i][line_len] = '\0';
        snap->num_lines++;
        /* Skip past |, newline */
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    /* Skip to "--- end ---" */
    while (p < end) {
        if (strncmp(p, "--- end ---", 11) == 0) {
            while (p < end && *p != '\n') p++;
            if (p < end) p++;
            break;
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

    *pos = p - buf;
    return snap;
}

static void free_snapshot(ScreenSnap *snap)
{
    if (snap) {
        for (int i = 0; i < snap->num_lines; i++)
            free(snap->lines[i]);
        free(snap->lines);
        free(snap);
    }
}

/* Get the last (most recent) snapshot from the dump */
static ScreenSnap *get_last_snapshot(const char *buf, size_t len)
{
    size_t pos = 0;
    ScreenSnap *last = NULL;
    ScreenSnap *snap;

    while ((snap = parse_next_snapshot(buf, len, &pos)) != NULL) {
        free_snapshot(last);
        last = snap;
    }
    return last;
}

/* Check if a line in a snapshot contains a substring */
static int snap_line_contains(ScreenSnap *snap, int line, const char *substr)
{
    if (!snap || line < 0 || line >= snap->num_lines)
        return 0;
    return strstr(snap->lines[line], substr) != NULL;
}

/* Check if any line in a snapshot contains a substring */
static int snap_any_line_contains(ScreenSnap *snap, const char *substr)
{
    if (!snap) return 0;
    for (int i = 0; i < snap->num_lines; i++) {
        if (strstr(snap->lines[i], substr) != NULL)
            return 1;
    }
    return 0;
}

/* Read the current screen dump file (can be called while qe is running).
 * Returns a snapshot of the latest screen state, or NULL on failure.
 * Caller must free_snapshot() the result. */
static ScreenSnap *session_take_screenshot(TestSession *sess)
{
    FILE *f = fopen(sess->dump_path, "r");
    if (!f) return NULL;

    char *buf = malloc(MAX_DUMP_SIZE);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, MAX_DUMP_SIZE - 1, f);
    buf[n] = '\0';
    fclose(f);

    ScreenSnap *snap = get_last_snapshot(buf, n);
    free(buf);
    return snap;
}

/* Find a color annotation in the dump for a specific cell.
 * Returns 1 if found, fills fg/bg/attrs. */
static int find_cell_color(const char *dump_buf, int last_flush,
                           int row, int col, int *fg, int *bg, char *attrs_buf)
{
    char pattern[64];
    const char *p;

    /* Find the colors section of the last flush */
    snprintf(pattern, sizeof(pattern), "--- flush %d", last_flush);
    p = strstr(dump_buf, pattern);
    if (!p) return 0;

    p = strstr(p, "--- colors ---");
    if (!p) return 0;

    /* Search for the specific cell */
    snprintf(pattern, sizeof(pattern), "  %d,%d:", row, col);
    p = strstr(p, pattern);
    if (!p) return 0;

    /* Check it's before "--- end ---" */
    const char *end_marker = strstr(p, "--- end ---");
    if (!end_marker) return 0;

    /* Parse fg and bg */
    int parsed_fg = -1, parsed_bg = -1;
    char attrs[16] = "";
    if (sscanf(p + strlen(pattern), " ch=U+%*X fg=%d bg=%d", &parsed_fg, &parsed_bg) >= 2) {
        if (fg) *fg = parsed_fg;
        if (bg) *bg = parsed_bg;
        /* Look for attrs= */
        const char *a = strstr(p, "attrs=");
        if (a && a < end_marker) {
            a += 6;
            int i = 0;
            while (*a && *a != '\n' && *a != ' ' && i < 15) {
                attrs[i++] = *a++;
            }
            attrs[i] = '\0';
        }
        if (attrs_buf) strcpy(attrs_buf, attrs);
        return 1;
    }
    return 0;
}

/*--- Tests ---*/

TEST(terminal, startup_shows_filename)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);  /* failed to start */
        return;
    }

    session_stop(&sess);

    ASSERT_TRUE(sess.dump_buf != NULL);
    ASSERT_TRUE(sess.dump_len > 0);

    ScreenSnap *snap = get_last_snapshot(sess.dump_buf, sess.dump_len);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->width, SCREEN_WIDTH);
    ASSERT_EQ(snap->height, SCREEN_HEIGHT);

    /* The file content "Hello, World!" should be visible */
    ASSERT_TRUE(snap_any_line_contains(snap, "Hello, World!"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(terminal, type_text)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Move to end of buffer and type new text.
     * Use \r (carriage return) for Enter key, matching terminal behavior. */
    session_ctrl(&sess, 'e');   /* C-e: end of line */
    usleep(50000);
    session_type(&sess, "\rTest line added by harness");
    usleep(200000);  /* wait for qe to process and flush */

    /* Take screenshot BEFORE quit (avoids quit confirmation contamination) */
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "Test line added by harness"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(terminal, cursor_movement)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Move cursor to beginning of line */
    session_ctrl(&sess, 'a');   /* C-a: beginning of line */
    usleep(100000);

    session_stop(&sess);

    ScreenSnap *snap = get_last_snapshot(sess.dump_buf, sess.dump_len);
    ASSERT_TRUE(snap != NULL);
    /* Cursor should be at column 0 */
    ASSERT_EQ(snap->cursor_x, 0);

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(terminal, screen_dimensions)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    session_stop(&sess);

    ScreenSnap *snap = get_last_snapshot(sess.dump_buf, sess.dump_len);
    ASSERT_TRUE(snap != NULL);

    /* Verify configured dimensions */
    ASSERT_EQ(snap->width, SCREEN_WIDTH);
    ASSERT_EQ(snap->height, SCREEN_HEIGHT);
    ASSERT_EQ(snap->num_lines, SCREEN_HEIGHT);

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(terminal, multiple_flushes)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Type something to trigger additional redraws */
    session_type(&sess, "abc");
    usleep(100000);

    /* Take screenshot and check the flush counter (increments on each render) */
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* flush_num should be > 1 (initial render + redraws after typing) */
    ASSERT_TRUE(snap->flush_num >= 2);

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(terminal, color_annotations_present)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    session_stop(&sess);

    ASSERT_TRUE(sess.dump_buf != NULL);

    /* The dump should contain color annotation sections */
    ASSERT_TRUE(strstr(sess.dump_buf, "--- colors ---") != NULL);

    /* Non-space characters should have color annotations */
    /* The file content has 'H' at some position, it should have a color entry */
    ScreenSnap *snap = get_last_snapshot(sess.dump_buf, sess.dump_len);
    ASSERT_TRUE(snap != NULL);

    /* Find which line has "Hello" */
    int found_color = 0;
    for (int row = 0; row < snap->num_lines; row++) {
        if (snap_line_contains(snap, row, "Hello")) {
            int col = strstr(snap->lines[row], "Hello") - snap->lines[row];
            int fg = -1, bg = -1;
            char attrs[16] = "";
            if (find_cell_color(sess.dump_buf, snap->flush_num,
                                row, col, &fg, &bg, attrs)) {
                found_color = 1;
                /* fg should be a valid color index */
                ASSERT_TRUE(fg >= 0 && fg <= 255);
            }
            break;
        }
    }
    ASSERT_TRUE(found_color);

    free_snapshot(snap);
    session_cleanup(&sess);
}

/*--- Main ---*/

int main(void)
{
    /* Ignore SIGPIPE - child may close pipe before we finish writing */
    signal(SIGPIPE, SIG_IGN);

    /* Check that qe binary exists */
    if (access(QE_BINARY, X_OK) != 0) {
        fprintf(stderr, "ERROR: qe binary not found at '%s'\n", QE_BINARY);
        fprintf(stderr, "Build qe first: make -f Makefile qe\n");
        return 1;
    }

    return testlib_run_all();
}
