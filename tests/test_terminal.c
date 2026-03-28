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
#include <sys/stat.h>
#include <sys/utsname.h>

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
    char resize_path[256];  /* path to resize control file */
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

/* Resize the terminal: write new dimensions to the resize file,
 * then send SIGWINCH to the child process. */
static void session_resize(TestSession *sess, int new_width, int new_height)
{
    FILE *f = fopen(sess->resize_path, "w");
    if (f) {
        fprintf(f, "%dx%d\n", new_width, new_height);
        fclose(f);
    }
    kill(sess->child_pid, SIGWINCH);
    usleep(200000);  /* 200ms for qe to process resize and redraw */
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

    /* Create resize control file path */
    snprintf(sess->resize_path, sizeof(sess->resize_path),
             "/tmp/qe_test_resize_%d.txt", (int)getpid());

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
        setenv("QE_TEST_RESIZE_FILE", sess->resize_path, 1);
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
    unlink(sess->resize_path);

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

/*--- Split window tests ---*/

TEST(terminal, split_horizontal)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* C-x 2: split window horizontally (stacked) */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "2");
    usleep(200000);

    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* After horizontal split, there should be a mode line separator
     * in the middle of the screen (row ~11 for a 24-line screen).
     * The separator uses box-drawing char U+2500 (─) rendered as UTF-8. */
    int found_separator = 0;
    int sep_row = -1;
    for (int row = 3; row < snap->num_lines - 3; row++) {
        /* Mode line starts with ┤ (U+2524) */
        if (strstr(snap->lines[row], "\xe2\x94\xa4") != NULL) {
            found_separator = 1;
            sep_row = row;
            break;
        }
    }
    ASSERT_TRUE(found_separator);

    /* Both panes should show the file content */
    int content_above = 0, content_below = 0;
    for (int row = 0; row < sep_row; row++) {
        if (snap_line_contains(snap, row, "Hello"))
            content_above = 1;
    }
    for (int row = sep_row + 1; row < snap->num_lines - 1; row++) {
        if (snap_line_contains(snap, row, "Hello"))
            content_below = 1;
    }
    ASSERT_TRUE(content_above);
    ASSERT_TRUE(content_below);

    free_snapshot(snap);
    session_stop(&sess);
    session_cleanup(&sess);
}

TEST(terminal, split_vertical)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* C-x 3: split window vertically (side by side) */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "3");
    usleep(200000);

    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* After vertical split, there should be a vertical separator (│, U+2502)
     * roughly in the middle columns of the content rows */
    int found_separator = 0;
    for (int row = 0; row < snap->num_lines - 2; row++) {
        /* Look for │ (U+2502, UTF-8: E2 94 82) in the line */
        if (strstr(snap->lines[row], "\xe2\x94\x82") != NULL) {
            found_separator = 1;
            break;
        }
    }
    ASSERT_TRUE(found_separator);

    /* The file content "Hello" should appear on both sides of the split.
     * Since both panes show the same file, the text should appear twice
     * on the same line (once per pane). */
    int hello_count = 0;
    for (int row = 0; row < snap->num_lines; row++) {
        if (snap_line_contains(snap, row, "Hello"))
            hello_count++;
    }
    /* At least one row should have Hello visible */
    ASSERT_TRUE(hello_count >= 1);

    free_snapshot(snap);
    session_stop(&sess);
    session_cleanup(&sess);
}

/*--- Resize tests ---*/

TEST(terminal, resize_basic)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Verify initial dimensions */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->width, 80);
    ASSERT_EQ(snap->height, 24);
    ASSERT_TRUE(snap_any_line_contains(snap, "Hello"));
    free_snapshot(snap);

    /* Resize to a smaller terminal */
    session_resize(&sess, 60, 20);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->width, 60);
    ASSERT_EQ(snap->height, 20);
    /* Content should still be visible after resize */
    ASSERT_TRUE(snap_any_line_contains(snap, "Hello"));
    free_snapshot(snap);

    /* Resize to a larger terminal */
    session_resize(&sess, 120, 40);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->width, 120);
    ASSERT_EQ(snap->height, 40);
    ASSERT_TRUE(snap_any_line_contains(snap, "Hello"));
    free_snapshot(snap);

    session_stop(&sess);
    session_cleanup(&sess);
}

TEST(terminal, resize_split_horizontal)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Split horizontally first */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "2");
    usleep(200000);

    /* Find the separator row in the initial 80x24 layout */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int initial_sep = -1;
    for (int row = 3; row < snap->num_lines - 3; row++) {
        if (strstr(snap->lines[row], "\xe2\x94\xa4") != NULL) {
            initial_sep = row;
            break;
        }
    }
    ASSERT_TRUE(initial_sep >= 0);
    free_snapshot(snap);

    /* Resize to a taller terminal */
    session_resize(&sess, 80, 40);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->height, 40);

    /* The separator should have moved proportionally to the new height */
    int new_sep = -1;
    for (int row = 3; row < snap->num_lines - 3; row++) {
        if (strstr(snap->lines[row], "\xe2\x94\xa4") != NULL) {
            new_sep = row;
            break;
        }
    }
    ASSERT_TRUE(new_sep >= 0);
    /* Separator should be further down in a taller window */
    ASSERT_TRUE(new_sep > initial_sep);

    /* Both panes should still show content */
    int above = 0, below = 0;
    for (int row = 0; row < new_sep; row++) {
        if (snap_line_contains(snap, row, "Hello")) above = 1;
    }
    for (int row = new_sep + 1; row < snap->num_lines - 1; row++) {
        if (snap_line_contains(snap, row, "Hello")) below = 1;
    }
    ASSERT_TRUE(above);
    ASSERT_TRUE(below);
    free_snapshot(snap);

    session_stop(&sess);
    session_cleanup(&sess);
}

TEST(terminal, resize_split_vertical)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Split vertically */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "3");
    usleep(200000);

    /* Verify vertical separator exists at 80 cols */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->width, 80);

    /* Find the separator column position in a content row */
    int initial_sep_col = -1;
    for (int row = 0; row < snap->num_lines - 2; row++) {
        char *sep = strstr(snap->lines[row], "\xe2\x94\x82");
        if (sep) {
            initial_sep_col = sep - snap->lines[row];
            break;
        }
    }
    ASSERT_TRUE(initial_sep_col >= 0);
    free_snapshot(snap);

    /* Resize to a wider terminal */
    session_resize(&sess, 120, 24);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    ASSERT_EQ(snap->width, 120);

    /* Separator should have moved further right proportionally */
    int new_sep_col = -1;
    for (int row = 0; row < snap->num_lines - 2; row++) {
        char *sep = strstr(snap->lines[row], "\xe2\x94\x82");
        if (sep) {
            new_sep_col = sep - snap->lines[row];
            break;
        }
    }
    ASSERT_TRUE(new_sep_col >= 0);
    /* Separator should be further right in a wider window */
    ASSERT_TRUE(new_sep_col > initial_sep_col);

    /* Content should still be visible */
    ASSERT_TRUE(snap_any_line_contains(snap, "Hello"));
    free_snapshot(snap);

    session_stop(&sess);
    session_cleanup(&sess);
}

/*--- Mode line tests ---*/

TEST(terminal, modeline_shows_filename)
{
    /* Create a file with a distinctive name */
    char filepath[256];
    snprintf(filepath, sizeof(filepath),
             "/tmp/qe_test_modeline_%d.txt", (int)getpid());
    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "modeline test content\n");
        fclose(f);
    }

    TestSession sess;
    if (session_start(&sess, filepath) != 0) {
        unlink(filepath);
        ASSERT_TRUE(0);
        return;
    }

    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* The mode line should be at row height-2 (second to last line,
     * last line is status/minibuffer).
     * It should contain the filename. */
    int modeline_row = snap->num_lines - 2;
    char basename[64];
    snprintf(basename, sizeof(basename), "qe_test_modeline_%d.txt", (int)getpid());
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, basename));

    free_snapshot(snap);
    session_stop(&sess);
    unlink(filepath);
    session_cleanup(&sess);
}

TEST(terminal, modeline_shows_line_column)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Default position should be L1 C1 */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int modeline_row = snap->num_lines - 2;
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "L1"));
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "C1"));
    free_snapshot(snap);

    /* Move cursor down and right to change line/column */
    session_ctrl(&sess, 'e');  /* C-e: end of line */
    usleep(100000);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    modeline_row = snap->num_lines - 2;
    /* Should still be L1 but column should be > 1 */
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "L1"));
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "C14"));  /* "Hello, World!" is 13 chars, cursor at 14 */
    free_snapshot(snap);

    session_stop(&sess);
    session_cleanup(&sess);
}

/*--- Syntax highlighting tests ---*/

TEST(terminal, syntax_highlight_c_file)
{
    /* Create a small C file with keyword, string, and comment */
    char filepath[256];
    snprintf(filepath, sizeof(filepath),
             "/tmp/qe_test_syntax_%d.c", (int)getpid());
    FILE *f = fopen(filepath, "w");
    if (f) {
        fprintf(f, "int main(void) {\n");
        fprintf(f, "    return 0;\n");
        fprintf(f, "}\n");
        fclose(f);
    }

    TestSession sess;
    if (session_start(&sess, filepath) != 0) {
        unlink(filepath);
        ASSERT_TRUE(0);
        return;
    }

    usleep(200000);  /* let syntax highlighting settle */

    session_stop(&sess);

    ASSERT_TRUE(sess.dump_buf != NULL);

    ScreenSnap *snap = get_last_snapshot(sess.dump_buf, sess.dump_len);
    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "int"));
    ASSERT_TRUE(snap_any_line_contains(snap, "return"));

    /* Find the row containing "int main" */
    int keyword_row = -1;
    for (int row = 0; row < snap->num_lines; row++) {
        if (snap_line_contains(snap, row, "int")) {
            keyword_row = row;
            break;
        }
    }
    ASSERT_TRUE(keyword_row >= 0);

    /* Find the column of "int" keyword and "main" identifier */
    int int_col = strstr(snap->lines[keyword_row], "int") - snap->lines[keyword_row];
    /* Find "main" which should be after "int " */
    char *main_ptr = strstr(snap->lines[keyword_row], "main");
    ASSERT_TRUE(main_ptr != NULL);
    int main_col = main_ptr - snap->lines[keyword_row];

    /* Get colors for the keyword "int" and the identifier "main" */
    int int_fg = -1, int_bg = -1;
    int main_fg = -1, main_bg = -1;
    char attrs[16];

    int got_int = find_cell_color(sess.dump_buf, snap->flush_num,
                                   keyword_row, int_col, &int_fg, &int_bg, attrs);
    int got_main = find_cell_color(sess.dump_buf, snap->flush_num,
                                    keyword_row, main_col, &main_fg, &main_bg, attrs);

    /* Both should have color info */
    ASSERT_TRUE(got_int);
    ASSERT_TRUE(got_main);

    /* The keyword "int" should have a DIFFERENT fg color than "main"
     * (syntax highlighting distinguishes keywords from identifiers) */
    ASSERT_NE(int_fg, main_fg);

    free_snapshot(snap);
    unlink(filepath);
    session_cleanup(&sess);
}

/*--- Shell mode tests ---*/

/* Helper: wait for a substring to appear on screen, with timeout.
 * Returns 1 if found, 0 if timed out. */
static int session_wait_for(TestSession *sess, const char *substr, int timeout_ms)
{
    int waited = 0;
    while (waited < timeout_ms) {
        ScreenSnap *snap = session_take_screenshot(sess);
        if (snap) {
            int found = snap_any_line_contains(snap, substr);
            free_snapshot(snap);
            if (found) return 1;
        }
        usleep(100000);  /* 100ms */
        waited += 100;
    }
    return 0;
}

/* Diagnose PTY availability for debugging CI failures */
static void diagnose_pty(void)
{
    struct stat st;
    fprintf(stderr, "PTY diagnostics:\n");
    fprintf(stderr, "  /dev/ptmx exists: %s\n",
            stat("/dev/ptmx", &st) == 0 ? "yes" : "no");
    fprintf(stderr, "  /dev/pts exists: %s\n",
            stat("/dev/pts", &st) == 0 ? "yes" : "no");

    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    fprintf(stderr, "  posix_openpt: fd=%d errno=%d (%s)\n",
            fd, fd < 0 ? errno : 0, fd < 0 ? strerror(errno) : "ok");
    if (fd >= 0) {
        const char *name = ptsname(fd);
        fprintf(stderr, "  ptsname: %s\n", name ? name : "(null)");
        int gpt = grantpt(fd);
        fprintf(stderr, "  grantpt: %d errno=%d\n", gpt, gpt < 0 ? errno : 0);
        int upt = unlockpt(fd);
        fprintf(stderr, "  unlockpt: %d errno=%d\n", upt, upt < 0 ? errno : 0);
        close(fd);
    }

    /* Check kernel version */
    struct utsname uts;
    if (uname(&uts) == 0) {
        fprintf(stderr, "  kernel: %s %s\n", uts.sysname, uts.release);
    }
}

/* Test that M-x shell opens a shell buffer and shows a prompt.
 * Reproduces: "M-x shell stopped working" */
TEST(shell, mx_shell_opens)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Send M-x shell RET to open a shell */
    char esc = 0x1b;
    session_send_keys(&sess, &esc, 1);
    usleep(50000);
    session_type(&sess, "x");
    usleep(100000);
    session_type(&sess, "shell\r");

    /* Wait for shell prompt to appear (look for common prompt chars) */
    int got_prompt = session_wait_for(&sess, "$", 5000);
    if (!got_prompt)
        got_prompt = session_wait_for(&sess, "#", 2000);
    if (!got_prompt)
        got_prompt = session_wait_for(&sess, ">", 2000);

    if (!got_prompt) {
        diagnose_pty();
        ScreenSnap *snap = session_take_screenshot(&sess);
        if (snap) {
            fprintf(stderr, "DEBUG: M-x shell did not open. Screen:\n");
            for (int i = 0; i < snap->num_lines && i < 10; i++)
                fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
            free_snapshot(snap);
        }
    }

    /* The original file content should no longer be visible —
     * we should be in a *shell* buffer now */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int still_has_hello = snap_any_line_contains(snap, "Hello, World!");
    free_snapshot(snap);
    ASSERT_FALSE(still_has_hello);

    /* A shell prompt must have appeared */
    ASSERT_TRUE(got_prompt);

    /* Clean up */
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);
    session_cleanup(&sess);
}

TEST(shell, ctrl_w_kills_word)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Open a shell: M-x shell RET
     * We send ESC (0x1b) then 'x' to get M-x, then type "shell" and RET */
    char esc = 0x1b;
    session_send_keys(&sess, &esc, 1);
    usleep(50000);
    session_type(&sess, "x");
    usleep(100000);
    session_type(&sess, "shell\r");

    /* Wait for shell prompt to appear (look for $ or # or >) */
    int got_prompt = session_wait_for(&sess, "$", 5000);
    if (!got_prompt)
        got_prompt = session_wait_for(&sess, "#", 2000);
    if (!got_prompt)
        got_prompt = session_wait_for(&sess, ">", 2000);

    if (!got_prompt) {
        /* Debug output */
        ScreenSnap *snap = session_take_screenshot(&sess);
        if (snap) {
            fprintf(stderr, "DEBUG: shell prompt not found. Screen:\n");
            for (int i = 0; i < snap->num_lines && i < 10; i++)
                fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
            free_snapshot(snap);
        }
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(got_prompt);
        return;
    }

    /* Type two words with a space between them.
     * Use distinctive marker words to avoid matching prompt text.
     * Type slowly with delays to ensure the shell processes each chunk. */
    session_type(&sess, "echo ALPHA");
    usleep(500000);  /* wait for shell to echo */
    session_type(&sess, " BRAVO");
    usleep(500000);  /* wait for shell to echo */

    /* Verify both words are visible */
    ScreenSnap *snap_before = session_take_screenshot(&sess);
    ASSERT_TRUE(snap_before != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap_before, "ALPHA"));
    ASSERT_TRUE(snap_any_line_contains(snap_before, "BRAVO"));
    free_snapshot(snap_before);

    /* Send C-w (0x17) which bash interprets as unix-word-rubout */
    session_ctrl(&sess, 'w');
    usleep(500000);  /* wait for shell to process and redraw */

    /* Take screenshot and verify BRAVO is gone but ALPHA remains */
    ScreenSnap *snap_after = session_take_screenshot(&sess);
    ASSERT_TRUE(snap_after != NULL);

    /* ALPHA should still be on screen */
    ASSERT_TRUE(snap_any_line_contains(snap_after, "ALPHA"));
    /* BRAVO should be gone (killed by C-w) */
    ASSERT_TRUE(!snap_any_line_contains(snap_after, "BRAVO"));

    free_snapshot(snap_after);

    /* Clean up: send C-c to cancel command, then quit */
    session_ctrl(&sess, 'c');
    usleep(100000);

    session_stop(&sess);
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
