/*
 * Minimal C test framework for qemacs
 * Inspired by cosmopolitan libc's testlib
 *
 * Usage:
 *   #include "testlib.h"
 *
 *   TEST(suite_name, test_name) {
 *       ASSERT_EQ(1 + 1, 2);
 *       ASSERT_STREQ("hello", "hello");
 *   }
 *
 *   int main() { return testlib_run_all(); }
 */

#ifndef TESTLIB_H
#define TESTLIB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum number of tests */
#define TESTLIB_MAX_TESTS 1024

typedef struct {
    const char *suite;
    const char *name;
    void (*func)(void);
} TestEntry;

static TestEntry testlib_tests[TESTLIB_MAX_TESTS];
static int testlib_count = 0;
static int testlib_failures = 0;
static int testlib_current_failed = 0;
static const char *testlib_current_suite = "";
static const char *testlib_current_name = "";

static void testlib_register(const char *suite, const char *name, void (*func)(void)) {
    if (testlib_count < TESTLIB_MAX_TESTS) {
        testlib_tests[testlib_count].suite = suite;
        testlib_tests[testlib_count].name = name;
        testlib_tests[testlib_count].func = func;
        testlib_count++;
    }
}

static int testlib_run_all(void) {
    int i, passed = 0;
    for (i = 0; i < testlib_count; i++) {
        testlib_current_suite = testlib_tests[i].suite;
        testlib_current_name = testlib_tests[i].name;
        testlib_current_failed = 0;
        testlib_tests[i].func();
        if (testlib_current_failed) {
            testlib_failures++;
        } else {
            passed++;
        }
    }
    printf("\n%d/%d tests passed", passed, testlib_count);
    if (testlib_failures)
        printf(", \033[31m%d FAILED\033[0m", testlib_failures);
    else
        printf(", \033[32mall ok\033[0m");
    printf("\n");
    return testlib_failures ? 1 : 0;
}

#define TEST(suite, name)                                              \
    static void test_##suite##_##name(void);                           \
    __attribute__((constructor))                                        \
    static void register_##suite##_##name(void) {                      \
        testlib_register(#suite, #name, test_##suite##_##name);        \
    }                                                                  \
    static void test_##suite##_##name(void)

#define FAIL_(file, line, fmt, ...)                                    \
    do {                                                               \
        if (!testlib_current_failed) {                                 \
            printf("\033[31mFAIL\033[0m %s.%s\n",                      \
                   testlib_current_suite, testlib_current_name);       \
        }                                                              \
        testlib_current_failed = 1;                                    \
        printf("  %s:%d: " fmt "\n", file, line, ##__VA_ARGS__);      \
    } while (0)

#define ASSERT_TRUE(expr)                                              \
    do {                                                               \
        if (!(expr))                                                   \
            FAIL_(__FILE__, __LINE__, "expected true: %s", #expr);     \
    } while (0)

#define ASSERT_FALSE(expr)                                             \
    do {                                                               \
        if ((expr))                                                    \
            FAIL_(__FILE__, __LINE__, "expected false: %s", #expr);    \
    } while (0)

#define ASSERT_EQ(a, b)                                                \
    do {                                                               \
        long long _a = (long long)(a), _b = (long long)(b);           \
        if (_a != _b)                                                  \
            FAIL_(__FILE__, __LINE__,                                   \
                  "%s == %s: got %lld, expected %lld", #a, #b, _a, _b);\
    } while (0)

#define ASSERT_NE(a, b)                                                \
    do {                                                               \
        long long _a = (long long)(a), _b = (long long)(b);           \
        if (_a == _b)                                                  \
            FAIL_(__FILE__, __LINE__,                                   \
                  "%s != %s: both are %lld", #a, #b, _a);             \
    } while (0)

#define ASSERT_STREQ(a, b)                                             \
    do {                                                               \
        const char *_a = (a), *_b = (b);                              \
        if (strcmp(_a, _b) != 0)                                       \
            FAIL_(__FILE__, __LINE__,                                   \
                  "%s == %s: got \"%s\", expected \"%s\"", #a, #b, _a, _b); \
    } while (0)

#define ASSERT_STRNE(a, b)                                             \
    do {                                                               \
        const char *_a = (a), *_b = (b);                              \
        if (strcmp(_a, _b) == 0)                                       \
            FAIL_(__FILE__, __LINE__,                                   \
                  "%s != %s: both are \"%s\"", #a, #b, _a);           \
    } while (0)

#define ASSERT_MEMEQ(a, b, n)                                          \
    do {                                                               \
        if (memcmp((a), (b), (n)) != 0)                               \
            FAIL_(__FILE__, __LINE__,                                   \
                  "memcmp(%s, %s, %d) != 0", #a, #b, (int)(n));       \
    } while (0)

#endif /* TESTLIB_H */
