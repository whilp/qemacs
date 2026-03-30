# QEmacs vs GNU Emacs

QEmacs ("Quick Emacs") is a small, self-contained editor inspired by GNU Emacs.
It reproduces many of Emacs's core editing concepts — buffers, windows, modes,
minibuffer, kill ring, incremental search, keyboard macros — but is a
fundamentally different program. This document describes what QEmacs shares with
GNU Emacs, what it omits, and where it diverges.

## No Emacs Lisp

The most consequential difference is the absence of Emacs Lisp (Elisp). GNU
Emacs is, at its core, a Lisp interpreter with an editor built on top. Nearly
everything in GNU Emacs — modes, keybindings, the modeline, the minibuffer,
even basic cursor movement — is implemented in Elisp and can be redefined at
runtime.

QEmacs has none of this. There is no Lisp interpreter. There is no runtime
evaluation of arbitrary code by the user. The implications are far-reaching:

- **No packages.** GNU Emacs has thousands of packages installable via
  `package.el`, MELPA, and ELPA. QEmacs has no package manager and no way to
  install third-party extensions at runtime. All functionality is compiled into
  the binary.
- **No user-defined functions.** In GNU Emacs, users routinely write Elisp
  functions and bind them to keys. QEmacs users cannot define new commands.
- **No hooks.** GNU Emacs exposes hooks (e.g., `after-save-hook`,
  `prog-mode-hook`) that let users run arbitrary code in response to events.
  QEmacs has no hook system.
- **No advice system.** GNU Emacs lets users wrap or replace any function via
  `advice-add`. QEmacs functions are fixed at compile time.
- **No dynamic mode creation.** GNU Emacs modes are Elisp programs. QEmacs
  modes are C modules compiled into the binary.

### What QEmacs offers instead

QEmacs embeds Lua 5.4 as its scripting engine for configuration and plugins.
Configuration files (`~/.qe/config.lua`, `.qerc.lua`) use Lua syntax. The
`qe.*` API provides access to:

- Buffer editing (`qe.insert`, `qe.delete`, `qe.region_text`)
- Cursor navigation (`qe.point`, `qe.goto_line`, `qe.bol`, `qe.eol`)
- Command execution (`qe.call`, `qe.bind`)
- Command registration (`qe.command`)
- Status messages (`qe.message`, `qe.error`)

Lua plugins can define new commands, manipulate buffers, bind keys, and use
the full Lua 5.4 standard library (including `io`, `os`, `string`, `math`).

## Keybindings and commands

QEmacs reproduces the standard Emacs keybinding scheme: `C-x C-f` to find
files, `C-x C-s` to save, `C-x b` to switch buffers, `C-s` for incremental
search, `C-w`/`M-w`/`C-y` for kill/copy/yank, `C-x (`/`C-x )` for keyboard
macros, and so on. Most users with Emacs muscle memory will feel immediately
at home.

Differences to note:

- **The command set is smaller.** GNU Emacs has thousands of interactive
  commands. QEmacs has hundreds. Many specialized Emacs commands (e.g.,
  `query-replace-regexp` variations, `rectangle-mark-mode`, `align-regexp`)
  are absent or simplified.
- **No `M-x` completion frameworks.** GNU Emacs users often rely on Ivy, Helm,
  Vertico, or the built-in `completing-read` with rich annotation. QEmacs has
  basic tab completion in the minibuffer with a popup window, but no fuzzy
  matching, candidate ranking, or annotation.
- **No prefix argument generality.** QEmacs supports `C-u` numeric arguments
  for many commands, but the range of commands that respect them is narrower.

## Buffers and windows

QEmacs replicates the Emacs buffer/window model:

- Multiple buffers, switchable with `C-x b`
- Windows can be split horizontally (`C-x 2`) and vertically (`C-x 3`)
- `C-x o` to cycle between windows
- `C-x 1` to delete other windows, `C-x 0` to delete the current window
- Special buffers like `*scratch*`, `*messages*`, dired, and shell output

Differences:

- **No frames.** GNU Emacs can open multiple OS-level windows (frames). QEmacs
  runs in a single terminal and has no frame concept.
- **No indirect buffers.** GNU Emacs supports multiple buffers sharing the same
  text with independent point and narrowing. QEmacs does not.
- **No narrowing.** GNU Emacs can restrict editing to a region of a buffer
  (`narrow-to-region`). QEmacs does not support this.
- **No buffer-local variables in the Emacs sense.** GNU Emacs lets any
  variable be made buffer-local. QEmacs has per-buffer properties but not
  a general buffer-local variable system.

## Modes

QEmacs has two categories of modes, similar to GNU Emacs's major/minor mode
distinction:

### Language/syntax modes

QEmacs includes syntax highlighting for 56+ languages, covering most popular
languages: C, C++, Python, Rust, Go, JavaScript, TypeScript, Ruby, Haskell,
Lua, OCaml, Erlang, Elixir, Swift, SQL, and many more. These provide:

- Syntax highlighting (keyword, string, comment, etc. coloring)
- Basic indentation support
- Language-aware navigation in some cases

What they do **not** provide compared to GNU Emacs major modes:

- **No LSP integration.** GNU Emacs has `eglot` and `lsp-mode` for
  language-server-backed completion, diagnostics, and refactoring. QEmacs has
  no language server support.
- **No semantic analysis.** Modes are pattern-based colorizers, not parsers.
  They don't build ASTs or understand program structure deeply.
- **No REPL integration.** GNU Emacs modes for Python, Lisp, Haskell, etc.
  can send code to a running interpreter. QEmacs has a shell mode but no
  language-specific REPL support.
- **No code completion.** GNU Emacs has `company-mode`, `corfu`, and built-in
  `completion-at-point`. QEmacs has no in-buffer code completion.
- **No linting or flycheck.** No inline diagnostics or on-the-fly syntax
  checking.

### Editor modes

QEmacs includes several non-language modes:

| Mode | Emacs equivalent | Notes |
|------|-----------------|-------|
| Dired | `dired-mode` | File manager with mark/delete/rename operations |
| Shell | `shell-mode` | Run shell commands in a buffer with terminal emulation |
| Hex | `hexl-mode` | Hex editor for binary files |
| Buffer list | `buffer-menu` | `C-x C-b` buffer listing |
| Compile | `compile-mode` | Run build commands, parse errors, jump to source |
| Markdown | `markdown-mode` | Syntax highlighting plus a rendered preview mode |
| Org | `org-mode` | Basic org-mode syntax support |
| LaTeX | `latex-mode` | LaTeX syntax highlighting |
| HTML | `html-mode` | HTML syntax and optional rendered view |
| Archive | `archive-mode` | Browse tar/zip contents |
| Image | — | View images via `stb_image` (terminal rendering) |

Notable GNU Emacs modes with **no QEmacs equivalent**:

- **Magit / VC mode** — no built-in version control interface
- **Org-mode (full)** — QEmacs has syntax highlighting but not org-babel,
  agenda, capture, export, or any of org-mode's extensive feature set
- **ERC / rcirc** — no IRC client
- **Gnus / mu4e** — no email client
- **TRAMP** — no transparent remote file editing
- **Calc** — no calculator mode
- **Ediff** — no visual diff/merge tool
- **Proced** — no process manager
- **EWW** — no web browser (though QEmacs has an HTML rendering library)
- **Info** — no GNU Info reader
- **Calendar / diary** — no calendar system

## Search and replace

QEmacs has:

- Incremental search (`C-s`, `C-r`) with word-match toggle and yank commands
- Regular expression search (`M-C-s`, `M-C-r`) using a built-in regex engine
- Query replace (`M-%`) and regex query replace
- Compile mode with `next-error` / `previous-error` for jumping to matches

What it lacks compared to GNU Emacs:

- **No `occur` / `multi-occur`** — listing all matches across buffers
- **No `grep-find` / `rgrep`** — recursive project-wide search (though compile
  mode can run `grep` as a shell command)
- **No `xref`** — cross-reference navigation (find definition, find references)
- **No `project.el`** — no project-scoped search, file finding, or compilation

## Undo

QEmacs has a linear undo system (`C-/` or `C-x u`). GNU Emacs has a more
sophisticated undo tree where undo itself can be undone (redo via `C-g` then
undo), preserving the full history of all states. QEmacs provides basic
undo/redo but without the full tree model.

## Configuration

| Aspect | GNU Emacs | QEmacs |
|--------|-----------|--------|
| Config file | `~/.emacs` or `~/.emacs.d/init.el` (Elisp) | `~/.qe/config.lua` (Lua) |
| Config language | Full Emacs Lisp | Lua 5.4 with `qe.*` API |
| Directory-local | `.dir-locals.el` | `.qerc.lua` files in parent directories |
| Customization UI | `M-x customize` GUI | None — edit config files directly |
| Theme system | `load-theme`, `custom-theme` | Built-in color schemes, no theme framework |
| Per-mode config | Mode hooks in Elisp | Limited per-mode variables |

## Display and platform

GNU Emacs runs as a GUI application (GTK, macOS native, Windows native) with
an optional terminal mode (`emacs -nw`). QEmacs in this fork is **terminal
only** — there is no GUI. The trade-offs:

- **No proportional fonts.** GNU Emacs GUI supports variable-width fonts.
  QEmacs uses the terminal's monospace font exclusively.
- **No inline images.** GNU Emacs can display images inline in buffers (useful
  for org-mode, doc-view, image-mode). QEmacs can render images to the terminal
  but only in a dedicated image viewer mode.
- **No mouse-driven UI.** GNU Emacs has menus, toolbars, scrollbars, and
  tooltips. QEmacs has none of these.
- **24-bit color.** QEmacs supports full 24-bit RGB color in terminals that
  support it, matching GNU Emacs's GUI color fidelity.

### Portability

QEmacs compensates with extreme portability. The binary is an Actually Portable
Executable (APE) that runs unmodified on Linux, macOS, Windows, FreeBSD,
OpenBSD, and NetBSD. GNU Emacs requires separate compilation (or separate
packages) for each OS.

## Session management

QEmacs has a built-in session detach/reattach system (similar to tmux/screen)
that GNU Emacs lacks entirely. `qe --session-resume NAME` lets you disconnect
from and reconnect to editing sessions, surviving terminal disconnects. GNU
Emacs users achieve similar functionality through external tools (tmux, screen,
or `emacs --daemon` with `emacsclient`).

## Size and startup

QEmacs is dramatically smaller and faster to start than GNU Emacs:

- **Binary size**: QEmacs is a single binary under 2 MB. GNU Emacs installs
  hundreds of megabytes of Elisp libraries.
- **Startup time**: QEmacs starts essentially instantly. GNU Emacs typically
  takes 0.5-3 seconds (or more with extensive configuration), leading to
  workarounds like `emacs --daemon`.
- **Memory usage**: QEmacs uses a fraction of GNU Emacs's memory footprint.
- **Dependencies**: QEmacs has zero runtime dependencies. GNU Emacs depends on
  a C library, optional GUI toolkits, and its Elisp ecosystem.

## Summary

QEmacs is best understood as a fast, minimal editor that borrows Emacs's
keybindings and buffer/window model but discards its extensibility. If you
primarily need a capable text editor with familiar keybindings that starts
instantly, runs everywhere, and requires no configuration — QEmacs is a
compelling choice. If you need packages, LSP integration, Org-mode, Magit,
or any form of programmatic customization — you need GNU Emacs.

| Feature | GNU Emacs | QEmacs |
|---------|-----------|--------|
| Extension language | Emacs Lisp (full language) | Lua 5.4 (plugins + config) |
| Package ecosystem | Thousands of packages | None |
| Language support | LSP, Tree-sitter, REPL | Syntax highlighting only |
| Version control | Magit, VC, Ediff | None |
| Remote files | TRAMP (SSH, Docker, sudo) | None |
| Terminal multiplexing | Requires tmux/screen or daemon | Built-in sessions |
| Startup time | 0.5-3+ seconds | Instant |
| Binary size | Hundreds of MB installed | < 2 MB single file |
| Platforms (one binary) | One per OS | All major OSes |
| Display | GUI + terminal | Terminal only |
