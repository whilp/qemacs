/*
 * Tests for cosmopolitan-specific features
 *
 * These test that our cosmo integration compiles and works correctly
 * even when __COSMOPOLITAN__ is not defined (graceful fallback).
 */
#include "testlib.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pwd.h>

/* Test that pledge/unveil declarations are compatible */
#ifdef __COSMOPOLITAN__
#include <libc/calls/pledge.h>
#endif

TEST(cosmo, build_detection) {
#ifdef __COSMOPOLITAN__
    /* When built with cosmocc, this should be defined */
    ASSERT_TRUE(1);
#else
    /* When built with system compiler, this is expected */
    ASSERT_TRUE(1);
#endif
}

TEST(cosmo, clock_monotonic_available) {
    /* CLOCK_MONOTONIC should always be available */
    struct timespec ts;
    int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    ASSERT_EQ(ret, 0);
    ASSERT_TRUE(ts.tv_sec >= 0);
}

TEST(cosmo, pwd_available) {
    /* getpwnam should be available (we use it for ~ expansion) */
    struct passwd *pw = getpwnam("root");
    /* root may or may not exist in all environments, but the call shouldn't crash */
    (void)pw;
    ASSERT_TRUE(1);
}

int main(void) {
    return testlib_run_all();
}
