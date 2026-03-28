/*
 * TTY input handling tests for QEmacs
 *
 * Tests key input through a real PTY with the TTY driver, which
 * exercises the full escape sequence parser and key composition.
 * This complements test_terminal.c which uses the test display
 * driver (bypassing the TTY escape sequence state machine).
 *
 * Copyright (c) 2026 Contributors.
 * MIT License (same as QEmacs).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <pty.h>
#include <sys/select.h>

#include "testlib.h"

#define QE_BINARY_DEFAULT  "../o/qe"
static const char *qe_binary;

/*--- PTY session helpers ---*/

typedef struct {
    pid_t pid;
    int master_fd;
} PTYSession;

static int pty_start(PTYSession *ps) {
    struct winsize ws = { .ws_row = 25, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 };
    char buf[65536];
    int total = 0;

    char ptyname[256];
    ps->pid = forkpty(&ps->master_fd, ptyname, NULL, &ws);
    if (ps->pid < 0)
        return -1;

    if (ps->pid == 0) {
        /* child: run qe inside the PTY so the TTY driver is active */
        setenv("QE_SESSION", "test", 1);
        setenv("TERM", "xterm-256color", 1);
        execlp(qe_binary, qe_binary, (char *)NULL);
        _exit(1);
    }

    /* wait for qe to initialise and drain startup output */
    usleep(500000);
    for (int elapsed = 0; elapsed < 500; elapsed += 50) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ps->master_fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        if (select(ps->master_fd + 1, &fds, NULL, NULL, &tv) > 0) {
            int n = read(ps->master_fd, buf + total,
                         sizeof(buf) - 1 - total);
            if (n > 0) total += n;
        }
    }
    return 0;
}

/* read until pattern is found or timeout_ms elapses */
static int pty_read_until(PTYSession *ps, const char *pattern,
                          int timeout_ms)
{
    char buf[65536];
    int total = 0;
    for (int elapsed = 0; elapsed < timeout_ms; elapsed += 50) {
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(ps->master_fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 50000;
        if (select(ps->master_fd + 1, &fds, NULL, NULL, &tv) > 0) {
            int n = read(ps->master_fd, buf + total,
                         sizeof(buf) - 1 - total);
            if (n > 0) {
                total += n;
                buf[total] = '\0';
                if (strstr(buf, pattern))
                    return 1;
            }
        }
    }
    return 0;
}

static void pty_stop(PTYSession *ps) {
    if (ps->pid > 0) {
        kill(ps->pid, SIGTERM);
        waitpid(ps->pid, NULL, 0);
        ps->pid = 0;
    }
    if (ps->master_fd >= 0) {
        close(ps->master_fd);
        ps->master_fd = -1;
    }
}

/*--- Tests ---*/

/* M-x via ESC then x through the TTY driver */
TEST(tty_input, mx_esc_x)
{
    PTYSession ps;
    if (pty_start(&ps) != 0) { ASSERT_TRUE(0); return; }

    write(ps.master_fd, "\x1b", 1);
    usleep(50000);
    write(ps.master_fd, "x", 1);

    ASSERT_TRUE(pty_read_until(&ps, "Command:", 3000));
    pty_stop(&ps);
}

/* M-x via modifyOtherKeys: CSI 27;3;120~ */
TEST(tty_input, mx_modify_other_keys)
{
    PTYSession ps;
    if (pty_start(&ps) != 0) { ASSERT_TRUE(0); return; }

    write(ps.master_fd, "\033[27;3;120~", 11);

    ASSERT_TRUE(pty_read_until(&ps, "Command:", 3000));
    pty_stop(&ps);
}

/* M-x via CSI u: CSI 120;3u */
TEST(tty_input, mx_csi_u)
{
    PTYSession ps;
    if (pty_start(&ps) != 0) { ASSERT_TRUE(0); return; }

    write(ps.master_fd, "\033[120;3u", 8);

    ASSERT_TRUE(pty_read_until(&ps, "Command:", 3000));
    pty_stop(&ps);
}

/* F2 (alternative execute-command binding): CSI 12~ */
TEST(tty_input, mx_f2)
{
    PTYSession ps;
    if (pty_start(&ps) != 0) { ASSERT_TRUE(0); return; }

    write(ps.master_fd, "\033[12~", 5);

    ASSERT_TRUE(pty_read_until(&ps, "Command:", 3000));
    pty_stop(&ps);
}

/* Regression: focus-in event must not consume subsequent M-x keystrokes.
 *
 * The TTY driver enables focus reporting (CSI ?1004h).  When the
 * terminal sends a FocusIn event (CSI I), the handler requests the
 * clipboard via OSC 52.  Previously the clipboard reader blocked
 * synchronously for 200ms, consuming any user keystrokes that arrived
 * during that window.  This broke M-x when the user switched windows
 * (triggering FocusIn) and then typed ESC x. */
TEST(tty_input, mx_after_focus_event)
{
    PTYSession ps;
    if (pty_start(&ps) != 0) { ASSERT_TRUE(0); return; }

    /* send FocusIn event */
    write(ps.master_fd, "\033[I", 3);
    usleep(100000);

    /* send M-x via ESC then x */
    write(ps.master_fd, "\x1b", 1);
    usleep(50000);
    write(ps.master_fd, "x", 1);

    ASSERT_TRUE(pty_read_until(&ps, "Command:", 3000));
    pty_stop(&ps);
}

/* Focus event immediately followed by M-x in one write */
TEST(tty_input, mx_focus_then_immediate_mx)
{
    PTYSession ps;
    if (pty_start(&ps) != 0) { ASSERT_TRUE(0); return; }

    write(ps.master_fd, "\033[I\x1bx", 5);

    ASSERT_TRUE(pty_read_until(&ps, "Command:", 3000));
    pty_stop(&ps);
}

/*--- Main ---*/

int main(void)
{
    const char *env = getenv("QE_BINARY");
    qe_binary = (env && *env) ? env : QE_BINARY_DEFAULT;

    signal(SIGPIPE, SIG_IGN);

    if (access(qe_binary, X_OK) != 0) {
        fprintf(stderr, "ERROR: qe binary not found at '%s'\n", qe_binary);
        return 1;
    }

    return testlib_run_all();
}
