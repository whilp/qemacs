/*
 * Tests for session.c - packet protocol, socket paths, and session lifecycle
 */

#define _GNU_SOURCE
#include "testlib.h"

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

/*------------------------------------------------------------------------
 * Integration tests: session detach and reattach lifecycle
 *
 * These tests fork a real server process with a PTY running 'cat',
 * then connect clients to verify detach/reattach behavior.
 *------------------------------------------------------------------------*/

/* Receive a packet with timeout (milliseconds). Returns 0 on success, -1 on timeout/error. */
static int recv_with_timeout(int fd, Packet *pkt, int timeout_ms) {
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
        return -1;
    return recv_packet(fd, pkt);
}

/* Start a server process with 'cat' running in a PTY.
 * Returns server PID on success, -1 on failure. */
static pid_t start_test_server(const char *sock_path) {
    SessionServer saved = server;
    memset(&server, 0, sizeof(server));
    server.running = 1;
    server.exit_status = -1;

    server.socket = server_create_socket(sock_path);
    if (server.socket < 0) {
        server = saved;
        return -1;
    }

    int ready_pipe[2];
    if (pipe(ready_pipe) < 0) {
        close(server.socket);
        server = saved;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(server.socket);
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        server = saved;
        return -1;
    }

    if (pid == 0) {
        /* Child: become the server */
        close(ready_pipe[0]);

        struct sigaction sa;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = server_sigchld_handler;
        sigaction(SIGCHLD, &sa, NULL);

        struct winsize ws;
        memset(&ws, 0, sizeof(ws));
        ws.ws_row = 25;
        ws.ws_col = 80;
        server.winsize = ws;

        {
        char pty_name[256];
        server.pid = forkpty(&server.pty, pty_name, NULL, &ws);
        }
        if (server.pid == 0) {
            /* Grandchild: run cat */
            close(ready_pipe[1]);
            close(server.socket);
            execlp("cat", "cat", (char *)NULL);
            _exit(1);
        }
        if (server.pid < 0) {
            close(ready_pipe[1]);
            _exit(1);
        }

        sa.sa_handler = server_sigterm_handler;
        sigaction(SIGTERM, &sa, NULL);
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, NULL);
        sigaction(SIGINT, &sa, NULL);

        /* Signal parent we're ready */
        char r = 'R';
        (void)write(ready_pipe[1], &r, 1);
        close(ready_pipe[1]);

        server_mainloop();
        kill(server.pid, SIGKILL);
        waitpid(server.pid, NULL, 0);
        _exit(0);
    }

    /* Parent: close our copy of the listening socket */
    close(server.socket);
    close(ready_pipe[1]);

    /* Wait for server child to signal readiness */
    char r;
    (void)read(ready_pipe[0], &r, 1);
    close(ready_pipe[0]);

    /* Give PTY time to initialize */
    usleep(100000);

    server = saved;
    return pid;
}

static void stop_test_server(pid_t pid, const char *sock_path) {
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    unlink(sock_path);
}

/* Drain any pending packets (e.g. MSG_PID on connect, PTY initial output) */
static void drain_packets(int fd, int timeout_ms) {
    Packet pkt;
    while (recv_with_timeout(fd, &pkt, timeout_ms) == 0)
        ;
}

/* Send MSG_ATTACH to server */
static int send_attach(int fd, int flags) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_ATTACH;
    pkt.u.i = flags;
    pkt.len = sizeof(pkt.u.i);
    return send_packet(fd, &pkt);
}

/* Send MSG_DETACH to server */
static int send_detach(int fd) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_DETACH;
    pkt.len = 0;
    return send_packet(fd, &pkt);
}

/* Send MSG_CONTENT to server */
static int send_content(int fd, const char *data, size_t len) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = MSG_CONTENT;
    pkt.len = len;
    memcpy(pkt.u.msg, data, len);
    return send_packet(fd, &pkt);
}

/* Wait for MSG_CONTENT from server. Returns 1 if received, 0 if timeout. */
static int wait_for_content(int fd, int timeout_ms) {
    Packet pkt;
    int elapsed = 0;
    int step = 100;
    while (elapsed < timeout_ms) {
        if (recv_with_timeout(fd, &pkt, step) == 0 &&
            pkt.type == MSG_CONTENT && pkt.len > 0)
            return 1;
        elapsed += step;
    }
    return 0;
}

/* ---- Detach and reattach: the core lifecycle test ---- */

TEST(session_lifecycle, detach_and_reattach) {
    const char *sock = "/tmp/qe-test-lifecycle-dar";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* --- First client: attach, exchange data, detach --- */
    int cfd1 = session_connect(sock);
    ASSERT_TRUE(cfd1 >= 0);

    /* Drain the MSG_PID packet and any initial PTY output */
    drain_packets(cfd1, 200);

    /* Attach */
    ASSERT_EQ(send_attach(cfd1, 0), 0);

    /* Send data to cat via PTY */
    ASSERT_EQ(send_content(cfd1, "ping1\n", 6), 0);

    /* Verify we get content back (PTY echo and/or cat output) */
    ASSERT_TRUE(wait_for_content(cfd1, 2000));

    /* Detach */
    ASSERT_EQ(send_detach(cfd1), 0);
    close(cfd1);

    /* Give server time to process the detach */
    usleep(100000);

    /* --- Second client: reattach to the same session --- */
    int cfd2 = session_connect(sock);
    ASSERT_TRUE(cfd2 >= 0);  /* Server survived the detach */

    drain_packets(cfd2, 200);

    /* Attach again */
    ASSERT_EQ(send_attach(cfd2, 0), 0);

    /* Send different data */
    ASSERT_EQ(send_content(cfd2, "ping2\n", 6), 0);

    /* Verify data flows through the same cat process */
    ASSERT_TRUE(wait_for_content(cfd2, 2000));

    close(cfd2);
    stop_test_server(srv, sock);
}

/* ---- Server survives abrupt client disconnect (no MSG_DETACH) ---- */

TEST(session_lifecycle, server_survives_disconnect) {
    const char *sock = "/tmp/qe-test-lifecycle-disc";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* Client connects, attaches, then abruptly closes socket */
    int cfd1 = session_connect(sock);
    ASSERT_TRUE(cfd1 >= 0);
    drain_packets(cfd1, 200);
    ASSERT_EQ(send_attach(cfd1, 0), 0);
    ASSERT_EQ(send_content(cfd1, "test\n", 5), 0);
    ASSERT_TRUE(wait_for_content(cfd1, 2000));

    /* Abrupt disconnect: close without sending MSG_DETACH */
    close(cfd1);

    usleep(200000);

    /* New client can still connect */
    int cfd2 = session_connect(sock);
    ASSERT_TRUE(cfd2 >= 0);
    drain_packets(cfd2, 200);
    ASSERT_EQ(send_attach(cfd2, 0), 0);
    ASSERT_EQ(send_content(cfd2, "alive\n", 6), 0);
    ASSERT_TRUE(wait_for_content(cfd2, 2000));

    close(cfd2);
    stop_test_server(srv, sock);
}

/* ---- Multiple detach/reattach cycles ---- */

TEST(session_lifecycle, multiple_reattach_cycles) {
    const char *sock = "/tmp/qe-test-lifecycle-multi";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* Perform 3 attach/detach cycles on the same session */
    int i;
    for (i = 0; i < 3; i++) {
        int cfd = session_connect(sock);
        ASSERT_TRUE(cfd >= 0);
        drain_packets(cfd, 200);

        ASSERT_EQ(send_attach(cfd, 0), 0);

        char msg[32];
        int len = snprintf(msg, sizeof(msg), "cycle%d\n", i);
        ASSERT_EQ(send_content(cfd, msg, len), 0);
        ASSERT_TRUE(wait_for_content(cfd, 2000));

        ASSERT_EQ(send_detach(cfd), 0);
        close(cfd);

        usleep(100000);
    }

    /* Final attach to verify session is still alive */
    int cfd = session_connect(sock);
    ASSERT_TRUE(cfd >= 0);
    drain_packets(cfd, 200);
    ASSERT_EQ(send_attach(cfd, 0), 0);
    ASSERT_EQ(send_content(cfd, "final\n", 6), 0);
    ASSERT_TRUE(wait_for_content(cfd, 2000));

    close(cfd);
    stop_test_server(srv, sock);
}

/* ---- Process PID is preserved across reattach ---- */

TEST(session_lifecycle, same_pid_after_reattach) {
    const char *sock = "/tmp/qe-test-lifecycle-pid";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* First client: capture server PID from MSG_PID packet */
    int cfd1 = session_connect(sock);
    ASSERT_TRUE(cfd1 >= 0);

    Packet pkt;
    ASSERT_EQ(recv_with_timeout(cfd1, &pkt, 1000), 0);
    ASSERT_EQ(pkt.type, MSG_PID);
    uint64_t pid1 = pkt.u.l;
    ASSERT_TRUE(pid1 > 0);

    ASSERT_EQ(send_attach(cfd1, 0), 0);
    ASSERT_EQ(send_detach(cfd1), 0);
    close(cfd1);

    usleep(100000);

    /* Second client: verify same PID */
    int cfd2 = session_connect(sock);
    ASSERT_TRUE(cfd2 >= 0);

    ASSERT_EQ(recv_with_timeout(cfd2, &pkt, 1000), 0);
    ASSERT_EQ(pkt.type, MSG_PID);
    uint64_t pid2 = pkt.u.l;

    /* Same server process answers both clients */
    ASSERT_EQ(pid1, pid2);

    close(cfd2);
    stop_test_server(srv, sock);
}

/* ---- Two clients attached simultaneously, one detaches ---- */

TEST(session_lifecycle, concurrent_clients_one_detaches) {
    const char *sock = "/tmp/qe-test-lifecycle-conc";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* Client 1 connects and attaches */
    int cfd1 = session_connect(sock);
    ASSERT_TRUE(cfd1 >= 0);
    drain_packets(cfd1, 200);
    ASSERT_EQ(send_attach(cfd1, 0), 0);

    /* Client 2 connects and attaches */
    int cfd2 = session_connect(sock);
    ASSERT_TRUE(cfd2 >= 0);
    drain_packets(cfd2, 200);
    ASSERT_EQ(send_attach(cfd2, CLIENT_READONLY), 0);

    /* Client 1 sends data */
    ASSERT_EQ(send_content(cfd1, "shared\n", 7), 0);

    /* Both clients should receive content */
    ASSERT_TRUE(wait_for_content(cfd1, 2000));
    ASSERT_TRUE(wait_for_content(cfd2, 2000));

    /* Client 1 detaches */
    ASSERT_EQ(send_detach(cfd1), 0);
    close(cfd1);

    usleep(100000);

    /* Client 2 is still connected; a new client can also attach */
    int cfd3 = session_connect(sock);
    ASSERT_TRUE(cfd3 >= 0);
    drain_packets(cfd3, 200);
    ASSERT_EQ(send_attach(cfd3, 0), 0);

    ASSERT_EQ(send_content(cfd3, "after\n", 6), 0);
    ASSERT_TRUE(wait_for_content(cfd3, 2000));

    close(cfd2);
    close(cfd3);
    stop_test_server(srv, sock);
}

/* ---- Regression tests for busy-loop bugs ---- */

/*
 * Helper: read utime + stime (in clock ticks) for a given pid from
 * /proc/pid/stat.  Returns 0 on success, -1 on failure.
 */
static int get_cpu_ticks(pid_t pid, unsigned long *ticks) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char buf[1024];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    fclose(f);
    /* Fields: pid (comm) state ppid ... field14=utime field15=stime */
    /* Skip past the (comm) field which may contain spaces */
    char *p = strrchr(buf, ')');
    if (!p) return -1;
    p++;  /* past ')' */
    unsigned long utime = 0, stime = 0;
    /* After ')' we need fields: state(3) ppid(4) pgrp(5) session(6)
     * tty_nr(7) tpgid(8) flags(9) minflt(10) cminflt(11) majflt(12)
     * cmajflt(13) utime(14) stime(15) — that's 13 fields after ')' */
    char state;
    long dummy;
    if (sscanf(p, " %c %ld %ld %ld %ld %ld %lu %lu %lu %lu %lu %lu %lu",
               &state, &dummy, &dummy, &dummy, &dummy, &dummy,
               (unsigned long *)&dummy, (unsigned long *)&dummy,
               (unsigned long *)&dummy, (unsigned long *)&dummy,
               (unsigned long *)&dummy, &utime, &stime) < 13)
        return -1;
    *ticks = utime + stime;
    return 0;
}

/*
 * Regression: server must not busy-loop after abrupt client disconnect.
 *
 * Before the fix, recv_packet failure on a dead client socket was ignored,
 * leaving the socket in the select set.  select() returned immediately
 * (EOF readable), causing 100% CPU spin.
 */
TEST(busyloop, abrupt_disconnect_no_cpu_spin) {
    const char *sock = "/tmp/qe-test-busyloop-disc";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* Client connects and attaches */
    int cfd = session_connect(sock);
    ASSERT_TRUE(cfd >= 0);
    drain_packets(cfd, 200);
    ASSERT_EQ(send_attach(cfd, 0), 0);
    ASSERT_EQ(send_content(cfd, "hi\n", 3), 0);
    ASSERT_TRUE(wait_for_content(cfd, 2000));

    /* Abrupt disconnect: close without MSG_DETACH */
    close(cfd);

    /* Let server notice the disconnect and (hopefully) clean up */
    usleep(300000);

    /* Measure CPU usage over a 1-second window */
    unsigned long ticks_before = 0, ticks_after = 0;
    ASSERT_EQ(get_cpu_ticks(srv, &ticks_before), 0);
    usleep(1000000);  /* 1 second */
    ASSERT_EQ(get_cpu_ticks(srv, &ticks_after), 0);

    unsigned long ticks_used = ticks_after - ticks_before;
    long tps = sysconf(_SC_CLK_TCK);
    if (tps <= 0) tps = 100;

    /*
     * A well-behaved idle server should use near-zero CPU.
     * Allow up to 10% of one core (0.1s of CPU per 1s wall).
     * A busy-loop would use ~100% (== tps ticks per second).
     */
    unsigned long max_ticks = (unsigned long)tps / 10;
    if (ticks_used > max_ticks) {
        fprintf(stderr, "  FAIL: server used %lu ticks in 1s (max %lu, tps=%ld)\n",
                ticks_used, max_ticks, tps);
    }
    ASSERT_TRUE(ticks_used <= max_ticks);

    /* Verify server is still functional */
    int cfd2 = session_connect(sock);
    ASSERT_TRUE(cfd2 >= 0);
    drain_packets(cfd2, 200);
    ASSERT_EQ(send_attach(cfd2, 0), 0);
    ASSERT_EQ(send_content(cfd2, "ok\n", 3), 0);
    ASSERT_TRUE(wait_for_content(cfd2, 2000));
    close(cfd2);

    stop_test_server(srv, sock);
}

/*
 * Regression: server must not busy-loop when multiple clients disconnect
 * abruptly in sequence.
 */
TEST(busyloop, multiple_abrupt_disconnects) {
    const char *sock = "/tmp/qe-test-busyloop-multi";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* Connect and abruptly disconnect 3 clients */
    int i;
    for (i = 0; i < 3; i++) {
        int cfd = session_connect(sock);
        ASSERT_TRUE(cfd >= 0);
        drain_packets(cfd, 200);
        ASSERT_EQ(send_attach(cfd, 0), 0);
        ASSERT_EQ(send_content(cfd, "x\n", 2), 0);
        ASSERT_TRUE(wait_for_content(cfd, 2000));
        close(cfd);  /* abrupt disconnect */
        usleep(200000);
    }

    /* Measure CPU after all disconnects */
    unsigned long ticks_before = 0, ticks_after = 0;
    ASSERT_EQ(get_cpu_ticks(srv, &ticks_before), 0);
    usleep(1000000);
    ASSERT_EQ(get_cpu_ticks(srv, &ticks_after), 0);

    unsigned long ticks_used = ticks_after - ticks_before;
    long tps = sysconf(_SC_CLK_TCK);
    if (tps <= 0) tps = 100;
    unsigned long max_ticks = (unsigned long)tps / 10;

    if (ticks_used > max_ticks) {
        fprintf(stderr, "  FAIL: server used %lu ticks in 1s after 3 disconnects "
                "(max %lu)\n", ticks_used, max_ticks);
    }
    ASSERT_TRUE(ticks_used <= max_ticks);

    stop_test_server(srv, sock);
}

/*
 * Regression: two concurrent clients, one disconnects abruptly.
 * Server must not spin and the remaining client must still work.
 */
TEST(busyloop, concurrent_one_abrupt_disconnect) {
    const char *sock = "/tmp/qe-test-busyloop-conc";
    unlink(sock);

    pid_t srv = start_test_server(sock);
    ASSERT_TRUE(srv > 0);

    /* Client 1 connects */
    int cfd1 = session_connect(sock);
    ASSERT_TRUE(cfd1 >= 0);
    drain_packets(cfd1, 200);
    ASSERT_EQ(send_attach(cfd1, 0), 0);

    /* Client 2 connects */
    int cfd2 = session_connect(sock);
    ASSERT_TRUE(cfd2 >= 0);
    drain_packets(cfd2, 200);
    ASSERT_EQ(send_attach(cfd2, CLIENT_READONLY), 0);

    /* Both exchange data */
    ASSERT_EQ(send_content(cfd1, "both\n", 5), 0);
    ASSERT_TRUE(wait_for_content(cfd1, 2000));
    ASSERT_TRUE(wait_for_content(cfd2, 2000));

    /* Client 1 disconnects abruptly */
    close(cfd1);
    usleep(300000);

    /* Measure server CPU */
    unsigned long ticks_before = 0, ticks_after = 0;
    ASSERT_EQ(get_cpu_ticks(srv, &ticks_before), 0);
    usleep(1000000);
    ASSERT_EQ(get_cpu_ticks(srv, &ticks_after), 0);

    unsigned long ticks_used = ticks_after - ticks_before;
    long tps = sysconf(_SC_CLK_TCK);
    if (tps <= 0) tps = 100;
    unsigned long max_ticks = (unsigned long)tps / 10;

    ASSERT_TRUE(ticks_used <= max_ticks);

    /* Client 2 still works */
    close(cfd2);
    int cfd3 = session_connect(sock);
    ASSERT_TRUE(cfd3 >= 0);
    drain_packets(cfd3, 200);
    ASSERT_EQ(send_attach(cfd3, 0), 0);
    ASSERT_EQ(send_content(cfd3, "ok\n", 3), 0);
    ASSERT_TRUE(wait_for_content(cfd3, 2000));
    close(cfd3);

    stop_test_server(srv, sock);
}

/* ---- Unit tests for read_all / write_all edge cases ---- */

TEST(io, read_all_eof_returns_partial) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* Write 3 bytes, then close sender */
    ASSERT_EQ(write_all(fds[0], "abc", 3), 3);
    close(fds[0]);

    /* read_all asking for 8 bytes should return 3 (partial read before EOF) */
    char buf[8];
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(read_all(fds[1], buf, 8), 3);
    ASSERT_MEMEQ(buf, "abc", 3);

    close(fds[1]);
}

TEST(io, read_all_immediate_eof) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    close(fds[0]);  /* close writer immediately */

    char buf[8];
    memset(buf, 0, sizeof(buf));
    /* Should return 0 (no data read before EOF) */
    ASSERT_EQ(read_all(fds[1], buf, 8), 0);

    close(fds[1]);
}

TEST(io, write_all_to_closed_pipe) {
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    close(fds[1]);  /* close reader */

    /* Writing to a closed socket should fail (SIGPIPE ignored, returns -1) */
    struct sigaction sa, old_sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, &old_sa);

    ssize_t ret = write_all(fds[0], "data", 4);
    ASSERT_TRUE(ret == -1);

    sigaction(SIGPIPE, &old_sa, NULL);
    close(fds[0]);
}

/* ---- write_all_timeout tests ---- */

TEST(io, write_all_timeout_basic) {
    /* write_all_timeout with generous timeout should succeed normally */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    ssize_t ret = write_all_timeout(fds[0], "hello", 5, 5000);
    ASSERT_EQ(ret, 5);

    char buf[8];
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(read_all(fds[1], buf, 5), 5);
    ASSERT_MEMEQ(buf, "hello", 5);

    close(fds[0]);
    close(fds[1]);
}

TEST(io, write_all_timeout_to_closed_pipe) {
    /* write_all_timeout to a closed socket should return -1, not hang */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    close(fds[1]);  /* close reader */

    struct sigaction sa, old_sa;
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, &old_sa);

    ssize_t ret = write_all_timeout(fds[0], "data", 4, 1000);
    ASSERT_TRUE(ret == -1);

    sigaction(SIGPIPE, &old_sa, NULL);
    close(fds[0]);
}

TEST(io, write_all_timeout_returns_on_full_buffer) {
    /* When the socket buffer is full and the reader is gone,
     * write_all_timeout must return -1 within the timeout period,
     * NOT block forever like the old write_all did.
     * This is the core test for issue #47. */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* Set non-blocking so writes return EAGAIN when buffer full */
    set_socket_non_blocking(fds[0]);

    /* Fill the send buffer */
    char big[4096];
    memset(big, 'x', sizeof(big));
    for (int i = 0; i < 1000; i++) {
        ssize_t w = write(fds[0], big, sizeof(big));
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
    }

    /* Now write_all_timeout with a short timeout should fail, not block */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ssize_t ret = write_all_timeout(fds[0], "more", 4, 200);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    /* Must return failure (-1), indicating timeout/error */
    ASSERT_TRUE(ret == -1);
    /* Must return within reasonable time (not infinite) */
    ASSERT_TRUE(elapsed_ms < 5000);

    close(fds[0]);
    close(fds[1]);
}

TEST(io, write_all_timeout_zero_means_nonblocking) {
    /* timeout_ms=0 should mean pure non-blocking: no poll at all */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    set_socket_non_blocking(fds[0]);

    /* Fill the send buffer */
    char big[4096];
    memset(big, 'x', sizeof(big));
    for (int i = 0; i < 1000; i++) {
        ssize_t w = write(fds[0], big, sizeof(big));
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
    }

    /* With timeout=0, should fail immediately on full buffer */
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    ssize_t ret = write_all_timeout(fds[0], "data", 4, 0);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    ASSERT_TRUE(ret == -1);
    ASSERT_TRUE(elapsed_ms < 100);  /* should be nearly instant */

    close(fds[0]);
    close(fds[1]);
}

/* ---- send_packet_nonblock tests ---- */

TEST(io, send_packet_nonblock_basic) {
    /* send_packet_nonblock should succeed on a writable socket */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    set_socket_non_blocking(fds[0]);

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_CONTENT;
    out.len = 5;
    memcpy(out.u.msg, "hello", 5);
    ASSERT_EQ(send_packet_nonblock(fds[0], &out), 0);

    Packet in;
    memset(&in, 0, sizeof(in));
    ASSERT_EQ(recv_packet(fds[1], &in), 0);
    ASSERT_EQ(in.type, MSG_CONTENT);
    ASSERT_EQ(in.len, 5);
    ASSERT_MEMEQ(in.u.msg, "hello", 5);

    close(fds[0]);
    close(fds[1]);
}

TEST(io, send_packet_nonblock_full_buffer) {
    /* send_packet_nonblock must return -1 when buffer is full, not block */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
    set_socket_non_blocking(fds[0]);

    /* Fill the send buffer */
    char big[4096];
    memset(big, 'x', sizeof(big));
    for (int i = 0; i < 1000; i++) {
        ssize_t w = write(fds[0], big, sizeof(big));
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
    }

    Packet out;
    memset(&out, 0, sizeof(out));
    out.type = MSG_CONTENT;
    out.len = 100;
    memset(out.u.msg, 'z', 100);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int ret = send_packet_nonblock(fds[0], &out);
    clock_gettime(CLOCK_MONOTONIC, &end);

    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    ASSERT_EQ(ret, -1);
    ASSERT_TRUE(elapsed_ms < 100);  /* must not block */

    close(fds[0]);
    close(fds[1]);
}

TEST(io, recv_packet_on_half_closed_socket) {
    /* This directly tests the root cause of the busy-loop bug:
     * recv_packet on a socket whose peer has closed must return -1 */
    int fds[2];
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    close(fds[0]);  /* simulate abrupt client disconnect */

    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    ASSERT_EQ(recv_packet(fds[1], &pkt), -1);

    /* Verify the socket is reported as readable by select (EOF condition) */
    fd_set rfds;
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    FD_ZERO(&rfds);
    FD_SET(fds[1], &rfds);
    int ret = select(fds[1] + 1, &rfds, NULL, NULL, &tv);
    ASSERT_TRUE(ret > 0);  /* EOF makes socket readable */
    ASSERT_TRUE(FD_ISSET(fds[1], &rfds));

    /* Second recv_packet must also return -1 (not hang or succeed) */
    ASSERT_EQ(recv_packet(fds[1], &pkt), -1);

    close(fds[1]);
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
