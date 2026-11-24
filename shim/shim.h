#ifndef RBX_SHIM_H
#define RBX_SHIM_H

#include <windows.h>
#include <stdint.h>

#define RBX_PIPE_NAME L"\\\\.\\pipe\\rbx-exec"
#define RBX_SHIM_VERSION 0x00010000

typedef struct lua_State lua_State;

typedef int  (*luau_load_fn)(lua_State* L, const char* chunkname,
                             const char* data, size_t size, int env);
typedef int  (*lua_pcall_fn)(lua_State* L, int nargs, int nresults, int errfunc);
typedef void (*lua_pushcclosure_fn)(lua_State* L, int (*fn)(lua_State*),
                                    const char* debugname, int nup);
typedef void (*lua_setfield_fn)(lua_State* L, int idx, const char* k);
typedef int  (*lua_gettop_fn)(lua_State* L);
typedef void (*lua_settop_fn)(lua_State* L, int idx);
typedef const char* (*lua_tolstring_fn)(lua_State* L, int idx, size_t* len);
typedef void (*lua_pushstring_fn)(lua_State* L, const char* s);
typedef void (*lua_pushboolean_fn)(lua_State* L, int b);
typedef void (*lua_pushinteger_fn)(lua_State* L, int32_t n);
typedef lua_State* (*lua_newthread_fn)(lua_State* L);
typedef lua_State* (*luaL_newstate_fn)(void);
typedef void (*luaL_openlibs_fn)(lua_State* L);
typedef int  (*lua_resume_fn)(lua_State* L, lua_State* from, int narg);

typedef struct {
    uintptr_t base;
    size_t    size;
    luau_load_fn        luau_load;
    lua_pcall_fn        lua_pcall;
    lua_pushcclosure_fn lua_pushcclosure;
    lua_setfield_fn     lua_setfield;
    lua_gettop_fn       lua_gettop;
    lua_settop_fn       lua_settop;
    lua_tolstring_fn    lua_tolstring;
    lua_pushstring_fn   lua_pushstring;
    lua_pushboolean_fn  lua_pushboolean;
    lua_pushinteger_fn  lua_pushinteger;
    lua_newthread_fn    lua_newthread;
    lua_resume_fn       lua_resume;
    uintptr_t           scheduler;
    uintptr_t           datamodel_ptr;
    uintptr_t           script_context_vt;
} rbx_vm_t;

extern rbx_vm_t g_vm;

enum {
    IPC_HELLO        = 0x01,
    IPC_EXEC_SOURCE  = 0x10,
    IPC_EXEC_BYTECODE= 0x11,
    IPC_EJECT        = 0x20,
    IPC_LOG          = 0x80,
    IPC_RESULT       = 0x81,
};

#pragma pack(push, 1)
typedef struct {
    uint32_t kind;
    uint32_t len;
} rbx_msg_t;
#pragma pack(pop)

int  rbx_scan_roblox(rbx_vm_t* vm);
void rbx_ldr_unlink(HMODULE self);
int  rbx_hooks_install(rbx_vm_t* vm);
void rbx_hooks_remove(void);
int  rbx_luau_exec_bytecode(const char* bc, size_t len, char* err, size_t errcap);
int  rbx_luau_exec_source  (const char* src, size_t len, char* err, size_t errcap);
int  rbx_pipe_server(void);
void rbx_pipe_log(const char* s);

#endif
