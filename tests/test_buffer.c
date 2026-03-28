/*
 * Unit tests for buffer.c
 *
 * Tests core buffer operations: insert, delete, read, line navigation,
 * character operations, and edge cases.
 *
 * Build: make test  (from tests/)
 */

#include "testlib.h"
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
struct QETraceDef const qe_trace_defs[] = {};
size_t const qe_trace_defs_count = 0;

/* ---- test setup ---- */

static QEmacsState qs_storage;
static QEmacsState *qs = &qs_storage;

static void test_init(void) {
    memset(qs, 0, sizeof(*qs));
    qs->default_tab_width = 8;
    qs->default_fill_column = 80;
    qs->default_eol_type = EOL_UNIX;
    charset_init(qs);
}

static int buf_id = 0;

static EditBuffer *make_buffer(const char *name) {
    char uname[64];
    snprintf(uname, sizeof uname, "*test-%s-%d*", name, buf_id++);
    return qe_new_buffer(qs, uname, BF_UTF8);
}

/* ---- basic insert/read tests ---- */

TEST(buffer, new_buffer_empty) {
    EditBuffer *b = make_buffer("empty");
    ASSERT_TRUE(b != NULL);
    ASSERT_EQ(b->total_size, 0);
    eb_free(&b);
    ASSERT_TRUE(b == NULL);
}

TEST(buffer, insert_and_read) {
    EditBuffer *b = make_buffer("insert");
    int n;
    char buf[64];

    n = eb_insert(b, 0, "hello", 5);
    ASSERT_EQ(n, 5);
    ASSERT_EQ(b->total_size, 5);

    memset(buf, 0, sizeof buf);
    n = eb_read(b, 0, buf, sizeof buf);
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(buf, "hello", 5);

    eb_free(&b);
}

TEST(buffer, insert_at_offset) {
    EditBuffer *b = make_buffer("offset");
    char buf[64];

    eb_insert(b, 0, "hd", 2);       /* "hd" */
    eb_insert(b, 1, "ello worl", 9); /* "hello world" */
    ASSERT_EQ(b->total_size, 11);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "hello world", 11);

    eb_free(&b);
}

TEST(buffer, insert_at_end) {
    EditBuffer *b = make_buffer("end");
    char buf[64];

    eb_insert(b, 0, "foo", 3);
    eb_insert(b, b->total_size, "bar", 3);
    ASSERT_EQ(b->total_size, 6);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "foobar", 6);

    eb_free(&b);
}

/* ---- delete tests ---- */

TEST(buffer, delete_middle) {
    EditBuffer *b = make_buffer("del");
    char buf[64];

    eb_insert(b, 0, "hello world", 11);
    eb_delete(b, 5, 1); /* remove space -> "helloworld" */
    ASSERT_EQ(b->total_size, 10);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "helloworld", 10);

    eb_free(&b);
}

TEST(buffer, delete_beginning) {
    EditBuffer *b = make_buffer("delbeg");
    char buf[64];

    eb_insert(b, 0, "abcdef", 6);
    eb_delete(b, 0, 3); /* "def" */
    ASSERT_EQ(b->total_size, 3);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "def", 3);

    eb_free(&b);
}

TEST(buffer, delete_all) {
    EditBuffer *b = make_buffer("delall");

    eb_insert(b, 0, "test", 4);
    eb_delete(b, 0, 4);
    ASSERT_EQ(b->total_size, 0);

    eb_free(&b);
}

/* ---- read edge cases ---- */

TEST(buffer, read_partial) {
    EditBuffer *b = make_buffer("partial");
    char buf[3];
    int n;

    eb_insert(b, 0, "abcdef", 6);

    memset(buf, 0, sizeof buf);
    n = eb_read(b, 2, buf, 3); /* read "cde" */
    ASSERT_EQ(n, 3);
    ASSERT_MEMEQ(buf, "cde", 3);

    eb_free(&b);
}

TEST(buffer, read_past_end) {
    EditBuffer *b = make_buffer("pastend");
    char buf[64];
    int n;

    eb_insert(b, 0, "abc", 3);
    n = eb_read(b, 1, buf, 64); /* request more than available */
    ASSERT_EQ(n, 2); /* should clip to "bc" */

    eb_free(&b);
}

TEST(buffer, read_one_byte) {
    EditBuffer *b = make_buffer("onebyte");

    eb_insert(b, 0, "A", 1);
    ASSERT_EQ(eb_read_one_byte(b, 0), 'A');
    ASSERT_EQ(eb_read_one_byte(b, 1), -1); /* past end */

    eb_free(&b);
}

/* ---- line navigation ---- */

TEST(buffer, goto_bol_eol) {
    EditBuffer *b = make_buffer("bol");

    eb_insert(b, 0, "line1\nline2\nline3", 17);

    /* offset 8 is in "line2" */
    ASSERT_EQ(eb_goto_bol(b, 8), 6);   /* start of "line2" */
    ASSERT_EQ(eb_goto_eol(b, 8), 11);  /* end of "line2" */

    /* offset 0 is at start */
    ASSERT_EQ(eb_goto_bol(b, 0), 0);
    ASSERT_EQ(eb_goto_eol(b, 0), 5);

    eb_free(&b);
}

TEST(buffer, next_prev_line) {
    EditBuffer *b = make_buffer("lines");

    eb_insert(b, 0, "aaa\nbbb\nccc\n", 12);

    /* from start of line 1 (offset 0) */
    ASSERT_EQ(eb_next_line(b, 0), 4);  /* start of "bbb" */
    ASSERT_EQ(eb_next_line(b, 4), 8);  /* start of "ccc" */

    /* prev_line from start of "ccc" */
    ASSERT_EQ(eb_prev_line(b, 8), 4);  /* start of "bbb" */
    ASSERT_EQ(eb_prev_line(b, 4), 0);  /* start of "aaa" */
    ASSERT_EQ(eb_prev_line(b, 0), 0);  /* already at start, stays */

    eb_free(&b);
}

/* ---- character-level operations ---- */

TEST(buffer, insert_char32_ascii) {
    EditBuffer *b = make_buffer("char32");
    char buf[16];

    eb_insert_char32(b, 0, 'H');
    eb_insert_char32(b, 1, 'i');
    ASSERT_EQ(b->total_size, 2);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "Hi", 2);

    eb_free(&b);
}

TEST(buffer, insert_str) {
    EditBuffer *b = make_buffer("str");
    char buf[32];

    eb_insert_str(b, 0, "hello");
    ASSERT_EQ(b->total_size, 5);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "hello", 5);

    eb_free(&b);
}

TEST(buffer, nextc_prevc) {
    EditBuffer *b = make_buffer("nextc");
    int next, prev;
    char32_t c;

    eb_insert(b, 0, "abc", 3);

    c = eb_nextc(b, 0, &next);
    ASSERT_EQ(c, 'a');
    ASSERT_EQ(next, 1);

    c = eb_nextc(b, 1, &next);
    ASSERT_EQ(c, 'b');
    ASSERT_EQ(next, 2);

    c = eb_prevc(b, 3, &prev);
    ASSERT_EQ(c, 'c');
    ASSERT_EQ(prev, 2);

    c = eb_prevc(b, 1, &prev);
    ASSERT_EQ(c, 'a');
    ASSERT_EQ(prev, 0);

    eb_free(&b);
}

/* ---- replace and write ---- */

TEST(buffer, replace) {
    EditBuffer *b = make_buffer("replace");
    char buf[32];

    eb_insert(b, 0, "hello world", 11);
    /* replace "world" with "there" */
    eb_replace(b, 6, 5, "there", 5);
    ASSERT_EQ(b->total_size, 11);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "hello there", 11);

    eb_free(&b);
}

TEST(buffer, replace_different_size) {
    EditBuffer *b = make_buffer("replsz");
    char buf[32];

    eb_insert(b, 0, "abc", 3);
    /* replace "b" with "xyz" (1 -> 3) */
    eb_replace(b, 1, 1, "xyz", 3);
    ASSERT_EQ(b->total_size, 5);

    memset(buf, 0, sizeof buf);
    eb_read(b, 0, buf, sizeof buf);
    ASSERT_MEMEQ(buf, "axyzc", 5);

    eb_free(&b);
}

/* ---- large buffer operations ---- */

TEST(buffer, large_insert) {
    EditBuffer *b = make_buffer("large");
    char line[82];
    int i;

    memset(line, 'A', 80);
    line[80] = '\n';
    line[81] = '\0';

    /* insert 1000 lines */
    for (i = 0; i < 1000; i++) {
        eb_insert(b, b->total_size, line, 81);
    }
    ASSERT_EQ(b->total_size, 81000);

    /* verify first and last bytes */
    ASSERT_EQ(eb_read_one_byte(b, 0), 'A');
    ASSERT_EQ(eb_read_one_byte(b, 80), '\n');
    ASSERT_EQ(eb_read_one_byte(b, 80999), '\n');

    eb_free(&b);
}

TEST(buffer, delete_in_large_buffer) {
    EditBuffer *b = make_buffer("lgdel");
    char line[82];
    int i;

    memset(line, 'B', 80);
    line[80] = '\n';
    line[81] = '\0';

    for (i = 0; i < 100; i++) {
        eb_insert(b, b->total_size, line, 81);
    }
    ASSERT_EQ(b->total_size, 8100);

    /* delete middle chunk */
    eb_delete(b, 4050, 810); /* delete 10 lines from middle */
    ASSERT_EQ(b->total_size, 7290);

    eb_free(&b);
}

/* ---- UTF-8 operations ---- */

TEST(buffer, utf8_insert_and_read) {
    EditBuffer *b = make_buffer("utf8");
    char buf[32];
    int n;

    /* Insert multi-byte UTF-8: U+00E9 = C3 A9 (e-acute) */
    eb_insert_utf8_buf(b, 0, "caf\xc3\xa9", 5);
    ASSERT_EQ(b->total_size, 5);

    memset(buf, 0, sizeof buf);
    n = eb_read(b, 0, buf, sizeof buf);
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(buf, "caf\xc3\xa9", 5);

    eb_free(&b);
}

/* ---- clear test ---- */

TEST(buffer, clear) {
    EditBuffer *b = make_buffer("clear");

    eb_insert(b, 0, "some content here", 17);
    ASSERT_EQ(b->total_size, 17);

    eb_clear(b);
    ASSERT_EQ(b->total_size, 0);

    eb_free(&b);
}

/* ---- get_line_length ---- */

TEST(buffer, get_line_length) {
    EditBuffer *b = make_buffer("linelen");
    int next_offset;

    eb_insert(b, 0, "short\na longer line\n\n", 20);

    /* first line: "short" = 5 chars */
    ASSERT_EQ(eb_get_line_length(b, 0, &next_offset), 5);
    ASSERT_EQ(next_offset, 6); /* past \n */

    /* second line: "a longer line" = 13 chars */
    ASSERT_EQ(eb_get_line_length(b, 6, &next_offset), 13);
    ASSERT_EQ(next_offset, 20); /* past \n */

    /* third line: empty */
    ASSERT_EQ(eb_get_line_length(b, 20, &next_offset), 0);

    eb_free(&b);
}

int main(void) {
    test_init();
    return testlib_run_all();
}
