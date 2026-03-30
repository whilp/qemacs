# QEmacs Lua Plugin System

QEmacs embeds Lua 5.4 for extensibility. Plugins are `.lua` files that use
the `qe` API to register commands, manipulate buffers, and extend the editor.

## Quick Start

Create `~/.qe/hello.lua`:

```lua
qe.command("insert-hello", "C-c h", function()
    qe.insert("Hello from Lua!\n")
    qe.message("Inserted hello")
end)
```

Restart qemacs — the plugin loads automatically. Press `C-c h` to run it.

## Loading Plugins

**Automatic**: All `.lua` files in `~/.qe/` are loaded at startup.

**Manual**: Use `M-x load-plugin` and select a `.lua` file.

**Interactive**: Use `M-x eval-lua` to evaluate Lua expressions directly:
```
Lua: qe.message("it works")
```

Plugins can also `require` other Lua modules from `~/.qe/`:
```lua
local util = require('my_utils')  -- loads ~/.qe/my_utils.lua
```

## API Reference

### Buffer Operations

| Function | Description |
|----------|-------------|
| `qe.insert(text)` | Insert text at cursor position |
| `qe.delete(n)` | Delete `n` bytes forward from cursor |
| `qe.point()` | Return cursor byte offset |
| `qe.set_point(offset)` | Move cursor to byte offset |
| `qe.mark()` | Return mark byte offset |
| `qe.set_mark()` | Set mark at current cursor position |
| `qe.bol()` | Move cursor to beginning of line |
| `qe.eol()` | Move cursor to end of line |

### Reading Buffer Contents

| Function | Description |
|----------|-------------|
| `qe.char_at([offset])` | Return character at offset (default: cursor) as UTF-8 string |
| `qe.region_text(start, stop)` | Return text between byte offsets |
| `qe.buffer_size()` | Return total buffer size in bytes |
| `qe.buffer_name()` | Return current buffer name |
| `qe.buffer_filename()` | Return current buffer's file path |

### Navigation

| Function | Description |
|----------|-------------|
| `qe.current_line()` | Return current line number (1-based) |
| `qe.current_column()` | Return current column (0-based) |
| `qe.goto_line(line [, col])` | Move cursor to line (1-based), optional column |

### Commands and Key Bindings

| Function | Description |
|----------|-------------|
| `qe.command(name, fn)` | Register a command with no default binding |
| `qe.command(name, binding, fn)` | Register a command with a key binding |
| `qe.bind(command, key)` | Add a key binding to an existing command |
| `qe.call(command)` | Execute a qemacs command by name |

### Status Line

| Function | Description |
|----------|-------------|
| `qe.message(text)` | Display a message in the status line |
| `qe.error(text)` | Display an error in the status line |

## Key Binding Syntax

Key bindings use qemacs notation:

| Notation | Meaning |
|----------|---------|
| `C-x` | Ctrl+x |
| `M-x` | Alt+x (Meta) |
| `C-c h` | Ctrl+c followed by h |
| `C-x C-s` | Ctrl+x followed by Ctrl+s |
| `F5` | Function key F5 |

## Examples

### Word Count

```lua
qe.command("word-count", "C-c w", function()
    local text = qe.region_text(0, qe.buffer_size())
    local count = 0
    for _ in text:gmatch("%S+") do
        count = count + 1
    end
    qe.message(string.format("%d words", count))
end)
```

### Duplicate Line

```lua
qe.command("duplicate-line", "C-c d", function()
    qe.bol()
    local start = qe.point()
    qe.eol()
    local line = qe.region_text(start, qe.point())
    qe.insert("\n" .. line)
end)
```

### Surround Selection

```lua
qe.command("surround-parens", "C-c (", function()
    local mark = qe.mark()
    local point = qe.point()
    local start = math.min(mark, point)
    local stop = math.max(mark, point)
    local text = qe.region_text(start, stop)
    qe.set_point(start)
    qe.delete(stop - start)
    qe.insert("(" .. text .. ")")
end)
```

### Run Shell Command

```lua
qe.command("insert-date", "C-c t", function()
    local f = io.popen("date +%Y-%m-%d")
    if f then
        local date = f:read("*l")
        f:close()
        if date then qe.insert(date) end
    end
end)
```

## Lua Environment

Plugins run in a shared Lua 5.4 state with the full standard library available
(`string`, `table`, `math`, `io`, `os`, `coroutine`, `utf8`, `debug`).

Plugins execute with the same permissions as the editor process. There is no
sandboxing — `io.open`, `os.execute`, etc. all work.

Errors in plugins are caught and reported in the status line without crashing
the editor.

## Architecture

The Lua engine is compiled from an amalgamated source (`third_party/lua/lua-amalg.c`)
generated from [whilp/cosmopolitan](https://github.com/whilp/cosmopolitan)'s
Lua 5.4.6 distribution. The amalgamation is vendored directly in the repository.

Plugin system source: `plugin.c`. Tests: `tests/test_lua.c`.
