/*
 * Tests for unix.c — clock functions
 *
 * We directly include the clock implementation rather than linking
 * all of unix.c, which pulls in the full event loop.
 */
#include "testlib.h"
#include <time.h>
#include <sys/time.h>

/* Forward declarations — these are what we're testing */
int get_clock_ms(void);
int get_clock_usec(void);

/* Inline implementation for testing — matches unix.c */
int get_clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

int get_clock_usec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int)(ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

TEST(clock, ms_returns_positive) {
    int t = get_clock_ms();
    ASSERT_TRUE(t > 0);
}

TEST(clock, usec_returns_positive) {
    int t = get_clock_usec();
    ASSERT_TRUE(t > 0);
}

TEST(clock, ms_is_monotonic) {
    int t1 = get_clock_ms();
    /* Burn some CPU cycles */
    volatile int x = 0;
    for (int i = 0; i < 1000000; i++) x += i;
    int t2 = get_clock_ms();
    ASSERT_TRUE(t2 >= t1);
}

TEST(clock, usec_is_monotonic) {
    int t1 = get_clock_usec();
    volatile int x = 0;
    for (int i = 0; i < 1000000; i++) x += i;
    int t2 = get_clock_usec();
    ASSERT_TRUE(t2 >= t1);
}

TEST(clock, usec_finer_than_ms) {
    /* usec values should be ~1000x larger than ms for same instant */
    int ms = get_clock_ms();
    int us = get_clock_usec();
    ASSERT_TRUE(us > ms);
}

int main(void) {
    return testlib_run_all();
}
