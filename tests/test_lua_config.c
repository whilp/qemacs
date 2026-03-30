/*
 * Tests for Lua-based configuration file parsing
 *
 * These tests verify that configuration files written in Lua
 * can be loaded and executed, replacing the old qscript system.
 */
#include "testlib.h"

#include "../third_party/lua/lua-amalg.h"

/* Forward declarations from plugin.c */
extern int qe_lua_eval(lua_State *L, const char *code, char *errbuf, int errsize);
extern int qe_lua_load_config(lua_State *L, const char *filename,
                              char *errbuf, int errsize);
extern int qe_lua_eval_expression(lua_State *L, const char *code,
                                  char *result, int resultsize,
                                  char *errbuf, int errsize);

/*
 * Test: Lua config file with qe.call() commands can be loaded
 */
TEST(lua_config, load_lua_config_file) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Create a minimal qe table for testing */
    lua_newtable(L);
    lua_setglobal(L, "qe");

    /* Write a Lua config file */
    FILE *f = fopen("/tmp/test_config.lua", "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "-- QEmacs Lua config\n");
    fprintf(f, "config_loaded = true\n");
    fprintf(f, "config_value = 42\n");
    fclose(f);

    char errbuf[256] = {0};
    int rc = qe_lua_load_config(L, "/tmp/test_config.lua", errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);

    lua_getglobal(L, "config_loaded");
    ASSERT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "config_value");
    ASSERT_EQ(lua_tointeger(L, -1), 42);
    lua_pop(L, 1);

    unlink("/tmp/test_config.lua");
    lua_close(L);
}

/*
 * Test: Loading a nonexistent config file returns error
 */
TEST(lua_config, load_nonexistent_file) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    int rc = qe_lua_load_config(L, "/tmp/does_not_exist_12345.lua",
                                errbuf, sizeof(errbuf));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(strlen(errbuf) > 0);

    lua_close(L);
}

/*
 * Test: Config file with syntax error reports error
 */
TEST(lua_config, config_syntax_error) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    FILE *f = fopen("/tmp/test_bad_config.lua", "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "this is not valid lua syntax!!!\n");
    fclose(f);

    char errbuf[256] = {0};
    int rc = qe_lua_load_config(L, "/tmp/test_bad_config.lua",
                                errbuf, sizeof(errbuf));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(strlen(errbuf) > 0);

    unlink("/tmp/test_bad_config.lua");
    lua_close(L);
}

/*
 * Test: Config can call qe.call() to invoke commands
 */
TEST(lua_config, config_calls_commands) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Set up a mock qe.call that records what was called */
    int rc = luaL_dostring(L,
        "called_commands = {}\n"
        "qe = { call = function(cmd) table.insert(called_commands, cmd) end }\n"
    );
    ASSERT_EQ(rc, 0);

    FILE *f = fopen("/tmp/test_cmd_config.lua", "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "qe.call('set-style')\n");
    fprintf(f, "qe.call('other-command')\n");
    fclose(f);

    char errbuf[256] = {0};
    rc = qe_lua_load_config(L, "/tmp/test_cmd_config.lua", errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);

    /* Verify commands were called */
    rc = luaL_dostring(L, "result = #called_commands");
    ASSERT_EQ(rc, 0);
    lua_getglobal(L, "result");
    ASSERT_EQ(lua_tointeger(L, -1), 2);
    lua_pop(L, 1);

    unlink("/tmp/test_cmd_config.lua");
    lua_close(L);
}

/*
 * Test: Config can use qe.bind() for key bindings
 */
TEST(lua_config, config_key_bindings) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Mock qe.bind */
    int rc = luaL_dostring(L,
        "bindings = {}\n"
        "qe = { bind = function(cmd, key) bindings[key] = cmd end }\n"
    );
    ASSERT_EQ(rc, 0);

    FILE *f = fopen("/tmp/test_bind_config.lua", "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "qe.bind('set-style', 'C-x s')\n");
    fclose(f);

    char errbuf[256] = {0};
    rc = qe_lua_load_config(L, "/tmp/test_bind_config.lua", errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);

    rc = luaL_dostring(L, "result = bindings['C-x s']");
    ASSERT_EQ(rc, 0);
    lua_getglobal(L, "result");
    ASSERT_STREQ(lua_tostring(L, -1), "set-style");
    lua_pop(L, 1);

    unlink("/tmp/test_bind_config.lua");
    lua_close(L);
}

/*
 * Test: Eval expression returns result as string
 */
TEST(lua_config, eval_expression_number) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    char result[256] = {0};
    int rc = qe_lua_eval_expression(L, "return 1 + 2", result, sizeof(result),
                                    errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(result, "3");

    lua_close(L);
}

/*
 * Test: Eval expression returns string result
 */
TEST(lua_config, eval_expression_string) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    char result[256] = {0};
    int rc = qe_lua_eval_expression(L, "return 'hello'", result, sizeof(result),
                                    errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(result, "hello");

    lua_close(L);
}

/*
 * Test: Eval expression with syntax error
 */
TEST(lua_config, eval_expression_error) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    char result[256] = {0};
    int rc = qe_lua_eval_expression(L, "bad syntax!!!", result, sizeof(result),
                                    errbuf, sizeof(errbuf));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(strlen(errbuf) > 0);

    lua_close(L);
}

/*
 * Test: Eval expression with nil/void result
 */
TEST(lua_config, eval_expression_void) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    char result[256] = {0};
    int rc = qe_lua_eval_expression(L, "x = 5", result, sizeof(result),
                                    errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);
    /* No return value, result should be empty */
    ASSERT_STREQ(result, "");

    lua_close(L);
}

/*
 * Test: Eval expression with boolean result
 */
TEST(lua_config, eval_expression_boolean) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    char result[256] = {0};
    int rc = qe_lua_eval_expression(L, "return true", result, sizeof(result),
                                    errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(result, "true");

    lua_close(L);
}

/*
 * Test: Eval buffer evaluates multiple statements
 */
TEST(lua_config, eval_buffer_content) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    const char *code = "buf_x = 10\nbuf_y = 20\nbuf_z = buf_x + buf_y\n";
    int rc = qe_lua_eval(L, code, errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);

    lua_getglobal(L, "buf_z");
    ASSERT_EQ(lua_tointeger(L, -1), 30);
    lua_pop(L, 1);

    lua_close(L);
}

int main() { return testlib_run_all(); }
