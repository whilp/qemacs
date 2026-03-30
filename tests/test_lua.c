/*
 * Tests for the Lua plugin system
 *
 * Tests the C API that plugin.c exposes for embedding Lua:
 * - Lua state lifecycle (init/exit)
 * - Evaluating Lua code
 * - The qe.* API bindings
 * - Loading .lua plugin files
 * - Command registration from Lua
 */
#include "testlib.h"

#include "../third_party/lua/lua-amalg.h"

/* Forward declarations of plugin.c functions we're testing */
extern lua_State *qe_lua_get_state(void);
extern int qe_lua_eval(lua_State *L, const char *code, char *errbuf, int errsize);

/*
 * Test: Lua state can be created and destroyed
 */
TEST(lua, state_lifecycle) {
    lua_State *L = luaL_newstate();
    ASSERT_TRUE(L != NULL);
    luaL_openlibs(L);
    lua_close(L);
}

/*
 * Test: Basic Lua evaluation works
 */
TEST(lua, eval_expression) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Simple arithmetic */
    int rc = luaL_dostring(L, "result = 1 + 2");
    ASSERT_EQ(rc, 0);
    lua_getglobal(L, "result");
    ASSERT_EQ(lua_tointeger(L, -1), 3);
    lua_pop(L, 1);

    lua_close(L);
}

/*
 * Test: Lua string evaluation and return
 */
TEST(lua, eval_string) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    int rc = luaL_dostring(L, "result = 'hello' .. ' ' .. 'world'");
    ASSERT_EQ(rc, 0);
    lua_getglobal(L, "result");
    ASSERT_STREQ(lua_tostring(L, -1), "hello world");
    lua_pop(L, 1);

    lua_close(L);
}

/*
 * Test: Lua error handling - syntax error returns non-zero
 */
TEST(lua, eval_error) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    int rc = luaL_dostring(L, "this is not valid lua!!!");
    ASSERT_TRUE(rc != 0);
    /* error message is on the stack */
    ASSERT_TRUE(lua_isstring(L, -1));
    lua_pop(L, 1);

    lua_close(L);
}

/*
 * Test: C function can be registered and called from Lua
 */
static int test_cfunc_add(lua_State *L) {
    int a = luaL_checkinteger(L, 1);
    int b = luaL_checkinteger(L, 2);
    lua_pushinteger(L, a + b);
    return 1;
}

TEST(lua, c_function_binding) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    lua_pushcfunction(L, test_cfunc_add);
    lua_setglobal(L, "add");

    int rc = luaL_dostring(L, "result = add(10, 32)");
    ASSERT_EQ(rc, 0);
    lua_getglobal(L, "result");
    ASSERT_EQ(lua_tointeger(L, -1), 42);
    lua_pop(L, 1);

    lua_close(L);
}

/*
 * Test: Module table can be created and accessed
 */
static int test_mod_version(lua_State *L) {
    lua_pushstring(L, "test-1.0");
    return 1;
}

TEST(lua, module_table) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Create a "qe" module table */
    lua_newtable(L);
    lua_pushcfunction(L, test_mod_version);
    lua_setfield(L, -2, "version");
    lua_setglobal(L, "qe");

    int rc = luaL_dostring(L, "result = qe.version()");
    ASSERT_EQ(rc, 0);
    lua_getglobal(L, "result");
    ASSERT_STREQ(lua_tostring(L, -1), "test-1.0");
    lua_pop(L, 1);

    lua_close(L);
}

/*
 * Test: Lua function references (luaL_ref) work for command dispatch
 */
TEST(lua, function_ref) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Load a function */
    int rc = luaL_dostring(L, "function myfunc() return 99 end");
    ASSERT_EQ(rc, 0);

    /* Get and ref the function */
    lua_getglobal(L, "myfunc");
    ASSERT_TRUE(lua_isfunction(L, -1));
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ASSERT_TRUE(ref != LUA_NOREF);

    /* Call via ref */
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    rc = lua_pcall(L, 0, 1, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(lua_tointeger(L, -1), 99);
    lua_pop(L, 1);

    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    lua_close(L);
}

/*
 * Test: Loading a Lua file from disk
 */
TEST(lua, load_file) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    /* Write a temp Lua file */
    FILE *f = fopen("/tmp/test_plugin.lua", "w");
    ASSERT_TRUE(f != NULL);
    fprintf(f, "test_loaded = true\ntest_value = 42\n");
    fclose(f);

    int rc = luaL_dofile(L, "/tmp/test_plugin.lua");
    ASSERT_EQ(rc, 0);

    lua_getglobal(L, "test_loaded");
    ASSERT_TRUE(lua_toboolean(L, -1));
    lua_pop(L, 1);

    lua_getglobal(L, "test_value");
    ASSERT_EQ(lua_tointeger(L, -1), 42);
    lua_pop(L, 1);

    unlink("/tmp/test_plugin.lua");
    lua_close(L);
}

/*
 * Test: qe_lua_eval helper wraps error reporting
 */
TEST(lua, qe_lua_eval_success) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    int rc = qe_lua_eval(L, "eval_result = 7 * 6", errbuf, sizeof(errbuf));
    ASSERT_EQ(rc, 0);
    ASSERT_STREQ(errbuf, "");

    lua_getglobal(L, "eval_result");
    ASSERT_EQ(lua_tointeger(L, -1), 42);
    lua_pop(L, 1);

    lua_close(L);
}

TEST(lua, qe_lua_eval_error) {
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    char errbuf[256] = {0};
    int rc = qe_lua_eval(L, "bad syntax!!!", errbuf, sizeof(errbuf));
    ASSERT_TRUE(rc != 0);
    ASSERT_TRUE(strlen(errbuf) > 0);

    lua_close(L);
}

int main() { return testlib_run_all(); }
