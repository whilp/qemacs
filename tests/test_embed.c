/*
 * Tests for embedded resource files in APE executable
 *
 * Validates that resource files are properly embedded in the zip
 * portion of the APE binary and accessible via /zip/ paths.
 */

#include "testlib.h"
#include <sys/stat.h>
#include <unistd.h>

/* Expected embedded resource files */
static const char *embed_files[] = {
    "/zip/share/qe/kmaps",
    "/zip/share/qe/ligatures",
    "/zip/share/qe/config.eg",
    "/zip/share/qe/qe-manual.md",
    "/zip/share/qe/qe.1",
};

#define NUM_EMBED_FILES (sizeof(embed_files) / sizeof(embed_files[0]))

/* Check that a file exists and is readable */
static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* Get file size, returns -1 on error */
static long file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (long)st.st_size;
}

/* Read first N bytes of a file, returns bytes read or -1 */
static int read_head(const char *path, char *buf, int size) {
    FILE *f = fopen(path, "rb");
    int n;
    if (!f) return -1;
    n = fread(buf, 1, size, f);
    fclose(f);
    return n;
}

/*
 * Test that all expected resource files exist at /zip/ paths
 */
TEST(embed, files_exist) {
    for (int i = 0; i < (int)NUM_EMBED_FILES; i++) {
        if (!file_exists(embed_files[i])) {
            FAIL_(__FILE__, __LINE__,
                  "embedded file not found: %s", embed_files[i]);
        }
    }
}

/*
 * Test that embedded files have non-zero size
 */
TEST(embed, files_nonempty) {
    for (int i = 0; i < (int)NUM_EMBED_FILES; i++) {
        long sz = file_size(embed_files[i]);
        if (sz <= 0) {
            FAIL_(__FILE__, __LINE__,
                  "embedded file empty or missing: %s (size=%ld)",
                  embed_files[i], sz);
        }
    }
}

/*
 * Test that kmaps file starts with the expected magic header
 * The compiled kmap binary format starts with "kmap" magic bytes
 */
TEST(embed, kmaps_magic) {
    char buf[8] = {0};
    int n = read_head("/zip/share/qe/kmaps", buf, 4);
    ASSERT_TRUE(n >= 4);
    ASSERT_MEMEQ(buf, "kmap", 4);
}

/*
 * Test that ligatures file is readable and has reasonable content
 */
TEST(embed, ligatures_readable) {
    long sz = file_size("/zip/share/qe/ligatures");
    ASSERT_TRUE(sz > 100);  /* ligatures file should be several KB */
}

/*
 * Test that config.eg is a text file starting with expected comment
 */
TEST(embed, config_eg_content) {
    char buf[64] = {0};
    int n = read_head("/zip/share/qe/config.eg", buf, 2);
    ASSERT_TRUE(n >= 2);
    /* config.eg starts with "//" comment */
    ASSERT_TRUE(buf[0] == '/');
    ASSERT_TRUE(buf[1] == '/');
}

/*
 * Test that qe-manual.md looks like a markdown file
 */
TEST(embed, manual_is_markdown) {
    char buf[64] = {0};
    int n = read_head("/zip/share/qe/qe-manual.md", buf, sizeof(buf) - 1);
    ASSERT_TRUE(n > 0);
    /* Manual should start with a markdown heading */
    ASSERT_TRUE(buf[0] == '#' || buf[0] == '\n');
}

/*
 * Test that qe.1 is a troff man page
 */
TEST(embed, manpage_is_troff) {
    char buf[16] = {0};
    int n = read_head("/zip/share/qe/qe.1", buf, 2);
    ASSERT_TRUE(n >= 2);
    /* Man pages start with .\" or .TH */
    ASSERT_TRUE(buf[0] == '.');
}

/*
 * Test that the /zip/share/qe directory itself is accessible
 */
TEST(embed, directory_exists) {
    struct stat st;
    ASSERT_EQ(stat("/zip/share/qe", &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
}

int main() { return testlib_run_all(); }
