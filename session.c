/*
 * Session management for QEmacs - detach/reattach support
 *
 * Inspired by abduco (https://github.com/martanne/abduco)
 * Uses a PTY-proxy architecture with Unix domain sockets.
 *
 * Copyright (c) 2026 QEmacs contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <pwd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <pty.h>

#include "session.h"

#ifndef CTRL
#define CTRL(k) ((k) & 0x1F)
#endif

/* Default detach key: Ctrl-\ (same as abduco) */
static char key_detach = CTRL('\\');

/* OSC escape sequence that qemacs can write to trigger server-side detach.
 * Format: ESC ] q e ; detach BEL
 * Note: if the sequence is split across two PTY reads, detection will fail.
 * This is unlikely in practice since the sequence is only 12 bytes.
 */
#define QE_OSC_DETACH  "\033]qe;detach\007"
#define QE_OSC_DETACH_LEN 12

/*------------------------------------------------------------------------
 * Packet protocol
 *------------------------------------------------------------------------*/

enum PacketType {
    MSG_CONTENT = 0,
    MSG_ATTACH  = 1,
    MSG_DETACH  = 2,
    MSG_RESIZE  = 3,
    MSG_EXIT    = 4,
    MSG_PID     = 5,
};

typedef struct {
    uint32_t type;
    uint32_t len;
    union {
        char msg[4096 - 2 * sizeof(uint32_t)];
        struct {
            uint16_t rows;
            uint16_t cols;
        } ws;
        uint32_t i;
        uint64_t l;
    } u;
} Packet;

static inline size_t packet_header_size(void) {
    return offsetof(Packet, u);
}

static size_t packet_size(Packet *pkt) {
    return packet_header_size() + pkt->len;
}

/*------------------------------------------------------------------------
 * Client tracking (server side)
 *------------------------------------------------------------------------*/

typedef struct Client Client;
struct Client {
    int socket;
    enum {
        STATE_CONNECTED,
        STATE_ATTACHED,
        STATE_DETACHED,
        STATE_DISCONNECTED,
    } state;
    int need_resize;
    int flags;
    Client *next;
};

#define CLIENT_READONLY     (1 << 0)
#define CLIENT_LOWPRIORITY  (1 << 1)

/*------------------------------------------------------------------------
 * Server state
 *------------------------------------------------------------------------*/

typedef struct {
    Client *clients;
    int socket;
    int pty;
    int exit_status;
    struct termios term;
    struct winsize winsize;
    pid_t pid;
    volatile sig_atomic_t running;
    const char *session_name;
    char socket_path[256];
} SessionServer;

static SessionServer server;

/*------------------------------------------------------------------------
 * Client state (attaching side)
 *------------------------------------------------------------------------*/

typedef struct {
    int server_socket;
    int need_resize;
    int flags;
    volatile sig_atomic_t running;
} SessionClient;

static SessionClient client;
static struct termios orig_term;
static int has_term;
static int alternate_buffer;

/*------------------------------------------------------------------------
 * I/O helpers
 *------------------------------------------------------------------------*/

static ssize_t write_all(int fd, const char *buf, size_t len) {
    ssize_t ret = len;
    while (len > 0) {
        ssize_t res = write(fd, buf, len);
        if (res < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            return -1;
        }
        if (res == 0)
            return ret - (ssize_t)len;
        buf += res;
        len -= res;
    }
    return ret;
}

static ssize_t read_all(int fd, char *buf, size_t len) {
    ssize_t ret = len;
    while (len > 0) {
        ssize_t res = read(fd, buf, len);
        if (res < 0) {
            if (errno == EWOULDBLOCK)
                return ret - (ssize_t)len;
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return -1;
        }
        if (res == 0)
            return ret - (ssize_t)len;
        buf += res;
        len -= res;
    }
    return ret;
}

static int send_packet(int fd, Packet *pkt) {
    size_t size = packet_size(pkt);
    if (size > sizeof(*pkt))
        return -1;
    return write_all(fd, (char *)pkt, size) == (ssize_t)size ? 0 : -1;
}

static int recv_packet(int fd, Packet *pkt) {
    ssize_t len = read_all(fd, (char *)pkt, packet_header_size());
    if (len <= 0 || len != (ssize_t)packet_header_size())
        return -1;
    if (pkt->len > sizeof(pkt->u.msg)) {
        pkt->len = 0;
        return -1;
    }
    if (pkt->len > 0) {
        len = read_all(fd, pkt->u.msg, pkt->len);
        if (len <= 0 || len != (ssize_t)pkt->len)
            return -1;
    }
    return 0;
}

/*------------------------------------------------------------------------
 * Socket path management
 *------------------------------------------------------------------------*/

static int set_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int qe_session_get_dir(char *buf, size_t size) {
    const char *dir;
    struct stat sb;
    uid_t uid = getuid();

    /* Try $XDG_RUNTIME_DIR first */
    dir = getenv("XDG_RUNTIME_DIR");
    if (dir && dir[0]) {
        snprintf(buf, size, "%s/qemacs", dir);
        if (mkdir(buf, S_IRWXU) == 0 || errno == EEXIST) {
            if (lstat(buf, &sb) == 0 && S_ISDIR(sb.st_mode) && sb.st_uid == uid)
                return 0;
        }
    }

    /* Try $HOME/.qemacs-sessions */
    dir = getenv("HOME");
    if (!dir || !dir[0]) {
        struct passwd *pw = getpwuid(uid);
        if (pw)
            dir = pw->pw_dir;
    }
    if (dir && dir[0]) {
        snprintf(buf, size, "%s/.qemacs-sessions", dir);
        if (mkdir(buf, S_IRWXU) == 0 || errno == EEXIST) {
            if (lstat(buf, &sb) == 0 && S_ISDIR(sb.st_mode) && sb.st_uid == uid)
                return 0;
        }
    }

    /* Fallback: /tmp/qemacs-<uid> */
    snprintf(buf, size, "/tmp/qemacs-%d", (int)uid);
    if (mkdir(buf, S_IRWXU) == 0 || errno == EEXIST) {
        if (lstat(buf, &sb) == 0 && S_ISDIR(sb.st_mode) && sb.st_uid == uid)
            return 0;
    }

    return -1;
}

static int set_socket_path(char *path, size_t size, const char *name) {
    char dir[200];
    if (name[0] == '/' || name[0] == '.') {
        snprintf(path, size, "%s", name);
        return 0;
    }
    if (qe_session_get_dir(dir, sizeof(dir)) < 0)
        return -1;
    snprintf(path, size, "%s/%s", dir, name);
    return 0;
}

/*------------------------------------------------------------------------
 * Server: socket creation
 *------------------------------------------------------------------------*/

static int server_create_socket(const char *name) {
    struct sockaddr_un addr;
    int fd;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (set_socket_path(addr.sun_path, sizeof(addr.sun_path), name) < 0)
        return -1;

    /* Save the path for cleanup */
    snprintf(server.socket_path, sizeof(server.socket_path), "%s", addr.sun_path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    socklen_t socklen = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
    mode_t mask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
    int r = bind(fd, (struct sockaddr *)&addr, socklen);
    if (r == -1 && errno == EADDRINUSE) {
        /* Remove stale socket from a crashed server and retry */
        struct stat sb;
        if (stat(addr.sun_path, &sb) == 0 && S_ISSOCK(sb.st_mode)) {
            unlink(addr.sun_path);
            r = bind(fd, (struct sockaddr *)&addr, socklen);
        }
    }
    umask(mask);

    if (r == -1) {
        close(fd);
        return -1;
    }
    if (listen(fd, 5) == -1) {
        unlink(addr.sun_path);
        close(fd);
        return -1;
    }
    return fd;
}

/*------------------------------------------------------------------------
 * Server: connect to existing session
 *------------------------------------------------------------------------*/

static int session_connect(const char *name) {
    struct sockaddr_un addr;
    int fd;
    struct stat sb;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (set_socket_path(addr.sun_path, sizeof(addr.sun_path), name) < 0)
        return -1;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1)
        return -1;

    socklen_t socklen = offsetof(struct sockaddr_un, sun_path) + strlen(addr.sun_path) + 1;
    if (connect(fd, (struct sockaddr *)&addr, socklen) == -1) {
        /* Clean up stale socket */
        if (errno == ECONNREFUSED && stat(addr.sun_path, &sb) == 0 && S_ISSOCK(sb.st_mode))
            unlink(addr.sun_path);
        close(fd);
        return -1;
    }
    return fd;
}

/*------------------------------------------------------------------------
 * Server: client management
 *------------------------------------------------------------------------*/

static Client *client_new(int fd) {
    Client *c = calloc(1, sizeof(Client));
    if (!c) return NULL;
    c->socket = fd;
    c->state = STATE_CONNECTED;
    return c;
}

static void client_free(Client *c) {
    if (c) {
        if (c->socket >= 0)
            close(c->socket);
        free(c);
    }
}

static void server_mark_socket_exec(int exec, int usr) {
    struct stat sb;
    if (stat(server.socket_path, &sb) == -1)
        return;
    mode_t mode = sb.st_mode;
    mode_t flag = usr ? S_IXUSR : S_IXGRP;
    if (exec)
        mode |= flag;
    else
        mode &= ~flag;
    chmod(server.socket_path, mode);
}

/*------------------------------------------------------------------------
 * Server: signal handlers
 *------------------------------------------------------------------------*/

static void server_sigchld_handler(int sig) {
    int errsv = errno;
    pid_t pid;
    while ((pid = waitpid(-1, &server.exit_status, WNOHANG)) != 0) {
        if (pid == -1)
            break;
        server.exit_status = WEXITSTATUS(server.exit_status);
        server_mark_socket_exec(1, 0);
    }
    errno = errsv;
}

static void server_sigterm_handler(int sig) {
    exit(EXIT_FAILURE);
}

static void server_atexit_handler(void) {
    if (server.socket_path[0])
        unlink(server.socket_path);
}

/*------------------------------------------------------------------------
 * Server: accept client
 *------------------------------------------------------------------------*/

static Client *server_accept_client(void) {
    int newfd = accept(server.socket, NULL, NULL);
    if (newfd == -1 || set_socket_non_blocking(newfd) == -1) {
        if (newfd != -1) close(newfd);
        return NULL;
    }
    Client *c = client_new(newfd);
    if (!c) {
        close(newfd);
        return NULL;
    }
    if (!server.clients)
        server_mark_socket_exec(1, 1);
    c->next = server.clients;
    server.clients = c;

    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_PID;
    pkt.len = sizeof(pkt.u.l);
    pkt.u.l = getpid();
    send_packet(c->socket, &pkt);

    return c;
}

/*------------------------------------------------------------------------
 * Server: main loop
 *------------------------------------------------------------------------*/

#define FD_SET_MAX(fd, set, maxfd) do { \
    FD_SET(fd, set);                    \
    if ((fd) > (maxfd)) (maxfd) = (fd); \
} while (0)

static void server_mainloop(void) {
    fd_set new_readfds, new_writefds;
    int new_fdmax;
    int exit_packet_delivered = 0;

    atexit(server_atexit_handler);

    FD_ZERO(&new_readfds);
    FD_ZERO(&new_writefds);
    FD_SET(server.socket, &new_readfds);
    new_fdmax = server.socket;
    FD_SET_MAX(server.pty, &new_readfds, new_fdmax);

    while (server.clients || !exit_packet_delivered) {
        int fdmax = new_fdmax;
        fd_set readfds = new_readfds;
        fd_set writefds = new_writefds;
        FD_SET_MAX(server.socket, &readfds, fdmax);

        if (select(fdmax + 1, &readfds, &writefds, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            break;
        }

        FD_ZERO(&new_readfds);
        FD_ZERO(&new_writefds);
        new_fdmax = server.socket;

        int pty_data = 0;
        Packet server_pkt, client_pkt;
        memset(&server_pkt, 0, sizeof(server_pkt));

        if (FD_ISSET(server.socket, &readfds))
            server_accept_client();

        if (FD_ISSET(server.pty, &readfds)) {
            server_pkt.type = MSG_CONTENT;
            ssize_t len = read(server.pty, server_pkt.u.msg, sizeof(server_pkt.u.msg));
            if (len > 0) {
                /* Check for OSC detach escape sequence from qemacs */
                char *osc = memmem(server_pkt.u.msg, len,
                                   QE_OSC_DETACH, QE_OSC_DETACH_LEN);
                if (osc) {
                    /* Remove the OSC sequence from output */
                    size_t before = osc - server_pkt.u.msg;
                    size_t after = len - before - QE_OSC_DETACH_LEN;
                    if (after > 0)
                        memmove(osc, osc + QE_OSC_DETACH_LEN, after);
                    len = before + after;
                    /* Mark all clients for disconnection */
                    for (Client *dc = server.clients; dc; dc = dc->next)
                        dc->state = STATE_DISCONNECTED;
                }
                if (len > 0) {
                    server_pkt.len = len;
                    pty_data = 1;
                }
            } else if (len == 0) {
                server.running = 0;
            } else if (errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK) {
                server.running = 0;
            }
        }

        Client **prev_next = &server.clients;
        Client *c = server.clients;
        while (c) {
            if (FD_ISSET(c->socket, &readfds) && recv_packet(c->socket, &client_pkt) == 0) {
                switch (client_pkt.type) {
                case MSG_CONTENT:
                    write_all(server.pty, client_pkt.u.msg, client_pkt.len);
                    break;
                case MSG_ATTACH:
                    c->flags = client_pkt.u.i;
                    c->state = STATE_ATTACHED;
                    break;
                case MSG_RESIZE:
                    c->state = STATE_ATTACHED;
                    if (!(c->flags & CLIENT_READONLY) && c == server.clients) {
                        struct winsize ws;
                        memset(&ws, 0, sizeof(ws));
                        ws.ws_row = client_pkt.u.ws.rows;
                        ws.ws_col = client_pkt.u.ws.cols;
                        ioctl(server.pty, TIOCSWINSZ, &ws);
                    }
                    kill(-server.pid, SIGWINCH);
                    break;
                case MSG_EXIT:
                    exit_packet_delivered = 1;
                    /* fall through */
                case MSG_DETACH:
                    c->state = STATE_DISCONNECTED;
                    break;
                default:
                    break;
                }
            }

            if (c->state == STATE_DISCONNECTED) {
                int first = (c == server.clients);
                Client *t = c->next;
                client_free(c);
                *prev_next = c = t;
                if (first && server.clients) {
                    Packet rpkt;
                    memset(&rpkt, 0, sizeof(rpkt));
                    rpkt.type = MSG_RESIZE;
                    rpkt.len = 0;
                    send_packet(server.clients->socket, &rpkt);
                } else if (!server.clients) {
                    server_mark_socket_exec(0, 1);
                }
                continue;
            }

            FD_SET_MAX(c->socket, &new_readfds, new_fdmax);

            if (pty_data)
                send_packet(c->socket, &server_pkt);

            if (!server.running) {
                if (server.exit_status != -1) {
                    Packet epkt;
                    memset(&epkt, 0, sizeof(epkt));
                    epkt.type = MSG_EXIT;
                    epkt.u.i = server.exit_status;
                    epkt.len = sizeof(epkt.u.i);
                    if (send_packet(c->socket, &epkt) < 0)
                        FD_SET_MAX(c->socket, &new_writefds, new_fdmax);
                } else {
                    FD_SET_MAX(c->socket, &new_writefds, new_fdmax);
                }
            }

            prev_next = &c->next;
            c = c->next;
        }

        if (server.running)
            FD_SET_MAX(server.pty, &new_readfds, new_fdmax);
    }

    exit(EXIT_SUCCESS);
}

/*------------------------------------------------------------------------
 * Client: signal handler
 *------------------------------------------------------------------------*/

static void client_sigwinch_handler(int sig) {
    client.need_resize = 1;
}

/*------------------------------------------------------------------------
 * Client: terminal setup/restore
 *------------------------------------------------------------------------*/

static void client_restore_terminal(void) {
    if (!has_term)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_term);
    if (alternate_buffer) {
        /* Show cursor, restore main screen buffer */
        printf("\033[?25h\033[?1049l");
        fflush(stdout);
        alternate_buffer = 0;
    }
}

static void client_setup_terminal(void) {
    struct termios raw;

    if (!has_term)
        return;
    atexit(client_restore_terminal);

    raw = orig_term;
    raw.c_iflag &= ~(unsigned)(IGNBRK | BRKINT | PARMRK | ISTRIP |
                                INLCR | IGNCR | ICRNL | IXON | IXOFF);
    raw.c_oflag &= ~(unsigned)(OPOST);
    raw.c_lflag &= ~(unsigned)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(unsigned)(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    if (!alternate_buffer) {
        /* Switch to alternate screen buffer, home cursor */
        printf("\033[?1049h\033[H");
        fflush(stdout);
        alternate_buffer = 1;
    }
}

/*------------------------------------------------------------------------
 * Client: main loop
 *------------------------------------------------------------------------*/

static int client_mainloop(void) {
    sigset_t emptyset, blockset;
    Packet pkt;

    sigemptyset(&emptyset);
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGWINCH);
    sigprocmask(SIG_BLOCK, &blockset, NULL);

    client.need_resize = 1;
    client.running = 1;

    /* Send attach packet */
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_ATTACH;
    pkt.u.i = client.flags;
    pkt.len = sizeof(pkt.u.i);
    send_packet(client.server_socket, &pkt);

    while (client.running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(client.server_socket, &fds);

        if (client.need_resize) {
            struct winsize ws;
            if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) != -1) {
                Packet rpkt;
                memset(&rpkt, 0, sizeof(rpkt));
                rpkt.type = MSG_RESIZE;
                rpkt.u.ws.rows = ws.ws_row;
                rpkt.u.ws.cols = ws.ws_col;
                rpkt.len = sizeof(rpkt.u.ws);
                if (send_packet(client.server_socket, &rpkt) == 0)
                    client.need_resize = 0;
            }
        }

        int maxfd = client.server_socket;
        if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;
        if (pselect(maxfd + 1, &fds, NULL, NULL, NULL, &emptyset) == -1) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (FD_ISSET(client.server_socket, &fds)) {
            Packet rpkt;
            if (recv_packet(client.server_socket, &rpkt) == 0) {
                switch (rpkt.type) {
                case MSG_CONTENT:
                    write_all(STDOUT_FILENO, rpkt.u.msg, rpkt.len);
                    break;
                case MSG_RESIZE:
                    client.need_resize = 1;
                    break;
                case MSG_EXIT:
                    send_packet(client.server_socket, &rpkt);
                    close(client.server_socket);
                    return rpkt.u.i;
                default:
                    break;
                }
            } else {
                client.running = 0;
            }
        }

        if (FD_ISSET(STDIN_FILENO, &fds)) {
            Packet ipkt;
            memset(&ipkt, 0, sizeof(ipkt));
            ipkt.type = MSG_CONTENT;
            ssize_t len = read(STDIN_FILENO, ipkt.u.msg, sizeof(ipkt.u.msg));
            if (len == -1 && errno != EAGAIN && errno != EINTR)
                break;
            if (len > 0) {
                ipkt.len = len;
                if (ipkt.u.msg[0] == key_detach) {
                    Packet dpkt;
                    memset(&dpkt, 0, sizeof(dpkt));
                    dpkt.type = MSG_DETACH;
                    dpkt.len = 0;
                    send_packet(client.server_socket, &dpkt);
                    close(client.server_socket);
                    return -1;  /* detached */
                }
                send_packet(client.server_socket, &ipkt);
            } else if (len == 0) {
                /* stdin EOF */
                return -1;
            }
        }
    }

    return -2;  /* I/O error */
}

/*------------------------------------------------------------------------
 * Session creation (double-fork + forkpty)
 *------------------------------------------------------------------------*/

static int create_session(const char *name, int argc, char **argv) {
    int client_pipe[2];
    pid_t pid;
    char errormsg[256];
    struct sigaction sa;

    if (pipe(client_pipe) == -1)
        return -1;

    server.running = 1;
    server.exit_status = -1;
    server.session_name = name;

    if ((server.socket = server_create_socket(name)) == -1) {
        close(client_pipe[0]);
        close(client_pipe[1]);
        return -1;
    }

    switch ((pid = fork())) {
    case 0:  /* first child */
        setsid();
        close(client_pipe[0]);

        switch ((pid = fork())) {
        case 0: {  /* second child (will become server) */
            int server_pipe[2];
            if (pipe(server_pipe) == -1) {
                snprintf(errormsg, sizeof(errormsg), "pipe: %s\n", strerror(errno));
                write_all(client_pipe[1], errormsg, strlen(errormsg));
                close(client_pipe[1]);
                _exit(EXIT_FAILURE);
            }

            sa.sa_flags = 0;
            sigemptyset(&sa.sa_mask);
            sa.sa_handler = server_sigchld_handler;
            sigaction(SIGCHLD, &sa, NULL);

            switch (server.pid = forkpty(&server.pty, NULL,
                                         has_term ? &server.term : NULL,
                                         &server.winsize)) {
            case 0:  /* grandchild: exec qemacs */
                close(server.socket);
                close(server_pipe[0]);
                setenv("QE_SESSION", name, 1);
                if (fcntl(client_pipe[1], F_SETFD, FD_CLOEXEC) == 0 &&
                    fcntl(server_pipe[1], F_SETFD, FD_CLOEXEC) == 0) {
                    /* Re-exec ourselves without session flags */
                    execvp(argv[0], argv);
                }
                snprintf(errormsg, sizeof(errormsg), "execvp: %s: %s\n",
                         argv[0], strerror(errno));
                write_all(client_pipe[1], errormsg, strlen(errormsg));
                write_all(server_pipe[1], errormsg, strlen(errormsg));
                close(client_pipe[1]);
                close(server_pipe[1]);
                _exit(EXIT_FAILURE);
                break;

            case -1:  /* forkpty failed */
                snprintf(errormsg, sizeof(errormsg), "forkpty: %s\n", strerror(errno));
                write_all(client_pipe[1], errormsg, strlen(errormsg));
                close(client_pipe[1]);
                close(server_pipe[0]);
                close(server_pipe[1]);
                _exit(EXIT_FAILURE);
                break;

            default:  /* server process */
                sa.sa_handler = server_sigterm_handler;
                sigaction(SIGTERM, &sa, NULL);
                sigaction(SIGINT, &sa, NULL);
                sa.sa_handler = SIG_IGN;
                sigaction(SIGPIPE, &sa, NULL);
                sigaction(SIGHUP, &sa, NULL);

                if (chdir("/") == -1)
                    _exit(EXIT_FAILURE);

                /* Redirect stdio to /dev/null */
                {
                    int devnull = open("/dev/null", O_RDWR);
                    if (devnull != -1) {
                        dup2(devnull, STDIN_FILENO);
                        dup2(devnull, STDOUT_FILENO);
                        dup2(devnull, STDERR_FILENO);
                        close(devnull);
                    }
                }

                close(client_pipe[1]);
                close(server_pipe[1]);
                /* Wait for child to exec successfully */
                if (read_all(server_pipe[0], errormsg, sizeof(errormsg)) > 0)
                    _exit(EXIT_FAILURE);
                close(server_pipe[0]);
                server_mainloop();
                break;
            }
            break;
        }
        case -1:  /* fork failed */
            snprintf(errormsg, sizeof(errormsg), "fork: %s\n", strerror(errno));
            write_all(client_pipe[1], errormsg, strlen(errormsg));
            close(client_pipe[1]);
            _exit(EXIT_FAILURE);
            break;

        default:  /* intermediate process exits immediately */
            close(client_pipe[1]);
            _exit(EXIT_SUCCESS);
            break;
        }
        break;

    case -1:  /* fork failed */
        close(client_pipe[0]);
        close(client_pipe[1]);
        return -1;

    default:  /* original parent (client) */
        close(client_pipe[1]);
        while (waitpid(pid, NULL, 0) == -1 && errno == EINTR)
            ;
        {
            ssize_t len = read_all(client_pipe[0], errormsg, sizeof(errormsg));
            if (len > 0) {
                write_all(STDERR_FILENO, errormsg, len);
                unlink(server.socket_path);
                close(client_pipe[0]);
                return -1;
            }
        }
        close(client_pipe[0]);
        break;
    }

    return 0;
}

/*------------------------------------------------------------------------
 * Attach to existing session
 *------------------------------------------------------------------------*/

static int attach_session(const char *name, int terminate) {
    struct sigaction sa;
    int status;

    if (client.server_socket > 0)
        close(client.server_socket);

    client.server_socket = session_connect(name);
    if (client.server_socket == -1)
        return -1;

    if (set_socket_non_blocking(client.server_socket) == -1) {
        close(client.server_socket);
        return -1;
    }

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = client_sigwinch_handler;
    sigaction(SIGWINCH, &sa, NULL);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);

    client_setup_terminal();
    status = client_mainloop();
    client_restore_terminal();

    if (status == -1) {
        fprintf(stderr, "qe: session '%s': detached\n", name);
    } else if (status == -2) {
        fprintf(stderr, "qe: session '%s': connection lost\n", name);
    } else {
        fprintf(stderr, "qe: session '%s': terminated with exit status %d\n", name, status);
        if (terminate)
            exit(status);
    }

    return terminate ? 1 : 0;
}

/*------------------------------------------------------------------------
 * List sessions
 *------------------------------------------------------------------------*/

int qe_session_list(void) {
    char dir[256];
    struct dirent **namelist;
    int n;

    if (qe_session_get_dir(dir, sizeof(dir)) < 0) {
        fprintf(stderr, "qe: cannot determine session directory\n");
        return 1;
    }

    n = scandir(dir, &namelist, NULL, alphasort);
    if (n < 0) {
        fprintf(stderr, "qe: cannot scan %s: %s\n", dir, strerror(errno));
        return 1;
    }

    printf("QEmacs sessions in %s:\n", dir);
    int found = 0;
    while (n--) {
        char path[512];
        struct stat sb;
        snprintf(path, sizeof(path), "%s/%s", dir, namelist[n]->d_name);
        if (stat(path, &sb) == 0 && S_ISSOCK(sb.st_mode)) {
            char timebuf[64];
            strftime(timebuf, sizeof(timebuf), "%F %T", localtime(&sb.st_mtime));
            char status = ' ';
            if (sb.st_mode & S_IXUSR)
                status = '*';  /* clients attached */
            else if (sb.st_mode & S_IXGRP)
                status = '+';  /* process terminated */
            printf("  %c %-20s  %s\n", status, namelist[n]->d_name, timebuf);
            found = 1;
        }
        free(namelist[n]);
    }
    free(namelist);

    if (!found)
        printf("  (no active sessions)\n");

    return 0;
}

/*------------------------------------------------------------------------
 * Main entry point from qe.c
 *------------------------------------------------------------------------*/

int qe_session_handle(int action, const char *session_name,
                      int argc, char **argv) {
    if (action == SESSION_ACTION_NONE)
        return -1;  /* no session action, proceed normally */

    if (action == SESSION_ACTION_LIST)
        return qe_session_list();

    if (!session_name || !session_name[0]) {
        fprintf(stderr, "qe: session name required\n");
        return 1;
    }

    /* Save terminal state before forking */
    if (tcgetattr(STDIN_FILENO, &orig_term) != -1) {
        server.term = orig_term;
        has_term = 1;
    }
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &server.winsize) == -1) {
        server.winsize.ws_col = 80;
        server.winsize.ws_row = 25;
    }

    switch (action) {
    case SESSION_ACTION_CREATE:
        if (create_session(session_name, argc, argv) < 0) {
            fprintf(stderr, "qe: failed to create session '%s': %s\n",
                    session_name, strerror(errno));
            return 1;
        }
        if (attach_session(session_name, 1) < 0) {
            fprintf(stderr, "qe: failed to attach to session '%s': %s\n",
                    session_name, strerror(errno));
            return 1;
        }
        break;

    case SESSION_ACTION_ATTACH:
        if (attach_session(session_name, 1) < 0) {
            fprintf(stderr, "qe: no session '%s' found\n", session_name);
            return 1;
        }
        break;

    case SESSION_ACTION_CREATE_ATTACH:
        /* Try attach first, create if not found */
        if (attach_session(session_name, 1) < 0) {
            if (create_session(session_name, argc, argv) < 0) {
                fprintf(stderr, "qe: failed to create session '%s': %s\n",
                        session_name, strerror(errno));
                return 1;
            }
            if (attach_session(session_name, 1) < 0) {
                fprintf(stderr, "qe: failed to attach to session '%s': %s\n",
                        session_name, strerror(errno));
                return 1;
            }
        }
        break;

    default:
        return -1;
    }

    return 0;
}
