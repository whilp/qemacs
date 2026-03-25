/*
 * Session management for QEmacs - detach/reattach support
 *
 * Inspired by abduco (https://github.com/martanne/abduco)
 * Uses a PTY-proxy architecture: a daemonized server runs qemacs
 * inside a PTY, and thin clients connect via Unix domain sockets
 * to relay terminal I/O.
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

#ifndef QE_SESSION_H
#define QE_SESSION_H

/* Session actions for command-line parsing */
#define SESSION_ACTION_NONE    0
#define SESSION_ACTION_CREATE  'c'  /* -S name: create new session */
#define SESSION_ACTION_ATTACH  'a'  /* -A name: attach to existing session */
#define SESSION_ACTION_CREATE_ATTACH 'C'  /* -R name: create or attach */
#define SESSION_ACTION_LIST    'l'  /* --session-list: list sessions */
#define SESSION_ACTION_DETACH_KEY 'd'

/* Returns 0 if session handling was performed (caller should exit),
 * -1 if no session action was requested (caller should proceed normally) */
int qe_session_handle(int action, const char *session_name,
                      int argc, char **argv, int optind);

/* List active sessions to stdout. Returns 0 on success. */
int qe_session_list(void);

/* Get the session socket directory path. Returns 0 on success. */
int qe_session_get_dir(char *buf, size_t size);

/* OSC escape sequence to trigger detach from within the editor */
#define QE_SESSION_DETACH_SEQ "\033]qe;detach\007"

#endif /* QE_SESSION_H */
