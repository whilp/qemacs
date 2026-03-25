/*
 * Tests for session.c - packet protocol, socket paths, and session lifecycle
 */

#define _GNU_SOURCE
#include "testlib.h"
#include "config.h"

#ifdef CONFIG_SESSION_DETACH

/*
 * Include session.c directly to test static functions.
 * We need to provide the right environment for it.
 */
#include "../session.c"

/* ---- packet_header_size / packet_size ---- */

TEST(packet, header_size) {
    /* Header is type (4) + len (4) = 8 bytes */
    ASSERT_EQ(packet_header_size(), 8);
}

TEST(packet, size_empty) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_CONTENT;
    pkt.len = 0;
    ASSERT_EQ(packet_size(&pkt), 8);
}

TEST(packet, size_with_data) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_CONTENT;
    pkt.len = 100;
    ASSERT_EQ(packet_size(&pkt), 108);
}

TEST(packet, size_max) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.len = sizeof(pkt.u.msg);
    ASSERT_EQ(packet_size(&pkt), sizeof(Packet));
}

/* ---- packet types ---- */

TEST(packet, types_distinct) {
    ASSERT_NE(MSG_CONTENT, MSG_ATTACH);
    ASSERT_NE(MSG_ATTACH, MSG_DETACH);
    ASSERT_NE(MSG_DETACH, MSG_RESIZE);
    ASSERT_NE(MSG_RESIZE, MSG_EXIT);
    ASSERT_NE(MSG_EXIT, MSG_PID);
}

/* ---- send_packet / recv_packet over socketpair ---- */

TEST(packet, send_recv_content) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_CONTENT;
    out.len = 5;
    memcpy(out.u.msg, "hello", 5);
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_CONTENT);
    ASSERT_EQ(in.len, 5);
    ASSERT_MEMEQ(in.u.msg, "hello", 5);

    close(fds[0]);
    close(fds[1]);
}

TEST(packet, send_recv_resize) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_RESIZE;
    out.u.ws.rows = 24;
    out.u.ws.cols = 80;
    out.len = sizeof(out.u.ws);
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_RESIZE);
    ASSERT_EQ(in.u.ws.rows, 24);
    ASSERT_EQ(in.u.ws.cols, 80);

    close(fds[0]);
    close(fds[1]);
}

TEST(packet, send_recv_pid) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_PID;
    out.u.l = 12345;
    out.len = sizeof(out.u.l);
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_PID);
    ASSERT_EQ(in.u.l, 12345);

    close(fds[0]);
    close(fds[1]);
}

TEST(packet, send_recv_empty) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_DETACH;
    out.len = 0;
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_DETACH);
    ASSERT_EQ(in.len, 0);

    close(fds[0]);
    close(fds[1]);
}

TEST(packet, recv_closed_fd) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    close(fds[0]);  /* close writer */

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), -1);

    close(fds[1]);
}

/* ---- write_all / read_all ---- */

TEST(io, write_read_all) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    const char *msg = "test message";
    size_t len = strlen(msg);
    ASSERT_EQ(write_all(fds[0], msg, len), (ssize_t)len);

    char buf[64];
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(read_all(fds[1], buf, len), (ssize_t)len);
    ASSERT_MEMEQ(buf, msg, len);

    close(fds[0]);
    close(fds[1]);
}

/* ---- socket path resolution ---- */

TEST(socket_path, absolute_path) {
    char path[256];
    ASSERT_EQ(set_socket_path(path, sizeof(path), "/tmp/qe-test-sock"), 0);
    ASSERT_STREQ(path, "/tmp/qe-test-sock");
}

TEST(socket_path, relative_path) {
    char path[256];
    ASSERT_EQ(set_socket_path(path, sizeof(path), "./my-session"), 0);
    /* Should start with current directory */
    ASSERT_TRUE(path[0] == '.' || path[0] == '/');
}

TEST(socket_path, named_session) {
    char path[256];
    ASSERT_EQ(set_socket_path(path, sizeof(path), "mysession"), 0);
    /* Should be in a directory, not just the bare name */
    ASSERT_TRUE(strlen(path) > strlen("mysession"));
    /* Should end with the session name */
    const char *base = strrchr(path, '/');
    ASSERT_TRUE(base != NULL);
    ASSERT_STREQ(base + 1, "mysession");
}

/* ---- qe_session_get_dir ---- */

TEST(session_dir, returns_valid_dir) {
    char dir[256];
    ASSERT_EQ(qe_session_get_dir(dir, sizeof(dir)), 0);
    ASSERT_TRUE(strlen(dir) > 0);
    ASSERT_TRUE(dir[0] == '/');
}

TEST(session_dir, directory_exists) {
    char dir[256];
    ASSERT_EQ(qe_session_get_dir(dir, sizeof(dir)), 0);
    struct stat sb;
    ASSERT_EQ(stat(dir, &sb), 0);
    ASSERT_TRUE(S_ISDIR(sb.st_mode));
}

/* ---- client_new / client_free ---- */

TEST(client, new_and_free) {
    Client *c = client_new(42);
    ASSERT_TRUE(c != NULL);
    ASSERT_EQ(c->socket, 42);
    ASSERT_EQ(c->state, STATE_CONNECTED);
    ASSERT_TRUE(c->next == NULL);
    /* Don't close fd 42 - it's not a real fd, just set it to -1 */
    c->socket = -1;
    client_free(c);
}

/* ---- socket create / connect lifecycle ---- */

TEST(socket, create_and_cleanup) {
    const char *name = "/tmp/qe-test-socket-create";

    /* Clean up any leftover */
    unlink(name);

    /* Save/restore server state */
    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int fd = server_create_socket(name);
    ASSERT_TRUE(fd >= 0);

    /* Socket file should exist */
    struct stat sb;
    ASSERT_EQ(stat(name, &sb), 0);
    ASSERT_TRUE(S_ISSOCK(sb.st_mode));

    close(fd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

TEST(socket, connect_nonexistent) {
    int fd = session_connect("/tmp/qe-test-nonexistent-socket");
    ASSERT_TRUE(fd < 0);
}

TEST(socket, create_connect) {
    const char *name = "/tmp/qe-test-socket-cc";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);

    int cfd = session_connect(name);
    ASSERT_TRUE(cfd >= 0);

    close(cfd);
    close(sfd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

/* ---- OSC detach sequence detection ---- */

TEST(osc, detach_sequence_defined) {
    ASSERT_STREQ(QE_OSC_DETACH, "\033]qe;detach\007");
    ASSERT_EQ(QE_OSC_DETACH_LEN, 12);
    ASSERT_EQ(strlen(QE_OSC_DETACH), (size_t)QE_OSC_DETACH_LEN);
}

TEST(osc, memmem_finds_sequence) {
    char buf[64] = "hello\033]qe;detach\007world";
    void *p = memmem(buf, strlen(buf), QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ((char *)p - buf, 5);
}

TEST(osc, memmem_not_found) {
    char buf[64] = "hello world";
    void *p = memmem(buf, strlen(buf), QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    ASSERT_TRUE(p == NULL);
}

#else /* !CONFIG_SESSION_DETACH */

/* When session detach is disabled, just have a passing placeholder test */
TEST(session, disabled) {
    /* Session detach not configured - skip tests */
    ASSERT_TRUE(1);
}

#endif /* CONFIG_SESSION_DETACH */

int main(void) {
    return testlib_run_all();
}
