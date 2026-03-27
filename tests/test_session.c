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

/* ---- MSG_DETACH: client state transitions ---- */

TEST(detach, msg_detach_over_socketpair) {
    /* Verify that a MSG_DETACH packet can be sent and received correctly */
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

TEST(detach, client_state_to_disconnected) {
    /* When server receives MSG_DETACH, client state should become DISCONNECTED */
    Client *c = client_new(42);
    ASSERT_TRUE(c != NULL);
    ASSERT_EQ(c->state, STATE_CONNECTED);

    /* Simulate state transition as server_mainloop does for MSG_DETACH */
    c->state = STATE_DISCONNECTED;
    ASSERT_EQ(c->state, STATE_DISCONNECTED);

    c->socket = -1;
    client_free(c);
}

TEST(detach, client_state_attached_then_detach) {
    /* Simulate: client connects, attaches, then detaches */
    Client *c = client_new(99);
    ASSERT_TRUE(c != NULL);
    ASSERT_EQ(c->state, STATE_CONNECTED);

    /* Attach */
    c->state = STATE_ATTACHED;
    ASSERT_EQ(c->state, STATE_ATTACHED);

    /* Detach */
    c->state = STATE_DETACHED;
    ASSERT_EQ(c->state, STATE_DETACHED);

    /* Disconnect */
    c->state = STATE_DISCONNECTED;
    ASSERT_EQ(c->state, STATE_DISCONNECTED);

    c->socket = -1;
    client_free(c);
}

TEST(detach, detach_packet_roundtrip) {
    /* Full roundtrip: server socket -> accept -> client sends detach */
    const char *name = "/tmp/qe-test-detach-roundtrip";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);

    int cfd = session_connect(name);
    ASSERT_TRUE(cfd >= 0);

    /* Accept on server side */
    int afd = accept(sfd, NULL, NULL);
    ASSERT_TRUE(afd >= 0);

    /* Client sends MSG_DETACH */
    Packet dpkt;
    memset(&dpkt, 0, sizeof(dpkt));
    dpkt.type = MSG_DETACH;
    dpkt.len = 0;
    ASSERT_EQ(send_packet(cfd, &dpkt), 0);

    /* Server receives MSG_DETACH */
    Packet rpkt;
    memset(&rpkt, 0, sizeof(rpkt));
    ASSERT_EQ(recv_packet(afd, &rpkt), 0);
    ASSERT_EQ(rpkt.type, MSG_DETACH);
    ASSERT_EQ(rpkt.len, 0);

    close(afd);
    close(cfd);
    close(sfd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

/* ---- OSC detach: content stripping ---- */

TEST(detach, osc_strip_from_content) {
    /* Simulate what server_mainloop does when OSC detach is in PTY output */
    char buf[128];
    const char *input = "before\033]qe;detach\007after";
    size_t len = strlen(input);
    memcpy(buf, input, len);

    char *osc = memmem(buf, len, QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    ASSERT_TRUE(osc != NULL);

    size_t before = osc - buf;
    size_t after = len - before - QE_OSC_DETACH_LEN;
    ASSERT_EQ(before, 6);  /* "before" */
    ASSERT_EQ(after, 5);   /* "after" */

    if (after > 0)
        memmove(osc, osc + QE_OSC_DETACH_LEN, after);
    len = before + after;

    ASSERT_EQ(len, 11);
    ASSERT_MEMEQ(buf, "beforeafter", 11);
}

TEST(detach, osc_at_start_of_content) {
    char buf[128];
    const char *input = "\033]qe;detach\007trailing";
    size_t len = strlen(input);
    memcpy(buf, input, len);

    char *osc = memmem(buf, len, QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    ASSERT_TRUE(osc != NULL);
    ASSERT_EQ(osc - buf, 0);

    size_t before = 0;
    size_t after = len - QE_OSC_DETACH_LEN;
    if (after > 0)
        memmove(buf, buf + QE_OSC_DETACH_LEN, after);
    len = before + after;

    ASSERT_EQ(len, 8);
    ASSERT_MEMEQ(buf, "trailing", 8);
}

TEST(detach, osc_at_end_of_content) {
    char buf[128];
    const char *input = "leading\033]qe;detach\007";
    size_t len = strlen(input);
    memcpy(buf, input, len);

    char *osc = memmem(buf, len, QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    ASSERT_TRUE(osc != NULL);

    size_t before = osc - buf;
    size_t after = len - before - QE_OSC_DETACH_LEN;
    ASSERT_EQ(after, 0);
    len = before;

    ASSERT_EQ(len, 7);
    ASSERT_MEMEQ(buf, "leading", 7);
}

TEST(detach, osc_entire_content) {
    /* Content is just the OSC sequence itself */
    char buf[128];
    memcpy(buf, QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    size_t len = QE_OSC_DETACH_LEN;

    char *osc = memmem(buf, len, QE_OSC_DETACH, QE_OSC_DETACH_LEN);
    ASSERT_TRUE(osc != NULL);

    size_t before = osc - buf;
    size_t after = len - before - QE_OSC_DETACH_LEN;
    ASSERT_EQ(before, 0);
    ASSERT_EQ(after, 0);
    len = 0;
    ASSERT_EQ(len, 0);
}

/* ---- MSG_ATTACH: state transitions and flags ---- */

TEST(attach, msg_attach_with_flags) {
    /* Send an MSG_ATTACH with flags and verify receipt */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_ATTACH;
    out.u.i = CLIENT_READONLY | CLIENT_LOWPRIORITY;
    out.len = sizeof(out.u.i);
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_ATTACH);
    ASSERT_EQ(in.u.i, (uint32_t)(CLIENT_READONLY | CLIENT_LOWPRIORITY));

    close(fds[0]);
    close(fds[1]);
}

TEST(attach, msg_attach_readonly_flag) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_ATTACH;
    out.u.i = CLIENT_READONLY;
    out.len = sizeof(out.u.i);
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_ATTACH);
    ASSERT_TRUE(in.u.i & CLIENT_READONLY);
    ASSERT_FALSE(in.u.i & CLIENT_LOWPRIORITY);

    close(fds[0]);
    close(fds[1]);
}

TEST(attach, client_state_to_attached) {
    /* Simulate server handling of MSG_ATTACH: update state and flags */
    Client *c = client_new(55);
    ASSERT_TRUE(c != NULL);
    ASSERT_EQ(c->state, STATE_CONNECTED);
    ASSERT_EQ(c->flags, 0);

    /* Simulate server receiving MSG_ATTACH */
    c->flags = CLIENT_READONLY;
    c->state = STATE_ATTACHED;
    ASSERT_EQ(c->state, STATE_ATTACHED);
    ASSERT_EQ(c->flags, CLIENT_READONLY);

    c->socket = -1;
    client_free(c);
}

TEST(attach, attach_roundtrip) {
    /* Full roundtrip: create socket, connect, exchange attach packet */
    const char *name = "/tmp/qe-test-attach-roundtrip";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);

    int cfd = session_connect(name);
    ASSERT_TRUE(cfd >= 0);

    int afd = accept(sfd, NULL, NULL);
    ASSERT_TRUE(afd >= 0);

    /* Client sends MSG_ATTACH */
    Packet apkt;
    memset(&apkt, 0, sizeof(apkt));
    apkt.type = MSG_ATTACH;
    apkt.u.i = 0;
    apkt.len = sizeof(apkt.u.i);
    ASSERT_EQ(send_packet(cfd, &apkt), 0);

    /* Server receives and processes */
    Packet rpkt;
    memset(&rpkt, 0, sizeof(rpkt));
    ASSERT_EQ(recv_packet(afd, &rpkt), 0);
    ASSERT_EQ(rpkt.type, MSG_ATTACH);

    /* Server sends content back */
    Packet cpkt;
    memset(&cpkt, 0, sizeof(cpkt));
    cpkt.type = MSG_CONTENT;
    cpkt.len = 4;
    memcpy(cpkt.u.msg, "test", 4);
    ASSERT_EQ(send_packet(afd, &cpkt), 0);

    /* Client receives content */
    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(cfd, &in), 0);
    ASSERT_EQ(in.type, MSG_CONTENT);
    ASSERT_EQ(in.len, 4);
    ASSERT_MEMEQ(in.u.msg, "test", 4);

    close(afd);
    close(cfd);
    close(sfd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

TEST(attach, attach_then_detach_roundtrip) {
    /* Client attaches, then detaches: full message exchange */
    const char *name = "/tmp/qe-test-attach-detach";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);

    int cfd = session_connect(name);
    ASSERT_TRUE(cfd >= 0);

    int afd = accept(sfd, NULL, NULL);
    ASSERT_TRUE(afd >= 0);

    /* Client attaches */
    Packet apkt;
    memset(&apkt, 0, sizeof(apkt));
    apkt.type = MSG_ATTACH;
    apkt.u.i = 0;
    apkt.len = sizeof(apkt.u.i);
    ASSERT_EQ(send_packet(cfd, &apkt), 0);

    Packet rpkt;
    memset(&rpkt, 0, sizeof(rpkt));
    ASSERT_EQ(recv_packet(afd, &rpkt), 0);
    ASSERT_EQ(rpkt.type, MSG_ATTACH);

    /* Client detaches */
    Packet dpkt;
    memset(&dpkt, 0, sizeof(dpkt));
    dpkt.type = MSG_DETACH;
    dpkt.len = 0;
    ASSERT_EQ(send_packet(cfd, &dpkt), 0);

    memset(&rpkt, 0, sizeof(rpkt));
    ASSERT_EQ(recv_packet(afd, &rpkt), 0);
    ASSERT_EQ(rpkt.type, MSG_DETACH);

    close(afd);
    close(cfd);
    close(sfd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

/* ---- Multiple client management ---- */

TEST(multi_client, two_clients_connect) {
    const char *name = "/tmp/qe-test-multi-client";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);

    /* Two clients connect */
    int cfd1 = session_connect(name);
    ASSERT_TRUE(cfd1 >= 0);
    int afd1 = accept(sfd, NULL, NULL);
    ASSERT_TRUE(afd1 >= 0);

    int cfd2 = session_connect(name);
    ASSERT_TRUE(cfd2 >= 0);
    int afd2 = accept(sfd, NULL, NULL);
    ASSERT_TRUE(afd2 >= 0);

    /* Both can exchange packets independently */
    Packet pkt1;
    memset(&pkt1, 0, sizeof(pkt1));
    pkt1.type = MSG_ATTACH;
    pkt1.u.i = 0;
    pkt1.len = sizeof(pkt1.u.i);
    ASSERT_EQ(send_packet(cfd1, &pkt1), 0);

    Packet pkt2;
    memset(&pkt2, 0, sizeof(pkt2));
    pkt2.type = MSG_ATTACH;
    pkt2.u.i = CLIENT_READONLY;
    pkt2.len = sizeof(pkt2.u.i);
    ASSERT_EQ(send_packet(cfd2, &pkt2), 0);

    /* Server receives from client 1 */
    Packet in1;
    memset(&in1, 0, sizeof(in1));
    ASSERT_EQ(recv_packet(afd1, &in1), 0);
    ASSERT_EQ(in1.type, MSG_ATTACH);
    ASSERT_EQ(in1.u.i, 0u);

    /* Server receives from client 2 */
    Packet in2;
    memset(&in2, 0, sizeof(in2));
    ASSERT_EQ(recv_packet(afd2, &in2), 0);
    ASSERT_EQ(in2.type, MSG_ATTACH);
    ASSERT_EQ(in2.u.i, (uint32_t)CLIENT_READONLY);

    /* First client detaches, second stays */
    Packet dpkt;
    memset(&dpkt, 0, sizeof(dpkt));
    dpkt.type = MSG_DETACH;
    dpkt.len = 0;
    ASSERT_EQ(send_packet(cfd1, &dpkt), 0);

    Packet din;
    memset(&din, 0, sizeof(din));
    ASSERT_EQ(recv_packet(afd1, &din), 0);
    ASSERT_EQ(din.type, MSG_DETACH);

    /* Second client can still communicate */
    Packet cpkt;
    memset(&cpkt, 0, sizeof(cpkt));
    cpkt.type = MSG_CONTENT;
    cpkt.len = 3;
    memcpy(cpkt.u.msg, "hi!", 3);
    ASSERT_EQ(send_packet(cfd2, &cpkt), 0);

    Packet cin;
    memset(&cin, 0, sizeof(cin));
    ASSERT_EQ(recv_packet(afd2, &cin), 0);
    ASSERT_EQ(cin.type, MSG_CONTENT);
    ASSERT_MEMEQ(cin.u.msg, "hi!", 3);

    close(afd1);
    close(afd2);
    close(cfd1);
    close(cfd2);
    close(sfd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

/* ---- Client linked list management ---- */

TEST(client_list, add_remove) {
    /* Test the linked list operations used by the server */
    Client *list = NULL;

    Client *c1 = client_new(10);
    ASSERT_TRUE(c1 != NULL);
    c1->next = list;
    list = c1;

    Client *c2 = client_new(20);
    ASSERT_TRUE(c2 != NULL);
    c2->next = list;
    list = c2;

    Client *c3 = client_new(30);
    ASSERT_TRUE(c3 != NULL);
    c3->next = list;
    list = c3;

    /* List is: c3 -> c2 -> c1 */
    ASSERT_EQ(list->socket, 30);
    ASSERT_EQ(list->next->socket, 20);
    ASSERT_EQ(list->next->next->socket, 10);
    ASSERT_TRUE(list->next->next->next == NULL);

    /* Remove middle element (c2) - simulating detach */
    c2->state = STATE_DISCONNECTED;
    Client **prev = &list;
    Client *cur = list;
    while (cur) {
        if (cur->state == STATE_DISCONNECTED) {
            *prev = cur->next;
            cur->socket = -1;
            client_free(cur);
            cur = *prev;
            continue;
        }
        prev = &cur->next;
        cur = cur->next;
    }

    /* List is: c3 -> c1 */
    ASSERT_EQ(list->socket, 30);
    ASSERT_EQ(list->next->socket, 10);
    ASSERT_TRUE(list->next->next == NULL);

    /* Cleanup */
    c1->socket = -1;
    c3->socket = -1;
    client_free(c1);
    client_free(c3);
}

/* ---- Socket permission marking (session list status) ---- */

TEST(mark_socket, exec_bits) {
    const char *name = "/tmp/qe-test-mark-socket";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);

    struct stat sb;

    /* Mark user-exec (clients attached) */
    server_mark_socket_exec(1, 1);
    ASSERT_EQ(stat(name, &sb), 0);
    ASSERT_TRUE(sb.st_mode & S_IXUSR);

    /* Clear user-exec (no clients) */
    server_mark_socket_exec(0, 1);
    ASSERT_EQ(stat(name, &sb), 0);
    ASSERT_FALSE(sb.st_mode & S_IXUSR);

    /* Mark group-exec (process terminated) */
    server_mark_socket_exec(1, 0);
    ASSERT_EQ(stat(name, &sb), 0);
    ASSERT_TRUE(sb.st_mode & S_IXGRP);

    /* Clear group-exec */
    server_mark_socket_exec(0, 0);
    ASSERT_EQ(stat(name, &sb), 0);
    ASSERT_FALSE(sb.st_mode & S_IXGRP);

    close(sfd);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

/* ---- qe_session_list ---- */

TEST(list, finds_sockets) {
    /* Create a socket in the session directory and verify list finds it */
    char dir[256];
    ASSERT_EQ(qe_session_get_dir(dir, sizeof(dir)), 0);

    char sock_path[512];
    snprintf(sock_path, sizeof(sock_path), "%s/test-list-session", dir);
    unlink(sock_path);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    int sfd = server_create_socket(sock_path);
    ASSERT_TRUE(sfd >= 0);

    /* qe_session_list prints to stdout; just verify it returns 0 */
    ASSERT_EQ(qe_session_list(), 0);

    close(sfd);
    unlink(sock_path);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

TEST(list, empty_directory) {
    /* qe_session_list should succeed even with no sessions */
    /* Clean any test sockets first */
    char dir[256];
    ASSERT_EQ(qe_session_get_dir(dir, sizeof(dir)), 0);

    /* Just verify it returns 0 (prints "(no active sessions)") */
    /* Note: there may be other sockets present, so just check return value */
    ASSERT_EQ(qe_session_list(), 0);
}

/* ---- qe_session_handle ---- */

TEST(handle, action_none_returns_minus_one) {
    ASSERT_EQ(qe_session_handle(SESSION_ACTION_NONE, NULL, 0, NULL), -1);
}

TEST(handle, action_list_returns_zero) {
    ASSERT_EQ(qe_session_handle(SESSION_ACTION_LIST, NULL, 0, NULL), 0);
}

TEST(handle, attach_no_name_returns_error) {
    ASSERT_EQ(qe_session_handle(SESSION_ACTION_ATTACH, NULL, 0, NULL), 1);
}

TEST(handle, attach_empty_name_returns_error) {
    ASSERT_EQ(qe_session_handle(SESSION_ACTION_ATTACH, "", 0, NULL), 1);
}

TEST(handle, create_no_name_returns_error) {
    ASSERT_EQ(qe_session_handle(SESSION_ACTION_CREATE, NULL, 0, NULL), 1);
}

/* ---- MSG_EXIT packet ---- */

TEST(exit_pkt, send_recv_exit_status) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_EXIT;
    out.u.i = 42;
    out.len = sizeof(out.u.i);
    ASSERT_EQ(send_packet(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_EXIT);
    ASSERT_EQ(in.u.i, 42u);

    close(fds[0]);
    close(fds[1]);
}

/* ---- Stale socket cleanup ---- */

TEST(socket, stale_socket_cleanup_on_create) {
    /* server_create_socket should remove stale sockets and succeed */
    const char *name = "/tmp/qe-test-stale-socket";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    /* Create first socket */
    int sfd1 = server_create_socket(name);
    ASSERT_TRUE(sfd1 >= 0);
    close(sfd1);
    /* Socket file still exists but nothing is listening */

    /* Creating again should succeed (stale cleanup) */
    int sfd2 = server_create_socket(name);
    ASSERT_TRUE(sfd2 >= 0);

    close(sfd2);
    unlink(name);
    memcpy(server.socket_path, saved_path, sizeof(saved_path));
}

TEST(socket, stale_socket_cleanup_on_connect) {
    /* session_connect should clean up stale socket on ECONNREFUSED */
    const char *name = "/tmp/qe-test-stale-connect";
    unlink(name);

    char saved_path[256];
    memcpy(saved_path, server.socket_path, sizeof(saved_path));

    /* Create and immediately close to leave a stale socket */
    int sfd = server_create_socket(name);
    ASSERT_TRUE(sfd >= 0);
    close(sfd);

    struct stat sb;
    ASSERT_EQ(stat(name, &sb), 0);
    ASSERT_TRUE(S_ISSOCK(sb.st_mode));

    /* Connect should fail and clean up the stale socket */
    int cfd = session_connect(name);
    ASSERT_TRUE(cfd < 0);

    /* Stale socket should have been removed */
    ASSERT_TRUE(stat(name, &sb) == -1);

    memcpy(server.socket_path, saved_path, sizeof(saved_path));
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
