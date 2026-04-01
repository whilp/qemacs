/*
 * Lua plugin system for QEmacs
 *
 * Embeds Lua 5.4 and exposes editor APIs via a global `qe` table.
 * Loads .lua plugins from ~/.qe/ at startup. Plugins can register
 * commands, manipulate buffers, and extend the editor.
 *
 * Uses the Lua amalgamation from whilp/cosmopolitan.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
#include "third_party/lua/lua-amalg.h"
#pragma GCC diagnostic pop

#ifdef QE_MODULE
/* When compiled as a standalone test, skip qe.h dependencies */
#else
#ifndef TEST_LUA_STANDALONE
#include "qe.h"
#endif
#endif

#include <dirent.h>

/* ---- Forward declarations (used by tests) ---- */

lua_State *qe_lua_get_state(void);
int qe_lua_eval(lua_State *L, const char *code, char *errbuf, int errsize);
int qe_lua_load_config(lua_State *L, const char *filename,
                       char *errbuf, int errsize);
int qe_lua_eval_expression(lua_State *L, const char *code,
                           char *result, int result_size,
                           char *errbuf, int errsize);

/* ---- Lua state management ---- */

static lua_State *qe_L;

lua_State *qe_lua_get_state(void)
{
    return qe_L;
}

/*
 * Evaluate a Lua string. Returns 0 on success, non-zero on error.
 * If errbuf is provided, the error message is copied there.
 */
int qe_lua_eval(lua_State *L, const char *code, char *errbuf, int errsize)
{
    int rc;

    if (errbuf && errsize > 0)
        errbuf[0] = '\0';

    rc = luaL_dostring(L, code);
    if (rc != 0) {
        if (errbuf && errsize > 0) {
            const char *msg = lua_tostring(L, -1);
            if (msg) {
                int len = strlen(msg);
                if (len >= errsize)
                    len = errsize - 1;
                memcpy(errbuf, msg, len);
                errbuf[len] = '\0';
            }
        }
        lua_pop(L, 1);
    }
    return rc;
}

/*
 * Load and execute a Lua configuration file.
 * Returns 0 on success, non-zero on error.
 * If errbuf is provided, the error message is copied there.
 */
int qe_lua_load_config(lua_State *L, const char *filename,
                       char *errbuf, int errsize)
{
    int rc;

    if (errbuf && errsize > 0)
        errbuf[0] = '\0';

    rc = luaL_dofile(L, filename);
    if (rc != 0) {
        if (errbuf && errsize > 0) {
            const char *msg = lua_tostring(L, -1);
            if (msg) {
                int len = strlen(msg);
                if (len >= errsize)
                    len = errsize - 1;
                memcpy(errbuf, msg, len);
                errbuf[len] = '\0';
            }
        }
        lua_pop(L, 1);
    }
    return rc;
}

/*
 * Evaluate a Lua expression and return the result as a string.
 * The code is wrapped in a chunk; if it starts with "return" the result
 * is captured. Returns 0 on success, non-zero on error.
 */
int qe_lua_eval_expression(lua_State *L, const char *code,
                           char *result, int resultsize,
                           char *errbuf, int errsize)
{
    int rc;

    if (result && resultsize > 0)
        result[0] = '\0';
    if (errbuf && errsize > 0)
        errbuf[0] = '\0';

    rc = luaL_dostring(L, code);
    if (rc != 0) {
        if (errbuf && errsize > 0) {
            const char *msg = lua_tostring(L, -1);
            if (msg) {
                int len = strlen(msg);
                if (len >= errsize)
                    len = errsize - 1;
                memcpy(errbuf, msg, len);
                errbuf[len] = '\0';
            }
        }
        lua_pop(L, 1);
        return rc;
    }

    /* Capture any return value */
    if (result && resultsize > 0 && !lua_isnoneornil(L, -1)) {
        const char *s = luaL_tolstring(L, -1, NULL);
        if (s) {
            int len = strlen(s);
            if (len >= resultsize)
                len = resultsize - 1;
            memcpy(result, s, len);
            result[len] = '\0';
        }
        lua_pop(L, 1); /* pop tolstring result */
    }

    return 0;
}

/* ---- Helper: check if file has .lua extension ---- */

static int is_lua_file(const char *name)
{
    const char *ext = strrchr(name, '.');
    return ext && !strcmp(ext, ".lua");
}

/* ---- QEmacs API bindings (only when linked with the editor) ---- */

#if !defined(QE_MODULE) && !defined(TEST_LUA_STANDALONE)

static QEmacsState *lua_qs;

static EditState *lua_get_active_state(void)
{
    return lua_qs ? lua_qs->active_window : NULL;
}

/* qe.insert(text) */
static int l_insert(lua_State *L)
{
    EditState *s = lua_get_active_state();
    const char *text;

    if (!s) return 0;
    text = luaL_checkstring(L, 1);
    s->offset += eb_insert_str(s->b, s->offset, text);
    return 0;
}

/* qe.delete(n) */
static int l_delete(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int n;

    if (!s) return 0;
    n = luaL_checkinteger(L, 1);
    if (n > 0 && s->offset + n <= s->b->total_size)
        eb_delete(s->b, s->offset, n);
    return 0;
}

/* qe.point() -> offset */
static int l_point(lua_State *L)
{
    EditState *s = lua_get_active_state();
    lua_pushinteger(L, s ? s->offset : 0);
    return 1;
}

/* qe.set_point(offset) */
static int l_set_point(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int offset;

    if (!s) return 0;
    offset = luaL_checkinteger(L, 1);
    if (offset < 0) offset = 0;
    if (offset > s->b->total_size) offset = s->b->total_size;
    s->offset = offset;
    return 0;
}

/* qe.mark() -> offset */
static int l_mark(lua_State *L)
{
    EditState *s = lua_get_active_state();
    lua_pushinteger(L, s ? s->b->mark : 0);
    return 1;
}

/* qe.set_mark() */
static int l_set_mark(lua_State *L)
{
    EditState *s = lua_get_active_state();
    if (s) s->b->mark = s->offset;
    return 0;
}

/* qe.bol() */
static int l_bol(lua_State *L)
{
    EditState *s = lua_get_active_state();
    if (s) s->offset = eb_goto_bol(s->b, s->offset);
    return 0;
}

/* qe.eol() */
static int l_eol(lua_State *L)
{
    EditState *s = lua_get_active_state();
    if (s) s->offset = eb_goto_eol(s->b, s->offset);
    return 0;
}

/* qe.buffer_size() -> int */
static int l_buffer_size(lua_State *L)
{
    EditState *s = lua_get_active_state();
    lua_pushinteger(L, s ? s->b->total_size : 0);
    return 1;
}

/* qe.buffer_name() -> string */
static int l_buffer_name(lua_State *L)
{
    EditState *s = lua_get_active_state();
    lua_pushstring(L, s ? s->b->name : "");
    return 1;
}

/* qe.buffer_filename() -> string */
static int l_buffer_filename(lua_State *L)
{
    EditState *s = lua_get_active_state();
    lua_pushstring(L, s ? s->b->filename : "");
    return 1;
}

/* qe.current_line() -> line (1-based) */
static int l_current_line(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int line = 0, col = 0;
    if (s) eb_get_pos(s->b, &line, &col, s->offset);
    lua_pushinteger(L, line + 1);
    return 1;
}

/* qe.current_column() -> col (0-based) */
static int l_current_column(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int line = 0, col = 0;
    if (s) eb_get_pos(s->b, &line, &col, s->offset);
    lua_pushinteger(L, col);
    return 1;
}

/* qe.goto_line(line, col) — line is 1-based */
static int l_goto_line(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int line, col;

    if (!s) return 0;
    line = luaL_checkinteger(L, 1) - 1;  /* convert to 0-based */
    col = luaL_optinteger(L, 2, 0);
    s->offset = eb_goto_pos(s->b, line, col);
    return 0;
}

/* qe.char_at([offset]) -> string (single char) */
static int l_char_at(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int offset;
    char buf[8];
    int ch, len;

    if (!s) { lua_pushstring(L, ""); return 1; }
    offset = luaL_optinteger(L, 1, s->offset);
    ch = eb_nextc(s->b, offset, &offset);
    if (ch == EOF) {
        lua_pushstring(L, "");
    } else {
        /* encode as UTF-8 */
        if (ch < 0x80) {
            buf[0] = ch;
            len = 1;
        } else if (ch < 0x800) {
            buf[0] = 0xC0 | (ch >> 6);
            buf[1] = 0x80 | (ch & 0x3F);
            len = 2;
        } else if (ch < 0x10000) {
            buf[0] = 0xE0 | (ch >> 12);
            buf[1] = 0x80 | ((ch >> 6) & 0x3F);
            buf[2] = 0x80 | (ch & 0x3F);
            len = 3;
        } else {
            buf[0] = 0xF0 | (ch >> 18);
            buf[1] = 0x80 | ((ch >> 12) & 0x3F);
            buf[2] = 0x80 | ((ch >> 6) & 0x3F);
            buf[3] = 0x80 | (ch & 0x3F);
            len = 4;
        }
        lua_pushlstring(L, buf, len);
    }
    return 1;
}

/* qe.region_text(start, stop) -> string */
static int l_region_text(lua_State *L)
{
    EditState *s = lua_get_active_state();
    int start, stop, size;
    char *buf;

    if (!s) { lua_pushstring(L, ""); return 1; }
    start = luaL_checkinteger(L, 1);
    stop = luaL_checkinteger(L, 2);
    if (start < 0) start = 0;
    if (stop > s->b->total_size) stop = s->b->total_size;
    if (stop <= start) { lua_pushstring(L, ""); return 1; }
    size = stop - start;
    buf = qe_malloc_array(char, size + 1);
    if (!buf) { lua_pushstring(L, ""); return 1; }
    eb_read(s->b, start, buf, size);
    buf[size] = '\0';
    lua_pushlstring(L, buf, size);
    qe_free(&buf);
    return 1;
}

/* qe.message(text) */
static int l_message(lua_State *L)
{
    EditState *s = lua_get_active_state();
    const char *text = luaL_checkstring(L, 1);
    if (s) put_status(s, "%s", text);
    return 0;
}

/* qe.error(text) */
static int l_error(lua_State *L)
{
    EditState *s = lua_get_active_state();
    const char *text = luaL_checkstring(L, 1);
    if (s) put_error(s, "%s", text);
    return 0;
}

/* qe.call(command_name) — execute a qemacs command by name */
static int l_call(lua_State *L)
{
    EditState *s = lua_get_active_state();
    const char *cmd = luaL_checkstring(L, 1);
    if (s) do_execute_command(s, cmd, NO_ARG);
    return 0;
}

/* ---- Lua command trampoline ---- */

#define MAX_LUA_COMMANDS 256

/* Storage for dynamically registered command names and specs */
static char lua_cmd_storage[MAX_LUA_COMMANDS][128]; /* "name\0binding\0" */
static char lua_cmd_specs[MAX_LUA_COMMANDS][8];
static CmdDef lua_cmd_defs[MAX_LUA_COMMANDS];
static int nb_lua_cmds;

/* Dispatch function for Lua-registered commands */
static void lua_cmd_dispatch(EditState *s, int lua_ref)
{
    lua_State *L = qe_L;
    if (!L) return;

    lua_rawgeti(L, LUA_REGISTRYINDEX, lua_ref);
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        put_error(s, "Lua: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

/*
 * qe.command(name, [binding,] fn)
 *
 * Register a new editor command backed by a Lua function.
 * - name: command name (e.g. "insert-hello")
 * - binding: optional key binding (e.g. "C-c h")
 * - fn: Lua function to call
 */
static int l_command(lua_State *L)
{
    const char *name;
    const char *binding = "";
    int func_idx;
    int ref;
    CmdDef *cmd;
    char *storage;
    int name_len, binding_len;

    if (nb_lua_cmds >= MAX_LUA_COMMANDS) {
        return luaL_error(L, "too many Lua commands (max %d)", MAX_LUA_COMMANDS);
    }

    name = luaL_checkstring(L, 1);

    /* Determine argument layout: (name, fn) or (name, binding, fn) */
    if (lua_isfunction(L, 2)) {
        func_idx = 2;
    } else {
        binding = luaL_checkstring(L, 2);
        luaL_checktype(L, 3, LUA_TFUNCTION);
        func_idx = 3;
    }

    /* Store a reference to the Lua function */
    lua_pushvalue(L, func_idx);
    ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Build the name\0binding\0 storage (CmdDef.name format) */
    storage = lua_cmd_storage[nb_lua_cmds];
    name_len = strlen(name);
    binding_len = strlen(binding);
    if (name_len + 1 + binding_len + 1 > (int)sizeof(lua_cmd_storage[0])) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        return luaL_error(L, "command name + binding too long");
    }
    memcpy(storage, name, name_len);
    storage[name_len] = '\0';
    memcpy(storage + name_len + 1, binding, binding_len);
    storage[name_len + 1 + binding_len] = '\0';

    /* Build CmdDef */
    cmd = &lua_cmd_defs[nb_lua_cmds];
    cmd->name = storage;
    lua_cmd_specs[nb_lua_cmds][0] = '\0';
    cmd->spec = lua_cmd_specs[nb_lua_cmds];
    cmd->sig = CMD_ESi;
    cmd->val = ref;
    cmd->action.ESi = (void (*)(EditState *, int))lua_cmd_dispatch;

    qe_register_commands(lua_qs, NULL, cmd, 1);
    nb_lua_cmds++;

    return 0;
}

/* qe.bind(command_name, key) */
static int l_bind(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    const char *key = luaL_checkstring(L, 2);
    if (lua_qs)
        qe_register_bindings(lua_qs, &lua_qs->first_key, cmd, key);
    return 0;
}

/* The qe module function table */
static const luaL_Reg qe_funcs[] = {
    { "insert",          l_insert },
    { "delete",          l_delete },
    { "point",           l_point },
    { "set_point",       l_set_point },
    { "mark",            l_mark },
    { "set_mark",        l_set_mark },
    { "bol",             l_bol },
    { "eol",             l_eol },
    { "buffer_size",     l_buffer_size },
    { "buffer_name",     l_buffer_name },
    { "buffer_filename", l_buffer_filename },
    { "current_line",    l_current_line },
    { "current_column",  l_current_column },
    { "goto_line",       l_goto_line },
    { "char_at",         l_char_at },
    { "region_text",     l_region_text },
    { "message",         l_message },
    { "error",           l_error },
    { "call",            l_call },
    { "command",         l_command },
    { "bind",            l_bind },
    { NULL, NULL }
};

/* ---- Plugin loading ---- */

static int qe_load_lua_file(lua_State *L, const char *path)
{
    return luaL_dofile(L, path);
}

static void qe_load_lua_plugins_from_dir(lua_State *L, const char *dir)
{
    DIR *d;
    struct dirent *de;
    char path[1024];

    d = opendir(dir);
    if (!d)
        return;

    while ((de = readdir(d)) != NULL) {
        if (!is_lua_file(de->d_name))
            continue;
        if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, de->d_name) >= sizeof(path))
            continue;  /* path too long, skip */
        if (qe_load_lua_file(L, path) != LUA_OK) {
            /* Log error but continue loading other plugins */
            fprintf(stderr, "qe: error loading plugin %s: %s\n",
                    path, lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    closedir(d);
}

/* Public: load all plugins from ~/.qe/ */
void qe_load_all_plugins(QEmacsState *qs)
{
    const char *home;
    char dir[1024];

    if (!qe_L)
        return;

    home = getenv("HOME");
    if (!home)
        return;

    snprintf(dir, sizeof(dir), "%s/.qe", home);
    qe_load_lua_plugins_from_dir(qe_L, dir);
}

/* Public: cleanup */
void qe_exit_all_plugins(QEmacsState *qs)
{
    int i;

    if (qe_L) {
        /* Release Lua function references */
        for (i = 0; i < nb_lua_cmds; i++) {
            luaL_unref(qe_L, LUA_REGISTRYINDEX, lua_cmd_defs[i].val);
        }
        nb_lua_cmds = 0;

        lua_close(qe_L);
        qe_L = NULL;
    }
    lua_qs = NULL;
}

/* ---- Config file and eval (replaces qescript.c) ---- */

/*
 * Load and execute a Lua configuration file.
 * This replaces parse_config_file() from qescript.c.
 */
int parse_config_file(EditState *s, const char *filename) {
    char errbuf[512];

    if (!qe_L) return -1;
    /* Silently skip non-existent files (like the old qscript behavior) */
    if (access(filename, R_OK) != 0)
        return -1;
    if (qe_lua_load_config(qe_L, filename, errbuf, sizeof(errbuf)) != 0) {
        put_error(s, "Config: %s", errbuf);
        return -1;
    }
    return 0;
}

/*
 * Evaluate a Lua expression string.
 * With argval > 0, insert result into buffer; otherwise show in status bar.
 * This replaces do_eval_expression() from qescript.c.
 */
void do_eval_expression(EditState *s, const char *expression, int argval) {
    char errbuf[512];
    char result[512];

    if (!qe_L) {
        put_error(s, "Lua not initialized");
        return;
    }
    if (qe_lua_eval_expression(qe_L, expression, result, sizeof(result),
                               errbuf, sizeof(errbuf)) != 0) {
        put_error(s, "Lua: %s", errbuf);
        return;
    }
    if (result[0]) {
        if (argval != NO_ARG && argval > 0) {
            s->offset += eb_insert_str(s->b, s->offset, result);
        } else {
            put_status(s, "-> %s", result);
        }
    }
}

/*
 * Evaluate Lua code in a buffer region.
 * This replaces do_eval_region() from qescript.c.
 */
void do_eval_region(EditState *s, int argval) {
    char *buf;
    char errbuf[512];
    int start, stop, size;

    start = s->b->mark;
    stop = s->offset;
    if (stop < start) {
        int tmp = start;
        start = stop;
        stop = tmp;
    }
    size = stop - start;
    if (size <= 0) return;

    buf = qe_malloc_array(char, size + 1);
    if (!buf) return;
    eb_read(s->b, start, buf, size);
    buf[size] = '\0';

    if (!qe_L) {
        put_error(s, "Lua not initialized");
        qe_free(&buf);
        return;
    }
    if (qe_lua_eval(qe_L, buf, errbuf, sizeof(errbuf)) != 0) {
        put_error(s, "Lua: %s", errbuf);
    }
    qe_free(&buf);
}

/*
 * Evaluate Lua code in the entire buffer.
 * This replaces do_eval_buffer() from qescript.c.
 */
void do_eval_buffer(EditState *s, int argval) {
    char *buf;
    char errbuf[512];
    int size;

    size = s->b->total_size;
    if (size <= 0) return;

    buf = qe_malloc_array(char, size + 1);
    if (!buf) return;
    eb_read(s->b, 0, buf, size);
    buf[size] = '\0';

    if (!qe_L) {
        put_error(s, "Lua not initialized");
        qe_free(&buf);
        return;
    }
    if (qe_lua_eval(qe_L, buf, errbuf, sizeof(errbuf)) != 0) {
        put_error(s, "Lua: %s", errbuf);
    }
    qe_free(&buf);
}

/* ---- Interactive commands ---- */

static void do_load_plugin(EditState *s, const char *filename)
{
    if (!qe_L) {
        put_error(s, "Lua not initialized");
        return;
    }
    if (qe_load_lua_file(qe_L, filename) != LUA_OK) {
        put_error(s, "Lua: %s", lua_tostring(qe_L, -1));
        lua_pop(qe_L, 1);
    } else {
        put_status(s, "Plugin loaded: %s", filename);
    }
}

static void do_eval_lua(EditState *s, const char *code)
{
    char errbuf[512];

    if (!qe_L) {
        put_error(s, "Lua not initialized");
        return;
    }
    if (qe_lua_eval(qe_L, code, errbuf, sizeof(errbuf)) != 0) {
        put_error(s, "Lua: %s", errbuf);
    }
}

static const CmdDef plugin_commands[] = {
    CMD3( "load-plugin", "",
          "Load a Lua plugin from a .lua file",
          do_load_plugin, ESs,
          "s{Plugin file: }[file]|file|", 0)
    CMD3( "eval-lua", "",
          "Evaluate a Lua expression",
          do_eval_lua, ESs,
          "s{Lua: }|lua|", 0)
    CMD3( "eval-expression", "M-:",
          "Evaluate a Lua expression and display result",
          do_eval_expression, ESsi,
          "s{Eval: }|expression|"
          "P", 0)
    CMD3( "eval-region", "M-C-z",
          "Evaluate Lua code in the selected region",
          do_eval_region, ESi, "P", 0)
    CMD3( "eval-buffer", "",
          "Evaluate Lua code in the current buffer",
          do_eval_buffer, ESi, "P", 0)
};

/* ---- Module init ---- */

static int plugin_init(QEmacsState *qs)
{
    lua_qs = qs;

    /* Create Lua state */
    qe_L = luaL_newstate();
    if (!qe_L)
        return -1;
    luaL_openlibs(qe_L);

    /* Register the qe module as a global table */
    luaL_newlib(qe_L, qe_funcs);
    lua_setglobal(qe_L, "qe");

    /* Set package.path to include ~/.qe/ */
    {
        const char *home = getenv("HOME");
        if (home) {
            char lua_code[1024];
            snprintf(lua_code, sizeof(lua_code),
                     "package.path = '%s/.qe/?.lua;' .. package.path", home);
            (void)luaL_dostring(qe_L, lua_code);
        }
    }

    /* Register interactive commands */
    qe_register_commands(qs, NULL, plugin_commands, countof(plugin_commands));

    return 0;
}

qe_module_init(plugin_init);

#endif /* !QE_MODULE && !TEST_LUA_STANDALONE */
