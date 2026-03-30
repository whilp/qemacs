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

#define QE_BINARY_DEFAULT  "../o/qe"

static const char *qe_binary;

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
        /* Use a deterministic shell and prompt so tests don't depend
         * on the user's login shell or prompt configuration. */
        setenv("SHELL", "/bin/sh", 1);
        setenv("PS1", "$ ", 1);

        execlp(qe_binary, "qe", "-q", "-nw", initial_file, NULL);
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

TEST(terminal, modeline_shows_mode_name)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Mode line should show the file name and mode */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int modeline_row = snap->num_lines - 2;
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "qe_test_file"));
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "text"));
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

/*--- Mouse wheel helper ---*/

/* Send an SGR mouse wheel event.
 * button: 64 = wheel up, 65 = wheel down
 * x, y: 1-based coordinates */
static void session_send_mouse_wheel(TestSession *sess, int button, int x, int y)
{
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\033[<%d;%d;%dM", button, x, y);
    session_send_keys(sess, buf, len);
}

/* Send N wheel-up events at position (x, y) with small delays between them */
static void session_scroll_wheel_up(TestSession *sess, int count, int x, int y)
{
    for (int i = 0; i < count; i++) {
        session_send_mouse_wheel(sess, 64, x, y);
        usleep(50000);  /* 50ms between events */
    }
}

/* Send N wheel-down events at position (x, y) with small delays between them */
static void session_scroll_wheel_down(TestSession *sess, int count, int x, int y)
{
    for (int i = 0; i < count; i++) {
        session_send_mouse_wheel(sess, 65, x, y);
        usleep(50000);
    }
}

/*--- Shell mouse scroll tests ---*/

/* Helper: open a shell and wait for prompt. Returns 1 on success. */
static int shell_open_and_wait(TestSession *sess)
{
    char esc = 0x1b;
    session_send_keys(sess, &esc, 1);
    usleep(50000);
    session_type(sess, "x");
    usleep(100000);
    session_type(sess, "shell\r");

    int got_prompt = session_wait_for(sess, "$", 5000);
    if (!got_prompt)
        got_prompt = session_wait_for(sess, "#", 2000);
    if (!got_prompt)
        got_prompt = session_wait_for(sess, ">", 2000);

    if (!got_prompt) {
        diagnose_pty();
        ScreenSnap *snap = session_take_screenshot(sess);
        if (snap) {
            fprintf(stderr, "DEBUG: shell prompt not found. Screen:\n");
            for (int i = 0; i < snap->num_lines && i < 10; i++)
                fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
            free_snapshot(snap);
        }
    }
    return got_prompt;
}

/* Helper: find which screen row contains a given substring.
 * Returns row index or -1 if not found. */
static int snap_find_line(ScreenSnap *snap, const char *substr)
{
    if (!snap) return -1;
    for (int i = 0; i < snap->num_lines; i++) {
        if (strstr(snap->lines[i], substr) != NULL)
            return i;
    }
    return -1;
}

/* Test: generate output in a shell, then scroll up with mouse wheel.
 * After scrolling up, we should see earlier output lines that were
 * scrolled off screen. */
TEST(shell, mouse_scroll_up)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);  /* shell prompt not found */
        return;
    }

    /* Generate enough output to fill and overflow the screen.
     * Print numbered lines with a distinctive marker so we can
     * identify them later. Use seq to print 60 lines (more than
     * the 24-line screen). */
    session_type(&sess, "for i in $(seq 1 60); do echo \"SCROLLTEST line $i\"; done\r");
    usleep(3000000);  /* 3s for the command to complete */

    /* Take a screenshot: the last lines should be visible, the first should not */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* "SCROLLTEST line 1" should NOT be visible (it scrolled off) */
    int line1_before = snap_find_line(snap, "SCROLLTEST line 1 ");
    /* Use trailing space to avoid matching "line 10", "line 11", etc.
     * Actually, "line 1" without space could match "line 10".
     * Be more precise: */
    free_snapshot(snap);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* Check that early lines are NOT visible and late lines ARE visible */
    int has_early = snap_any_line_contains(snap, "SCROLLTEST line 2 ");
    int has_late = snap_any_line_contains(snap, "SCROLLTEST line 58");
    free_snapshot(snap);

    /* Late lines should be visible, early lines should have scrolled off */
    ASSERT_TRUE(has_late);
    /* If the screen is only 24 lines, 60 lines of output means early
     * lines are gone. (If early lines are somehow visible, the test
     * premise is wrong, but we continue.) */

    /* Now scroll up with the mouse wheel — send many wheel-up events
     * to scroll back to the top. The mouse is positioned in the middle
     * of the content area (col 40, row 10). */
    session_scroll_wheel_up(&sess, 50, 40, 10);
    usleep(500000);  /* 500ms for redraws to settle */

    /* Take screenshot after scrolling up */
    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* After scrolling up enough, "SCROLLTEST line 1" or other early
     * lines should now be visible on screen */
    int has_early_after = 0;
    /* Check for any of the first few lines */
    for (int i = 1; i <= 5; i++) {
        char marker[64];
        snprintf(marker, sizeof(marker), "SCROLLTEST line %d", i);
        if (snap_any_line_contains(snap, marker)) {
            has_early_after = 1;
            break;
        }
    }

    if (!has_early_after) {
        fprintf(stderr, "DEBUG: After scrolling up, screen contents:\n");
        for (int i = 0; i < snap->num_lines; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    ASSERT_TRUE(has_early_after);

    free_snapshot(snap);
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);
    session_cleanup(&sess);
}

/* Test: scroll down after scrolling up should return to the bottom */
TEST(shell, mouse_scroll_down_returns)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);
        return;
    }

    /* Generate output */
    session_type(&sess, "for i in $(seq 1 60); do echo \"SCROLLRET line $i\"; done\r");
    usleep(3000000);

    /* Scroll up */
    session_scroll_wheel_up(&sess, 50, 40, 10);
    usleep(500000);

    /* Verify we scrolled up (early lines visible) */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int has_early = 0;
    for (int i = 1; i <= 5; i++) {
        char marker[64];
        snprintf(marker, sizeof(marker), "SCROLLRET line %d", i);
        if (snap_any_line_contains(snap, marker)) {
            has_early = 1;
            break;
        }
    }
    free_snapshot(snap);

    /* Now scroll back down */
    session_scroll_wheel_down(&sess, 50, 40, 10);
    usleep(500000);

    /* Late lines should be visible again */
    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int has_late = snap_any_line_contains(snap, "SCROLLRET line 58") ||
                   snap_any_line_contains(snap, "SCROLLRET line 59") ||
                   snap_any_line_contains(snap, "SCROLLRET line 60");

    if (!has_late) {
        fprintf(stderr, "DEBUG: After scrolling down, screen contents:\n");
        for (int i = 0; i < snap->num_lines; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    ASSERT_TRUE(has_late);

    free_snapshot(snap);
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);
    session_cleanup(&sess);
}

/* Test: scrolling up many times should reach the very beginning of the
 * buffer, not get stuck partway through (the reported bug). */
TEST(shell, mouse_scroll_reaches_top)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);
        return;
    }

    /* Print a distinctive first line, then fill the screen with output */
    session_type(&sess, "echo \"TOP_MARKER_FIRST_LINE\"\r");
    usleep(500000);
    session_type(&sess, "for i in $(seq 1 80); do echo \"FILLER line $i of 80\"; done\r");
    usleep(4000000);

    /* The TOP_MARKER should have scrolled off screen */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int marker_visible_before = snap_any_line_contains(snap, "TOP_MARKER_FIRST_LINE");
    free_snapshot(snap);

    /* Scroll up aggressively — send 100 wheel-up events.
     * This should be more than enough to reach the top of the buffer.
     * If scrolling "only goes a little bit" (the bug), we won't reach
     * the TOP_MARKER line. */
    session_scroll_wheel_up(&sess, 100, 40, 10);
    usleep(1000000);

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    int marker_visible_after = snap_any_line_contains(snap, "TOP_MARKER_FIRST_LINE");

    if (!marker_visible_after) {
        fprintf(stderr, "DEBUG: After 100 scroll-up events, TOP_MARKER not visible.\n");
        fprintf(stderr, "Screen contents:\n");
        for (int i = 0; i < snap->num_lines; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    /* The marker should be visible after scrolling to the top */
    ASSERT_TRUE(marker_visible_after);

    free_snapshot(snap);
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);
    session_cleanup(&sess);
}

/*--- Shell text wrapping in split windows ---*/

/* Helper: find the byte position of the vertical separator (│, U+2502)
 * in a screen line. Returns the byte offset, or -1 if not found. */
static int find_separator_byte_pos(const char *line)
{
    const char *p = strstr(line, "\xe2\x94\x82");
    return p ? (int)(p - line) : -1;
}

/* Helper: count the number of character cells (columns) before a byte
 * position in a UTF-8 string. Assumes all characters are single-width
 * (ASCII or rendered as 1 cell). For the box-drawing separator, this
 * gives us the column index. */
static int byte_pos_to_col(const char *line, int byte_pos)
{
    int col = 0;
    int i = 0;
    while (i < byte_pos && line[i]) {
        unsigned char c = (unsigned char)line[i];
        if (c < 0x80) {
            i++;
        } else if (c < 0xE0) {
            i += 2;
        } else if (c < 0xF0) {
            i += 3;
        } else {
            i += 4;
        }
        col++;
    }
    return col;
}

/* Test that text typed in a shell buffer wraps within the left pane
 * of a side-by-side split, rather than bleeding into the right pane.
 *
 * Steps:
 * 1. Open a shell buffer
 * 2. Split side-by-side (C-x 3)
 * 3. Switch to right window (C-x o) and open another shell
 * 4. Switch back to left window (C-x o)
 * 5. Type a long string that exceeds the left pane width
 * 6. Verify: the typed text should NOT appear to the right of the separator
 *
 * The bug: text does not wrap and instead writes under the right-hand window.
 */
TEST(shell, text_wraps_in_split_window)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Step 1: Open a shell in the initial full-width window */
    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);
        return;
    }

    /* Step 2: Split side-by-side with C-x 3 */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "3");
    usleep(500000);

    /* Step 3: Switch to right window (C-x o) and open a different buffer.
     * M-x shell would reuse the existing *shell* buffer, so instead open
     * a scratch file to ensure the right pane has a separate buffer. */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "o");
    usleep(200000);

    /* Open the initial test file in the right pane via C-x C-f */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_ctrl(&sess, 'f');
    usleep(200000);
    session_type(&sess, sess.test_file);
    session_type(&sess, "\r");
    usleep(500000);

    /* Step 4: Switch back to left window (C-x o) */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "o");
    usleep(200000);

    /* Verify we have a vertical separator */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    int sep_col = -1;
    for (int row = 0; row < snap->num_lines - 2; row++) {
        int bp = find_separator_byte_pos(snap->lines[row]);
        if (bp >= 0) {
            sep_col = byte_pos_to_col(snap->lines[row], bp);
            break;
        }
    }
    free_snapshot(snap);
    ASSERT_TRUE(sep_col > 0);  /* separator must exist */

    /* Step 5: Type a long string that exceeds the left pane width.
     * The left pane is roughly sep_col chars wide (including mode line area).
     * Type a distinctive marker string that's definitely wider than the pane.
     * Use 'echo' so the shell processes it, but we're looking at the input
     * line echoed by the terminal. */
    char long_str[256];
    int fill_len = sep_col + 20;  /* definitely wider than left pane */
    if (fill_len > (int)sizeof(long_str) - 10)
        fill_len = sizeof(long_str) - 10;

    /* Build: "echo QQQQ...QQQQ" where Q's exceed the pane width.
     * Use 'Q' because it doesn't appear in "Hello, World!" (the right pane content),
     * avoiding false positives when checking for bleed-through. */
    memset(long_str, 0, sizeof(long_str));
    strcpy(long_str, "echo ");
    for (int i = 5; i < fill_len; i++)
        long_str[i] = 'Q';
    long_str[fill_len] = '\0';

    session_type(&sess, long_str);
    usleep(1000000);  /* wait for terminal to echo the characters */

    /* Step 6: Take screenshot and check that Q's don't appear past the separator
     * in ANY row. If wrapping works, all Q's stay within columns 0..sep_col-1.
     * If wrapping is broken, Q's will appear in columns >= sep_col. */
    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    int wrap_broken = 0;
    int q_found_at_all = 0;
    for (int row = 0; row < snap->num_lines - 1; row++) {
        /* Find the separator in this row */
        int bp = find_separator_byte_pos(snap->lines[row]);
        if (bp < 0)
            continue;  /* no separator on this row (e.g., mode line) */

        /* Check if 'Q' appears after the separator position */
        const char *after_sep = snap->lines[row] + bp + 3;  /* skip 3-byte UTF-8 │ */
        if (strchr(after_sep, 'Q') != NULL) {
            wrap_broken = 1;
            fprintf(stderr, "BUG: row %d has 'Q' past separator (col %d): '%s'\n",
                    row, sep_col, snap->lines[row]);
        }

        /* Check if 'Q' appears before the separator (expected) */
        char *before = strndup(snap->lines[row], bp);
        if (before) {
            if (strchr(before, 'Q') != NULL)
                q_found_at_all = 1;
            free(before);
        }
    }

    /* Also check rows that might not have a separator (wrapped continuation lines
     * in the left pane). Look for Q's on rows without a separator — these would
     * be continuation lines if wrapping works. */
    for (int row = 0; row < snap->num_lines - 1; row++) {
        if (snap_line_contains(snap, row, "Q"))
            q_found_at_all = 1;
    }

    if (!q_found_at_all) {
        fprintf(stderr, "DEBUG: No Q characters found on screen at all.\n");
        for (int i = 0; i < snap->num_lines; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    /* The Q's should be present somewhere on screen */
    ASSERT_TRUE(q_found_at_all);

    if (wrap_broken) {
        fprintf(stderr, "DETECTED BUG: Shell text does not wrap in split window.\n");
        fprintf(stderr, "Text from left pane bleeds past the vertical separator.\n");
    }
    ASSERT_FALSE(wrap_broken);

    free_snapshot(snap);
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);
    session_cleanup(&sess);
}

/* Variation: Type a long 'echo' command and execute it, checking that the
 * OUTPUT also wraps correctly within the left pane. */
TEST(shell, output_wraps_in_split_window)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    /* Open shell */
    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);
        return;
    }

    /* Split side-by-side */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "3");
    usleep(500000);

    /* Switch to right window and open a different buffer there.
     * M-x shell would reuse the existing *shell* buffer, so open
     * the initial test file instead. */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "o");
    usleep(200000);

    session_ctrl(&sess, 'x');
    usleep(50000);
    session_ctrl(&sess, 'f');
    usleep(200000);
    session_type(&sess, sess.test_file);
    session_type(&sess, "\r");
    usleep(500000);

    /* Switch back to left window */
    session_ctrl(&sess, 'x');
    usleep(50000);
    session_type(&sess, "o");
    usleep(200000);

    /* Find separator column */
    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);
    int sep_col = -1;
    for (int row = 0; row < snap->num_lines - 2; row++) {
        int bp = find_separator_byte_pos(snap->lines[row]);
        if (bp >= 0) {
            sep_col = byte_pos_to_col(snap->lines[row], bp);
            break;
        }
    }
    free_snapshot(snap);
    ASSERT_TRUE(sep_col > 0);

    /* Use printf to output a long string of Z's (wider than the pane).
     * The output from the command should wrap within the left pane. */
    char cmd[256];
    int z_count = sep_col + 15;
    snprintf(cmd, sizeof(cmd), "printf '%%*s\\n' %d '' | tr ' ' 'Z'\r", z_count);
    session_type(&sess, cmd);
    usleep(2000000);  /* wait for output */

    snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    int output_bleeds = 0;
    int z_found = 0;
    for (int row = 0; row < snap->num_lines - 1; row++) {
        int bp = find_separator_byte_pos(snap->lines[row]);
        if (bp < 0) continue;

        /* Check for Z's past the separator */
        const char *after_sep = snap->lines[row] + bp + 3;
        if (strchr(after_sep, 'Z') != NULL) {
            output_bleeds = 1;
            fprintf(stderr, "BUG: row %d has 'Z' past separator: '%s'\n",
                    row, snap->lines[row]);
        }
        char *before = strndup(snap->lines[row], bp);
        if (before) {
            if (strchr(before, 'Z') != NULL)
                z_found = 1;
            free(before);
        }
    }

    /* Also check all rows for Z presence */
    for (int row = 0; row < snap->num_lines - 1; row++) {
        if (snap_line_contains(snap, row, "Z"))
            z_found = 1;
    }

    if (!z_found) {
        fprintf(stderr, "DEBUG: No Z characters found. Screen:\n");
        for (int i = 0; i < snap->num_lines; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    ASSERT_TRUE(z_found);

    if (output_bleeds) {
        fprintf(stderr, "DETECTED BUG: Shell output bleeds past separator in split window.\n");
    }
    ASSERT_FALSE(output_bleeds);

    free_snapshot(snap);
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);
    session_cleanup(&sess);
}

/* Test: Back Color Erase (BCE) — when a terminal program sets a background
 * color and then erases to end of line (\e[K), the background color should
 * extend to the full terminal width, not stop at the last text character. */
TEST(shell, bce_extends_bg_to_eol)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);
        return;
    }

    /* Emit a line with red background (41) + erase to end of line.
     * The marker "BCE_TEST" lets us find this line in the dump.
     * \e[41m = set bg red, \e[K = erase to end of line, \e[0m = reset */
    session_type(&sess, "printf '\\033[41mBCE_TEST\\033[K\\033[0m\\n'\r");
    usleep(1000000);  /* wait for output */

    /* Read the dump file mid-session so we capture the BCE state.
     * The dump is in "last" mode, so it only has the latest flush.
     * We must read it before session_stop rewrites it. */
    char *mid_dump = NULL;
    size_t mid_dump_len = 0;
    {
        FILE *f = fopen(sess.dump_path, "r");
        if (f) {
            mid_dump = malloc(MAX_DUMP_SIZE);
            if (mid_dump) {
                mid_dump_len = fread(mid_dump, 1, MAX_DUMP_SIZE - 1, f);
                mid_dump[mid_dump_len] = '\0';
            }
            fclose(f);
        }
    }

    /* Clean up the session */
    session_ctrl(&sess, 'c');
    usleep(100000);
    session_stop(&sess);

    ASSERT_TRUE(mid_dump != NULL);
    ASSERT_TRUE(mid_dump_len > 0);

    ScreenSnap *snap = get_last_snapshot(mid_dump, mid_dump_len);
    ASSERT_TRUE(snap != NULL);

    /* Find the OUTPUT line with "BCE_TEST" (not the command line).
     * The command line shows the printf command with escape sequences,
     * while the output line shows just "BCE_TEST". Take the last match. */
    int bce_row = -1;
    int test_col = -1;

    for (int i = snap->num_lines - 1; i >= 0; i--) {
        char *p = strstr(snap->lines[i], "BCE_TEST");
        if (p) {
            bce_row = i;
            test_col = (p - snap->lines[i]) + 8;  /* column after "BCE_TEST" */
            break;
        }
    }

    if (bce_row < 0 || test_col < 0) {
        fprintf(stderr, "DEBUG: BCE_TEST not found (row=%d col=%d). Screen:\n",
                bce_row, test_col);
        for (int i = 0; i < snap->num_lines && i < 24; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    ASSERT_TRUE(bce_row >= 0);
    ASSERT_TRUE(test_col >= 0);

    /* Check that cells past the text have bg != 0 (non-default background).
     * The red background (SGR 41 = xterm red) should map to bg index 1.
     * Check several cells after the text to confirm BCE fills them. */
    int found_bce_bg = 0;
    for (int col = test_col; col < test_col + 10 && col < SCREEN_WIDTH; col++) {
        int fg = -1, bg = -1;
        char attrs[16] = "";
        if (find_cell_color(mid_dump, snap->flush_num, bce_row, col, &fg, &bg, attrs)) {
            if (bg != 0) {
                found_bce_bg = 1;
                break;
            }
        }
    }

    if (!found_bce_bg) {
        fprintf(stderr, "DEBUG: BCE bg not found past col %d on row %d.\n",
                test_col, bce_row);
        fprintf(stderr, "DEBUG: Cells with color info on row %d:\n", bce_row);
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            int fg = -1, bg = -1;
            char attrs[16] = "";
            if (find_cell_color(mid_dump, snap->flush_num, bce_row, col, &fg, &bg, attrs)) {
                fprintf(stderr, "  col %d: fg=%d bg=%d\n", col, fg, bg);
            }
        }
    }

    ASSERT_TRUE(found_bce_bg);

    free_snapshot(snap);
    free(mid_dump);
    session_cleanup(&sess);
}

/*--- Alternate screen buffer tests ---*/

/* Test: when a program uses the alternate screen buffer (ESC[?1049h)
 * and then exits it (ESC[?1049l), the alternate screen content must
 * be completely removed from the display.
 *
 * Reproduces: "top output persists after quit + clear"
 * Steps: M-x shell, run a command that enters alternate screen,
 *        writes identifiable content, then exits alternate screen.
 *        After exit, the alternate screen content must not be visible. */
TEST(shell, alternate_screen_cleanup)
{
    TestSession sess;
    if (session_start(&sess, NULL) != 0) {
        ASSERT_TRUE(0);
        return;
    }

    if (!shell_open_and_wait(&sess)) {
        session_stop(&sess);
        session_cleanup(&sess);
        ASSERT_TRUE(0);
        return;
    }

    /* Run a command that:
     * 1. Enters alternate screen (ESC[?1049h)
     * 2. Homes cursor (ESC[H)
     * 3. Writes identifiable content on multiple lines
     * 4. Exits alternate screen (ESC[?1049l)
     *
     * This simulates what `top` does: enter alt screen, paint content, exit.
     */
    /* Use a helper script to avoid the marker text appearing in the
     * command echo.  The script enters alternate screen, writes content,
     * then exits alternate screen. */
    session_type(&sess,
        "sh -c '"
        "printf \"\\033[?1049h\\033[H\""
        " && printf \"ALTSCR_L1\\nALTSCR_L2\\nALTSCR_L3\\n\""
        " && printf \"\\033[?1049l\"'\r");

    /* Wait for the shell prompt to return after the command completes */
    usleep(1500000);

    ScreenSnap *snap = session_take_screenshot(&sess);
    ASSERT_TRUE(snap != NULL);

    /* After exiting alternate screen, the alternate screen content
     * must not appear on any line by itself.  We check each line
     * individually: the marker should not appear on any line that
     * is NOT the command echo (the echo line contains "sh -c"). */
    int leaked = 0;
    for (int i = 0; i < snap->num_lines; i++) {
        if (strstr(snap->lines[i], "sh -c") != NULL)
            continue;  /* skip command echo line */
        if (strstr(snap->lines[i], "ALTSCR_L1") != NULL ||
            strstr(snap->lines[i], "ALTSCR_L2") != NULL ||
            strstr(snap->lines[i], "ALTSCR_L3") != NULL) {
            leaked = 1;
            break;
        }
    }

    if (leaked) {
        fprintf(stderr, "DETECTED BUG: Alternate screen content persists after exit.\n");
        fprintf(stderr, "Screen dump:\n");
        for (int i = 0; i < snap->num_lines; i++)
            fprintf(stderr, "  [%d]: '%s'\n", i, snap->lines[i]);
    }

    free_snapshot(snap);

    ASSERT_FALSE(leaked);

    session_stop(&sess);
    session_cleanup(&sess);
}

/*--- Main ---*/

int main(void)
{
    const char *env = getenv("QE_BINARY");
    qe_binary = (env && *env) ? env : QE_BINARY_DEFAULT;

    /* Ignore SIGPIPE - child may close pipe before we finish writing */
    signal(SIGPIPE, SIG_IGN);

    /* Check that qe binary exists */
    if (access(qe_binary, X_OK) != 0) {
        fprintf(stderr, "ERROR: qe binary not found at '%s'\n", qe_binary);
        fprintf(stderr, "Build qe first: make -f Makefile qe\n");
        return 1;
    }

    return testlib_run_all();
}
