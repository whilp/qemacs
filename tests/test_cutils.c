/*
 * Tests for cutils.c - string utilities, UTF-8, and DynBuf
 */

#include "testlib.h"

/* We need config.h for the module under test */
#include "config.h"
#include "cutils.h"

/* ---- pstrcpy ---- */

TEST(pstrcpy, basic) {
    char buf[16];
    pstrcpy(buf, sizeof buf, "hello");
    ASSERT_STREQ(buf, "hello");
}

TEST(pstrcpy, truncation) {
    char buf[4];
    pstrcpy(buf, sizeof buf, "hello world");
    ASSERT_STREQ(buf, "hel");
}

TEST(pstrcpy, size_one) {
    char buf[1];
    pstrcpy(buf, sizeof buf, "hello");
    ASSERT_STREQ(buf, "");
}

TEST(pstrcpy, size_zero) {
    char buf[4] = "abc";
    pstrcpy(buf, 0, "xyz");
    ASSERT_STREQ(buf, "abc");  /* unchanged */
}

TEST(pstrcpy, empty_src) {
    char buf[8] = "old";
    pstrcpy(buf, sizeof buf, "");
    ASSERT_STREQ(buf, "");
}

/* ---- pstrcat ---- */

TEST(pstrcat, basic) {
    char buf[16] = "hello";
    pstrcat(buf, sizeof buf, " world");
    ASSERT_STREQ(buf, "hello world");
}

TEST(pstrcat, truncation) {
    char buf[8] = "hello";
    pstrcat(buf, sizeof buf, " world!!!");
    ASSERT_STREQ(buf, "hello w");
}

TEST(pstrcat, empty_buf) {
    char buf[8] = "";
    pstrcat(buf, sizeof buf, "abc");
    ASSERT_STREQ(buf, "abc");
}

TEST(pstrcat, empty_src) {
    char buf[8] = "abc";
    pstrcat(buf, sizeof buf, "");
    ASSERT_STREQ(buf, "abc");
}

/* ---- pstrncpy ---- */

TEST(pstrncpy, basic) {
    char buf[16];
    pstrncpy(buf, sizeof buf, "hello world", 5);
    ASSERT_STREQ(buf, "hello");
}

TEST(pstrncpy, len_exceeds_size) {
    char buf[4];
    pstrncpy(buf, sizeof buf, "hello", 10);
    ASSERT_STREQ(buf, "hel");
}

TEST(pstrncpy, len_zero) {
    char buf[8] = "old";
    pstrncpy(buf, sizeof buf, "new", 0);
    ASSERT_STREQ(buf, "");
}

/* ---- pstrncat ---- */

TEST(pstrncat, basic) {
    char buf[16] = "hello";
    pstrncat(buf, sizeof buf, " world!!!", 6);
    ASSERT_STREQ(buf, "hello world");
}

/* ---- strstart ---- */

TEST(strstart, match) {
    const char *p = NULL;
    ASSERT_TRUE(strstart("hello world", "hello", &p));
    ASSERT_STREQ(p, " world");
}

TEST(strstart, no_match) {
    const char *p = (const char *)0xDEAD;
    ASSERT_FALSE(strstart("hello", "world", &p));
    /* p should be unchanged on no match */
    ASSERT_EQ((uintptr_t)p, (uintptr_t)0xDEAD);
}

TEST(strstart, empty_prefix) {
    const char *p = NULL;
    ASSERT_TRUE(strstart("hello", "", &p));
    ASSERT_STREQ(p, "hello");
}

TEST(strstart, null_ptr) {
    ASSERT_TRUE(strstart("hello", "hello", NULL));
    ASSERT_FALSE(strstart("hello", "world", NULL));
}

/* ---- strend ---- */

TEST(strend, match) {
    const char *p = NULL;
    ASSERT_TRUE(strend("hello world", "world", &p));
    ASSERT_STREQ(p, "world");
}

TEST(strend, no_match) {
    ASSERT_FALSE(strend("hello", "world", NULL));
}

TEST(strend, empty_suffix) {
    const char *p = NULL;
    ASSERT_TRUE(strend("hello", "", &p));
    ASSERT_STREQ(p, "");
}

TEST(strend, full_match) {
    const char *p = NULL;
    ASSERT_TRUE(strend("hello", "hello", &p));
    ASSERT_STREQ(p, "hello");
}

/* ---- get_basename_offset ---- */

TEST(basename, simple_path) {
    ASSERT_EQ(get_basename_offset("/usr/bin/qe"), 9);
}

TEST(basename, no_slash) {
    ASSERT_EQ(get_basename_offset("file.c"), 0);
}

TEST(basename, trailing_slash) {
    ASSERT_EQ(get_basename_offset("/usr/bin/"), 9);
}

TEST(basename, root) {
    ASSERT_EQ(get_basename_offset("/"), 1);
}

TEST(basename, null_path) {
    ASSERT_EQ(get_basename_offset(NULL), 0);
}

TEST(basename, empty) {
    ASSERT_EQ(get_basename_offset(""), 0);
}

/* ---- get_extension_offset ---- */

TEST(extension, simple) {
    const char *p = "file.c";
    ASSERT_EQ(get_extension_offset(p), 4);
    ASSERT_STREQ(p + get_extension_offset(p), ".c");
}

TEST(extension, no_ext) {
    const char *p = "Makefile";
    /* should point to the NUL terminator */
    ASSERT_EQ(p[get_extension_offset(p)], '\0');
}

TEST(extension, dotfile) {
    /* leading dots are not extensions */
    const char *p = ".gitignore";
    ASSERT_EQ(p[get_extension_offset(p)], '\0');
}

TEST(extension, dotfile_with_ext) {
    const char *p = ".config.bak";
    ASSERT_EQ(get_extension_offset(p), 7);
    ASSERT_STREQ(p + get_extension_offset(p), ".bak");
}

TEST(extension, multiple_dots) {
    const char *p = "archive.tar.gz";
    /* returns last extension */
    ASSERT_STREQ(p + get_extension_offset(p), ".gz");
}

TEST(extension, path_with_ext) {
    const char *p = "/home/user/file.txt";
    ASSERT_STREQ(p + get_extension_offset(p), ".txt");
}

TEST(extension, null) {
    ASSERT_EQ(get_extension_offset(NULL), 0);
}

/* ---- get_dirname ---- */

TEST(dirname, simple) {
    char buf[64];
    get_dirname(buf, sizeof buf, "/usr/bin/qe");
    ASSERT_STREQ(buf, "/usr/bin");
}

TEST(dirname, no_dir) {
    char buf[64];
    get_dirname(buf, sizeof buf, "file.c");
    ASSERT_STREQ(buf, ".");
}

TEST(dirname, root_file) {
    char buf[64];
    get_dirname(buf, sizeof buf, "/file.c");
    ASSERT_STREQ(buf, "/");
}

/* ---- get_relativename ---- */

TEST(relativename, basic) {
    ASSERT_STREQ(get_relativename("/usr/bin/qe", "/usr/bin"), "qe");
}

TEST(relativename, not_relative) {
    ASSERT_STREQ(get_relativename("/other/file", "/usr/bin"), "/other/file");
}

/* ---- unicode_to_utf8 / unicode_from_utf8 ---- */

TEST(utf8, ascii) {
    uint8_t buf[8];
    int len = unicode_to_utf8(buf, 'A');
    ASSERT_EQ(len, 1);
    ASSERT_EQ(buf[0], 'A');
}

TEST(utf8, two_byte) {
    uint8_t buf[8];
    /* U+00E9 = e-acute = 0xC3 0xA9 */
    int len = unicode_to_utf8(buf, 0xE9);
    ASSERT_EQ(len, 2);
    ASSERT_EQ(buf[0], 0xC3);
    ASSERT_EQ(buf[1], 0xA9);
}

TEST(utf8, three_byte) {
    uint8_t buf[8];
    /* U+4E16 = CJK character = 0xE4 0xB8 0x96 */
    int len = unicode_to_utf8(buf, 0x4E16);
    ASSERT_EQ(len, 3);
    ASSERT_EQ(buf[0], 0xE4);
    ASSERT_EQ(buf[1], 0xB8);
    ASSERT_EQ(buf[2], 0x96);
}

TEST(utf8, four_byte) {
    uint8_t buf[8];
    /* U+1F600 = grinning face = 0xF0 0x9F 0x98 0x80 */
    int len = unicode_to_utf8(buf, 0x1F600);
    ASSERT_EQ(len, 4);
    ASSERT_EQ(buf[0], 0xF0);
    ASSERT_EQ(buf[1], 0x9F);
    ASSERT_EQ(buf[2], 0x98);
    ASSERT_EQ(buf[3], 0x80);
}

TEST(utf8, invalid_codepoint) {
    uint8_t buf[8];
    /* >= 0x80000000 returns 0 */
    int len = unicode_to_utf8(buf, 0x80000000);
    ASSERT_EQ(len, 0);
}

TEST(utf8, roundtrip_ascii) {
    uint8_t buf[8];
    const uint8_t *p;
    int len = unicode_to_utf8(buf, 'Z');
    ASSERT_EQ(len, 1);
    p = buf;
    int c = unicode_from_utf8(p, len, &p);
    ASSERT_EQ(c, 'Z');
    ASSERT_EQ(p - buf, 1);
}

TEST(utf8, roundtrip_multibyte) {
    uint8_t buf[8];
    const uint8_t *p;
    unsigned int codepoints[] = { 0xE9, 0x4E16, 0x1F600 };
    int i;
    for (i = 0; i < 3; i++) {
        int len = unicode_to_utf8(buf, codepoints[i]);
        p = buf;
        int c = unicode_from_utf8(p, len, &p);
        ASSERT_EQ((unsigned int)c, codepoints[i]);
        ASSERT_EQ(p - buf, len);
    }
}

TEST(utf8, decode_overlong) {
    /* 0xC0 0x80 is an overlong encoding of U+0000 -- should reject */
    const uint8_t overlong[] = { 0xC0, 0x80 };
    const uint8_t *p = overlong;
    int c = unicode_from_utf8(p, 2, &p);
    ASSERT_EQ(c, -1);
}

TEST(utf8, decode_truncated) {
    /* 0xE4 needs 2 more bytes, but max_len=1 */
    const uint8_t trunc[] = { 0xE4 };
    const uint8_t *p = trunc;
    int c = unicode_from_utf8(p, 1, &p);
    ASSERT_EQ(c, -1);
}

TEST(utf8, decode_invalid_continuation) {
    /* 0xC3 followed by non-continuation byte */
    const uint8_t bad[] = { 0xC3, 0x00 };
    const uint8_t *p = bad;
    int c = unicode_from_utf8(p, 2, &p);
    ASSERT_EQ(c, -1);
}

/* ---- DynBuf ---- */

TEST(dbuf, init_and_free) {
    DynBuf db;
    dbuf_init(&db);
    ASSERT_EQ(db.size, 0);
    ASSERT_FALSE(db.error);
    dbuf_free(&db);
    ASSERT_EQ(db.size, 0);
    ASSERT_EQ(db.buf, NULL);
}

TEST(dbuf, putc) {
    DynBuf db;
    dbuf_init(&db);
    ASSERT_EQ(dbuf_putc(&db, 'A'), 0);
    ASSERT_EQ(dbuf_putc(&db, 'B'), 0);
    ASSERT_EQ(db.size, 2);
    ASSERT_EQ(db.buf[0], 'A');
    ASSERT_EQ(db.buf[1], 'B');
    dbuf_free(&db);
}

TEST(dbuf, putstr) {
    DynBuf db;
    dbuf_init(&db);
    dbuf_putstr(&db, "hello");
    ASSERT_EQ(db.size, 5);
    ASSERT_MEMEQ(db.buf, "hello", 5);
    dbuf_free(&db);
}

TEST(dbuf, printf_short) {
    DynBuf db;
    dbuf_init(&db);
    dbuf_printf(&db, "x=%d", 42);
    ASSERT_STREQ(dbuf_str(&db), "x=42");
    dbuf_free(&db);
}

TEST(dbuf, printf_long) {
    DynBuf db;
    dbuf_init(&db);
    /* Force the >128 byte path in dbuf_printf */
    char long_str[200];
    memset(long_str, 'A', sizeof(long_str) - 1);
    long_str[sizeof(long_str) - 1] = '\0';
    dbuf_printf(&db, "%s", long_str);
    ASSERT_EQ(db.size, sizeof(long_str) - 1);
    ASSERT_EQ(db.buf[0], 'A');
    ASSERT_EQ(db.buf[sizeof(long_str) - 2], 'A');
    dbuf_free(&db);
}

TEST(dbuf, write_at_offset) {
    DynBuf db;
    dbuf_init(&db);
    dbuf_putstr(&db, "hello");
    dbuf_write(&db, 0, (const uint8_t *)"HELL", 4);
    ASSERT_STREQ(dbuf_str(&db), "HELLo");
    dbuf_free(&db);
}

TEST(dbuf, write_extends) {
    DynBuf db;
    dbuf_init(&db);
    dbuf_write(&db, 5, (const uint8_t *)"world", 5);
    ASSERT_EQ(db.size, 10);
    ASSERT_MEMEQ(db.buf + 5, "world", 5);
    dbuf_free(&db);
}

TEST(dbuf, put_self) {
    DynBuf db;
    dbuf_init(&db);
    dbuf_putstr(&db, "abc");
    dbuf_put_self(&db, 0, 3);
    ASSERT_EQ(db.size, 6);
    ASSERT_MEMEQ(db.buf, "abcabc", 6);
    dbuf_free(&db);
}

TEST(dbuf, str_empty) {
    DynBuf db;
    dbuf_init(&db);
    ASSERT_STREQ(dbuf_str(&db), "");
    dbuf_free(&db);
}

TEST(dbuf, growth) {
    DynBuf db;
    int i;
    dbuf_init(&db);
    for (i = 0; i < 10000; i++) {
        ASSERT_EQ(dbuf_putc(&db, 'x'), 0);
    }
    ASSERT_EQ(db.size, 10000);
    ASSERT_FALSE(db.error);
    dbuf_free(&db);
}

/* ---- from_hex (inline in cutils.h) ---- */

TEST(from_hex, digits) {
    ASSERT_EQ(from_hex('0'), 0);
    ASSERT_EQ(from_hex('9'), 9);
    ASSERT_EQ(from_hex('a'), 10);
    ASSERT_EQ(from_hex('f'), 15);
    ASSERT_EQ(from_hex('A'), 10);
    ASSERT_EQ(from_hex('F'), 15);
}

TEST(from_hex, invalid) {
    ASSERT_EQ(from_hex('g'), -1);
    ASSERT_EQ(from_hex(' '), -1);
    ASSERT_EQ(from_hex('\0'), -1);
}

/* ---- clamp_int, min_int, max_int ---- */

TEST(arith, clamp) {
    ASSERT_EQ(clamp_int(5, 0, 10), 5);
    ASSERT_EQ(clamp_int(-5, 0, 10), 0);
    ASSERT_EQ(clamp_int(15, 0, 10), 10);
}

TEST(arith, min_max) {
    ASSERT_EQ(min_int(3, 7), 3);
    ASSERT_EQ(max_int(3, 7), 7);
    ASSERT_EQ(min3_int(5, 3, 7), 3);
    ASSERT_EQ(max3_int(5, 3, 7), 7);
}

/* ---- strequal ---- */

TEST(strequal, match) {
    ASSERT_TRUE(strequal("abc", "abc"));
}

TEST(strequal, no_match) {
    ASSERT_FALSE(strequal("abc", "def"));
}

TEST(strequal, empty) {
    ASSERT_TRUE(strequal("", ""));
    ASSERT_FALSE(strequal("", "x"));
}

/* ---- osc_get_payload ---- */

TEST(osc_get_payload, basic_bel_terminator) {
    /* OSC 0 ; hello BEL */
    const char buf[] = "\033]0;hello\007";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 5);
    ASSERT_TRUE(memcmp(p, "hello", 5) == 0);
}

TEST(osc_get_payload, esc_backslash_terminator) {
    /* OSC 2 ; title ESC \ */
    const char buf[] = "\033]2;title\033\\";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 5);
    ASSERT_TRUE(memcmp(p, "title", 5) == 0);
}

TEST(osc_get_payload, eight_bit_st_terminator) {
    /* OSC 0 ; text 0x9C */
    const char buf[] = "\033]0;text\x9C";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 4);
    ASSERT_TRUE(memcmp(p, "text", 4) == 0);
}

TEST(osc_get_payload, multi_digit_osc_number) {
    /* OSC 133 ; A BEL */
    const char buf[] = "\033]133;A\007";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 1);
    ASSERT_TRUE(p[0] == 'A');
}

TEST(osc_get_payload, osc7_file_url) {
    /* OSC 7 ; file://host/tmp BEL */
    const char buf[] = "\033]7;file://host/tmp\007";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 15);
    ASSERT_TRUE(memcmp(p, "file://host/tmp", 15) == 0);
}

TEST(osc_get_payload, no_semicolon) {
    /* OSC with just a number and terminator, no payload */
    const char buf[] = "\033]0\007";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 0);
    (void)p;
}

TEST(osc_get_payload, empty_payload) {
    /* OSC 8 ; ; BEL (hyperlink close — empty params and uri) */
    const char buf[] = "\033]8;;\007";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    /* payload is ";" (the second semicolon is part of the content) */
    ASSERT_EQ(len, 1);
    ASSERT_TRUE(p[0] == ';');
}

TEST(osc_get_payload, osc133_with_exit_code) {
    /* OSC 133 ; D;0 BEL */
    const char buf[] = "\033]133;D;0\007";
    int len;
    const char *p = osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 3);
    ASSERT_TRUE(memcmp(p, "D;0", 3) == 0);
}

TEST(osc_get_payload, too_short) {
    /* Buffer too short — just ESC ] */
    const char buf[] = "\033]";
    int len;
    osc_get_payload(buf, sizeof(buf) - 1, &len);
    ASSERT_EQ(len, 0);
}

int main(void) {
    return testlib_run_all();
}
