#include "shim.h"
#include <string.h>

extern uintptr_t rbx_pe_text(HMODULE mod, size_t* out_size);
extern HMODULE   rbx_pe_find_roblox(void);

typedef struct {
    const char* name;
    const char* pattern;
    const char* mask;
    int         offset;
    int         rel_add;
} sig_t;

static int match(const uint8_t* mem, const uint8_t* pat, const char* mask)
{
    for (; *mask; ++mem, ++pat, ++mask) {
        if (*mask == 'x' && *mem != *pat) return 0;
    }
    return 1;
}

static uintptr_t find_sig(uintptr_t base, size_t size, const sig_t* s)
{
    size_t plen = strlen(s->mask);
    const uint8_t* mem = (const uint8_t*)base;
    for (size_t i = 0; i + plen <= size; ++i) {
        if (match(mem + i, (const uint8_t*)s->pattern, s->mask)) {
            uintptr_t hit = (uintptr_t)(mem + i) + s->offset;
            if (s->rel_add) {
                int32_t disp = *(int32_t*)hit;
                hit = hit + 4 + disp;
            }
            return hit;
        }
    }
    return 0;
}

static const sig_t SIGS[] = {
    { "luau_load",
      "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x49\x8B\xF0",
      "xxxx?xxxx?xxxx?xxx", 0, 0 },
    { "lua_pcall",
      "\x48\x89\x5C\x24\x00\x48\x89\x6C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x41\x8B\xE9",
      "xxxx?xxxx?xxxx?xxxx?xxx", 0, 0 },
    { "lua_pushcclosure",
      "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x41\x8B\xF8",
      "xxxx?xxxx?xxxx?xxx", 0, 0 },
    { "lua_setfield",
      "\x40\x53\x48\x83\xEC\x00\x49\x8B\xD8\x48\x8B\xCA",
      "xxxxx?xxxxxx", 0, 0 },
    { "lua_gettop",
      "\x48\x8B\x41\x00\x48\x2B\x41\x00\x48\xC1\xF8\x00\xC3",
      "xxx?xxx?xxx?x", 0, 0 },
    { "lua_settop",
      "\x48\x8B\x41\x00\x4C\x8B\xC2",
      "xxx?xxx", 0, 0 },
    { "lua_tolstring",
      "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x00\x49\x8B\xF8",
      "xxxx?xxxx?xxx", 0, 0 },
    { "lua_pushstring",
      "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x49\x8B\xF0\x48\x8B\xDA",
      "xxxx?xxxx?xxxx?xxxxxx", 0, 0 },
    { "lua_pushboolean",
      "\x48\x8B\x41\x00\x89\x10",
      "xxx?xx", 0, 0 },
    { "lua_pushinteger",
      "\x48\x8B\x41\x00\xC7\x40",
      "xxx?xx", 0, 0 },
    { "lua_newthread",
      "\x40\x55\x53\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24",
      "xxxxxxxxxxxxxxxxx", 0, 0 },
    { "lua_resume",
      "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x41\x56\x41\x57\x48\x83\xEC",
      "xxxx?xxxx?xxxxxxxx", 0, 0 },
};

int rbx_scan_roblox(rbx_vm_t* vm)
{
    HMODULE mod = rbx_pe_find_roblox();
    if (!mod) return 0;

    size_t size = 0;
    uintptr_t base = rbx_pe_text(mod, &size);
    if (!base || !size) return 0;

    vm->base = (uintptr_t)mod;
    vm->size = size;

    void** slots[] = {
        (void**)&vm->luau_load,
        (void**)&vm->lua_pcall,
        (void**)&vm->lua_pushcclosure,
        (void**)&vm->lua_setfield,
        (void**)&vm->lua_gettop,
        (void**)&vm->lua_settop,
        (void**)&vm->lua_tolstring,
        (void**)&vm->lua_pushstring,
        (void**)&vm->lua_pushboolean,
        (void**)&vm->lua_pushinteger,
        (void**)&vm->lua_newthread,
        (void**)&vm->lua_resume,
    };

    int hits = 0;
    for (size_t i = 0; i < sizeof(SIGS)/sizeof(SIGS[0]); ++i) {
        uintptr_t p = find_sig(base, size, &SIGS[i]);
        if (p) {
            *slots[i] = (void*)p;
            ++hits;
        }
    }
    return hits == (int)(sizeof(SIGS)/sizeof(SIGS[0]));
}
