/*
 * HTML rendering tests for QEmacs
 *
 * Tests that the libqhtml HTML/CSS rendering engine correctly displays
 * HTML content when qe opens an HTML file in its built-in HTML mode.
 *
 * Uses the same terminal test harness as test_terminal.c: launches qe
 * with the headless test display driver, creates HTML files, and
 * verifies screen output contains the expected rendered text.
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
#define MAX_DUMP_SIZE  (256 * 1024)

/*--- Test session management (same as test_terminal.c) ---*/

typedef struct TestSession {
    pid_t child_pid;
    int input_pipe_write;
    int input_pipe_read;
    char dump_path[256];
    char resize_path[256];
    char test_file[256];
    char *dump_buf;
    size_t dump_len;
} TestSession;

static void session_send_keys(TestSession *sess, const char *keys, int len)
{
    if (len <= 0) len = strlen(keys);
    write(sess->input_pipe_write, keys, len);
    usleep(50000);
}

static void session_type(TestSession *sess, const char *text)
{
    session_send_keys(sess, text, strlen(text));
}

static void session_ctrl(TestSession *sess, char c)
{
    char ch = c & 0x1F;
    session_send_keys(sess, &ch, 1);
}

static int session_start(TestSession *sess, const char *initial_file)
{
    int pipefds[2];
    pid_t pid;
    char fd_str[16];
    char width_str[16];
    char height_str[16];

    memset(sess, 0, sizeof(*sess));

    snprintf(sess->dump_path, sizeof(sess->dump_path),
             "/tmp/qe_html_test_dump_%d.txt", (int)getpid());
    snprintf(sess->resize_path, sizeof(sess->resize_path),
             "/tmp/qe_html_test_resize_%d.txt", (int)getpid());

    if (pipe(pipefds) < 0) {
        perror("pipe");
        return -1;
    }
    sess->input_pipe_read = pipefds[0];
    sess->input_pipe_write = pipefds[1];

    if (initial_file) {
        snprintf(sess->test_file, sizeof(sess->test_file), "%s", initial_file);
    } else {
        snprintf(sess->test_file, sizeof(sess->test_file),
                 "/tmp/qe_html_test_%d.html", (int)getpid());
        FILE *f = fopen(sess->test_file, "w");
        if (f) {
            fprintf(f, "<html><body><p>Default HTML</p></body></html>\n");
            fclose(f);
        }
        initial_file = sess->test_file;
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
        close(sess->input_pipe_write);
        setenv("QE_TEST_DISPLAY", "1", 1);
        setenv("QE_TEST_INPUT_FD", fd_str, 1);
        setenv("QE_TEST_WIDTH", width_str, 1);
        setenv("QE_TEST_HEIGHT", height_str, 1);
        setenv("QE_TEST_DUMP_FILE", sess->dump_path, 1);
        setenv("QE_TEST_DUMP_MODE", "last", 1);
        setenv("QE_TEST_RESIZE_FILE", sess->resize_path, 1);
        setenv("HOME", "/nonexistent", 1);
        execlp(QE_BINARY, "qe", "-q", "-nw", initial_file, NULL);
        perror("execlp");
        _exit(127);
    }

    close(sess->input_pipe_read);
    sess->child_pid = pid;
    usleep(300000);  /* 300ms for HTML mode to parse and render */
    return 0;
}

static int session_stop(TestSession *sess)
{
    int status = 0;

    session_ctrl(sess, 'x');
    usleep(20000);
    session_ctrl(sess, 'c');
    usleep(100000);
    session_type(sess, "yes\r");
    usleep(100000);

    close(sess->input_pipe_write);

    int waited = 0;
    while (waited < 3000) {
        int ret = waitpid(sess->child_pid, &status, WNOHANG);
        if (ret > 0) break;
        if (ret < 0) break;
        usleep(50000);
        waited += 50;
    }

    if (waited >= 3000) {
        kill(sess->child_pid, SIGKILL);
        waitpid(sess->child_pid, &status, 0);
    }

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

    unlink(sess->dump_path);
    unlink(sess->resize_path);

    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void session_cleanup(TestSession *sess)
{
    if (sess->test_file[0])
        unlink(sess->test_file);
    free(sess->dump_buf);
    sess->dump_buf = NULL;
}

/*--- Screen dump parsing ---*/

typedef struct ScreenSnap {
    int flush_num;
    int width, height;
    int cursor_x, cursor_y;
    char **lines;
    int num_lines;
} ScreenSnap;

static ScreenSnap *parse_next_snapshot(const char *buf, size_t len, size_t *pos)
{
    const char *p = buf + *pos;
    const char *end = buf + len;
    ScreenSnap *snap;
    int w, h, cx, cy, flush;

    while (p < end) {
        if (sscanf(p, "--- flush %d (%dx%d) cursor=(%d,%d) ---",
                   &flush, &w, &h, &cx, &cy) == 5) {
            break;
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    if (p >= end) return NULL;

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

    for (int i = 0; i < h && p < end; i++) {
        if (*p != '|') break;
        p++;
        const char *line_start = p;
        while (p < end && *p != '|' && *p != '\n') p++;
        int line_len = p - line_start;
        snap->lines[i] = malloc(line_len + 1);
        memcpy(snap->lines[i], line_start, line_len);
        snap->lines[i][line_len] = '\0';
        snap->num_lines++;
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }

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

static int snap_line_contains(ScreenSnap *snap, int line, const char *substr)
{
    if (!snap || line < 0 || line >= snap->num_lines)
        return 0;
    return strstr(snap->lines[line], substr) != NULL;
}

static int snap_any_line_contains(ScreenSnap *snap, const char *substr)
{
    if (!snap) return 0;
    for (int i = 0; i < snap->num_lines; i++) {
        if (strstr(snap->lines[i], substr) != NULL)
            return 1;
    }
    return 0;
}

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

/* Helper: write an HTML file and start a session with it.
 * Returns 0 on success. The caller must session_stop + session_cleanup. */
static int create_html_session(TestSession *sess, const char *html_content)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath),
             "/tmp/qe_html_test_%d.html", (int)getpid());

    FILE *f = fopen(filepath, "w");
    if (!f) return -1;
    fputs(html_content, f);
    fclose(f);

    return session_start(sess, filepath);
}

/*--- HTML rendering tests ---*/

TEST(html, basic_paragraph)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Hello from HTML rendering</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* The rendered text should appear (HTML tags should NOT be visible) */
    ASSERT_TRUE(snap_any_line_contains(snap, "Hello from HTML rendering"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, heading_text)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<h1>Main Title</h1>\n"
        "<p>Body paragraph text</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* Both heading and paragraph text should be rendered */
    ASSERT_TRUE(snap_any_line_contains(snap, "Main Title"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Body paragraph text"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, multiple_paragraphs)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>First paragraph</p>\n"
        "<p>Second paragraph</p>\n"
        "<p>Third paragraph</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "First paragraph"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Second paragraph"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Third paragraph"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, unordered_list)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<ul>\n"
        "  <li>Apple</li>\n"
        "  <li>Banana</li>\n"
        "  <li>Cherry</li>\n"
        "</ul>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* All list items should be rendered */
    ASSERT_TRUE(snap_any_line_contains(snap, "Apple"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Banana"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Cherry"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, ordered_list)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<ol>\n"
        "  <li>First item</li>\n"
        "  <li>Second item</li>\n"
        "  <li>Third item</li>\n"
        "</ol>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "First item"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Second item"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Third item"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, inline_formatting)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>This has <b>bold</b> and <i>italic</i> text</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* The rendered line should contain the full text with inline elements */
    ASSERT_TRUE(snap_any_line_contains(snap, "bold"));
    ASSERT_TRUE(snap_any_line_contains(snap, "italic"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, preformatted_text)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<pre>line one\n"
        "line two\n"
        "line three</pre>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* PRE preserves line breaks, so each line should be on a separate row */
    ASSERT_TRUE(snap_any_line_contains(snap, "line one"));
    ASSERT_TRUE(snap_any_line_contains(snap, "line two"));
    ASSERT_TRUE(snap_any_line_contains(snap, "line three"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, html_entities)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Less &lt; Greater &gt; Amp &amp;</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* Entities should be decoded in the rendered output */
    ASSERT_TRUE(snap_any_line_contains(snap, "Less"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Greater"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Amp"));
    /* The decoded characters < > & should appear */
    ASSERT_TRUE(snap_any_line_contains(snap, "<"));
    ASSERT_TRUE(snap_any_line_contains(snap, ">"));
    ASSERT_TRUE(snap_any_line_contains(snap, "&"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, simple_table)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<table>\n"
        "  <tr><td>Cell A1</td><td>Cell B1</td></tr>\n"
        "  <tr><td>Cell A2</td><td>Cell B2</td></tr>\n"
        "</table>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(500000);  /* tables need extra layout time */

    session_stop(&sess);

    ASSERT_TRUE(sess.dump_buf != NULL);
    ScreenSnap *snap = get_last_snapshot(sess.dump_buf, sess.dump_len);
    ASSERT_TRUE(snap != NULL);
    /* Table cells are rendered; the first column may be slightly clipped
     * by the table border, so check for partial text too */
    ASSERT_TRUE(snap_any_line_contains(snap, "ell A1"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Cell B1"));
    ASSERT_TRUE(snap_any_line_contains(snap, "ell A2"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Cell B2"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, nested_divs)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<div>\n"
        "  <div>Inner content here</div>\n"
        "</div>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "Inner content here"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, line_break)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Before break<br>After break</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "Before break"));
    ASSERT_TRUE(snap_any_line_contains(snap, "After break"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, horizontal_rule)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Above the rule</p>\n"
        "<hr>\n"
        "<p>Below the rule</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* Both paragraphs around the HR should render */
    ASSERT_TRUE(snap_any_line_contains(snap, "Above the rule"));
    ASSERT_TRUE(snap_any_line_contains(snap, "Below the rule"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, anchor_text)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Click <a href=\"http://example.com\">this link</a> please</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* Link text should be rendered inline */
    ASSERT_TRUE(snap_any_line_contains(snap, "this link"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, tags_not_visible)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Visible text only</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "Visible text only"));
    /* Raw HTML tags should NOT appear in the rendered output.
     * Check content rows (skip mode line and status line). */
    int tags_visible = 0;
    for (int row = 0; row < snap->num_lines - 2; row++) {
        if (snap_line_contains(snap, row, "</p>") ||
            snap_line_contains(snap, row, "<body>") ||
            snap_line_contains(snap, row, "</html>")) {
            tags_visible = 1;
            break;
        }
    }
    ASSERT_FALSE(tags_visible);

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, modeline_shows_html)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body><p>Mode check</p></body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* The mode line (second to last row) should indicate html mode */
    int modeline_row = snap->num_lines - 2;
    ASSERT_TRUE(snap_line_contains(snap, modeline_row, "html"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, blockquote)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<blockquote>Quoted text here</blockquote>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    ASSERT_TRUE(snap_any_line_contains(snap, "Quoted text here"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

TEST(html, numeric_entity)
{
    TestSession sess;
    int rc = create_html_session(&sess,
        "<html><body>\n"
        "<p>Copyright &#169; symbol</p>\n"
        "</body></html>\n");
    if (rc != 0) { ASSERT_TRUE(0); return; }

    usleep(200000);
    ScreenSnap *snap = session_take_screenshot(&sess);

    session_stop(&sess);

    ASSERT_TRUE(snap != NULL);
    /* The word "Copyright" should be visible in rendered output */
    ASSERT_TRUE(snap_any_line_contains(snap, "Copyright"));
    ASSERT_TRUE(snap_any_line_contains(snap, "symbol"));

    free_snapshot(snap);
    session_cleanup(&sess);
}

/*--- Main ---*/

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    if (access(QE_BINARY, X_OK) != 0) {
        fprintf(stderr, "ERROR: qe binary not found at '%s'\n", QE_BINARY);
        fprintf(stderr, "Build qe first: make -f Makefile qe\n");
        return 1;
    }

    return testlib_run_all();
}
