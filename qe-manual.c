/*
 * QEmacs, tiny but powerful multimode editor
 *
 * Copyright (c) 2000-2002 Fabrice Bellard.
 * Copyright (c) 2000-2024 Charlie Gordon.
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

/*
   This file contains the main framework for the Quick Emacs manual.

   The manual is composed by scandoc from all source files including this one.
   The sections are extracted from scandoc comments, multiline comments
   starting with a @ and an ordering string. All sections are concatenated
   in natural order of the ordering strings.
 */

/*@ INTRO=0 - introduction
   # QEmacs User Guide

   # Introduction

   Welcome to QEmacs! A small but powerful text editor with many features
   that even big editors lack.

   ## Quick Description

   QEmacs is a small text editor targeted at embedded systems or debugging.
   This fork builds with cosmocc to produce Actually Portable Executables
   (APE) that run on Linux, macOS, Windows, and BSDs from a single binary.

   Although it is very small, it has some very interesting features that
   even big editors lack:

   - Full screen editor with an Emacs look and feel with all common Emacs
   features: multi-buffer, multi-window, command mode, universal argument,
   keyboard macros, config file with C-like syntax, minibuffer with
   completion and history.

   - Can edit huge files (hundreds of megabytes) without delay, using a
   highly optimized internal representation and memory mapping for large
   files.

   - Full Unicode support, including multi charset handling
   (8859-x, UTF8, SJIS, EUC-JP, ...) and bidirectional editing respecting
   the Unicode bidi algorithm. Arabic and Indic scripts handling (in
   progress). Automatic end of line detection.

   - C mode: coloring with immediate update, auto-indent, automatic tags.

   - Shell mode: full color VT100 terminal emulation so your shell works
   exactly as you expect. Compile mode with colorized error messages,
   automatic error message parser jumps to next/previous error, works
   with grep too. The shell buffer is a fully functional terminal: you
   can run qemacs, vim or even emacs recursively!

   - Input methods for most languages, including Chinese (input methods
   descriptions come from the Yudit editor).

   - Binary and hexadecimal in place editing mode with insertion and
   block commands. Unicode hexa editing of UTF-8 files also supported.
   Can patch binary files, preserving every byte outside the modified
   areas.

   - Works on any VT100 terminal without termcap. UTF-8 VT100 support
   included with double width glyphs.

   - X11 support. Supports multiple proportional fonts at the same time
   (like XEmacs). X Input methods supported. Xft extension supported for
   anti-aliased font display.

   - Bitmap images are displayed on graphics displays and as ASCII colored text
   on text terminals, which is handy when browsing files over an ssh connection.
   (QEmacs uses the public domain [`stb_image`](https://github.com/nothings/stb/blob/master/stb_image.h)
   package for image parsing.

 */

/*@ CONCEPTS=1 - concepts
   # Concepts

   QEmacs uses an Emacs-style model built around buffers, windows, and modes.
   Text is stored in buffers, displayed through windows, and behavior is
   controlled by modes. Commands are invoked via key bindings or by name
   through the minibuffer (`M-x`).
 */

/*@ BUFFERS=2 - buffers
   # Buffers

   A buffer holds the contents of a file or other text being edited. Each
   buffer has a name (usually the filename) and an associated major mode.
   You can have many buffers open simultaneously and switch between them
   with `C-x b`. List all buffers with `C-x C-b`.

   Buffers use a page-based internal representation that allows efficient
   editing of very large files (hundreds of megabytes) with minimal memory
   overhead.
 */

/*@ WINDOWS=3 - windows
   # Windows

   A window is a view onto a buffer. The QEmacs display can be split into
   multiple windows, each showing a different buffer or a different part of
   the same buffer. Split horizontally with `C-x 2`, vertically with
   `C-x 3`, and close the current window with `C-x 0`. Switch between
   windows with `C-x o`.
 */

/*@ MODES=4 - modes
   # Modes

   A mode defines the behavior of a buffer: syntax highlighting, indentation,
   key bindings, and special commands. QEmacs supports major modes (one per
   buffer) and minor modes (which can be layered on top).

   Built-in modes include:
   - **Text mode** — default for plain text files
   - **C mode** — syntax highlighting and auto-indent for C/C++/Java/etc.
   - **Shell mode** — full VT100 terminal emulator (`M-x shell`)
   - **Dired mode** — directory browser (`C-x C-d`)
   - **Hex mode** — binary/hexadecimal editing (`M-x hex-mode`)
   - **Markdown/Org modes** — structured document editing

   Language syntax modules (56 languages) are in `lang/` and are loaded
   automatically based on file extension and content.
 */

/*@ CMD=5 - commands
   # Commands
 */

/*@ IMPL=6 - implementation
   # Implementation

   QEmacs is implemented in C99 and organized as a core editor with a
   modular system for language support and editor modes. The source is
   structured as follows:

   - **Core** (`qe.c`, `qe.h`) — main loop, command dispatch, key bindings
   - **Buffer management** (`buffer.c`) — page-based buffer representation
     for efficient editing of large files
   - **Display** (`display.c`, `tty.c`) — rendering pipeline and terminal driver
   - **Character sets** (`charset.c`) — encoding/decoding for multiple charsets
   - **Utilities** (`cutils.c`) — string handling, memory management, path operations
   - **Search** (`search.c`) — incremental search, query-replace, regex support
   - **Input** (`input.c`) — key binding resolution and input method support

   Modules use `qe_module_init()` / `qe_module_exit()` macros to register
   themselves at startup. Language modules go in `lang/`, editor modes in `modes/`.
 */

/*@ STRUCT=7 - structures
   # Structures

   The key data structures are:

   - **`EditBuffer`** — represents a text buffer with page-based storage
   - **`EditState`** — per-window editing state (cursor position, display info)
   - **`ModeDef`** — defines a major or minor mode (callbacks, key bindings)
   - **`buf_t`** — fixed-length character array for safe formatted output
   - **`DynBuf`** — dynamically growing byte buffer
 */

/*@ API=8 - functions
   # C functions
 */

/*@ EPILOG=9 - epilog
   ## Building QEmacs

 * Get the source code from [GitHub](https://github.com/whilp/qemacs).
 * Install cosmocc: `make install-cosmocc` (one-time setup).
 * Ensure cosmocc is in your PATH: `export PATH="/opt/cosmocc/bin:$PATH"`
 * Type `make` to compile qemacs and its associated tools.
 * Type `make install` as root to install it in `/usr/local`.

   ## Authors

   QEmacs was started in 2000. The initial version was developped by
   Fabrice Bellard and Charlie Gordon, who since then, has been maintaining
   and extending it.

   ## Licensing

   QEmacs is released under the MIT license.
   (read the accompanying [LICENCE](LICENCE) file).

   ## Contributing to QEmacs

   This fork is maintained at [whilp/qemacs](https://github.com/whilp/qemacs).
   Please file an issue for any questions or feature requests. Pull requests are welcome.

   The upstream QEmacs project is at [qemacs/qemacs](https://github.com/qemacs/qemacs).

 */
