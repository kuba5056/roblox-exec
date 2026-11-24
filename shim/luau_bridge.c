#include "shim.h"
#include <stdio.h>
#include <string.h>

static lua_State* g_L_main = NULL;

static int l_print(lua_State* L)
{
    int top = g_vm.lua_gettop(L);
    for (int i = 1; i <= top; ++i) {
        size_t n = 0;
        const char* s = g_vm.lua_tolstring(L, i, &n);
        if (s) {
            rbx_pipe_log(s);
        }
    }
    return 0;
}

static int l_checkcaller(lua_State* L)
{
    g_vm.lua_pushboolean(L, 1);
    return 1;
}

static int l_identityof(lua_State* L)
{
    g_vm.lua_pushinteger(L, 8);
    return 1;
}

static void inject_globals(lua_State* L)
{
    struct { const char* name; int (*fn)(lua_State*); } api[] = {
        { "rbx_print",      l_print },
        { "checkcaller",    l_checkcaller },
        { "getidentity",    l_identityof },
    };
    for (size_t i = 0; i < sizeof(api)/sizeof(api[0]); ++i) {
        g_vm.lua_pushcclosure(L, api[i].fn, api[i].name, 0);
        g_vm.lua_setfield(L, -10002, api[i].name);
    }
}

static lua_State* acquire_state(void)
{
    if (g_L_main) return g_L_main;
    return NULL;
}

int rbx_luau_exec_bytecode(const char* bc, size_t len, char* err, size_t errcap)
{
    lua_State* L = acquire_state();
    if (!L) { snprintf(err, errcap, "no lua_State yet"); return -1; }

    lua_State* T = g_vm.lua_newthread(L);
    inject_globals(T);

    int r = g_vm.luau_load(T, "=rbx-exec", bc, len, 0);
    if (r != 0) {
        size_t n = 0;
        const char* m = g_vm.lua_tolstring(T, -1, &n);
        snprintf(err, errcap, "load: %.*s", (int)n, m ? m : "?");
        g_vm.lua_settop(T, 0);
        return -2;
    }

    r = g_vm.lua_resume(T, L, 0);
    if (r != 0 && r != 1) {
        size_t n = 0;
        const char* m = g_vm.lua_tolstring(T, -1, &n);
        snprintf(err, errcap, "run: %.*s", (int)n, m ? m : "?");
        g_vm.lua_settop(T, 0);
        return -3;
    }
    g_vm.lua_settop(T, 0);
    return 0;
}

int rbx_luau_exec_source(const char* src, size_t len, char* err, size_t errcap)
{
    (void)src; (void)len;
    snprintf(err, errcap, "source exec routed through host luau-compile");
    return -1;
}

void rbx_luau_set_main_state(lua_State* L) { g_L_main = L; }
