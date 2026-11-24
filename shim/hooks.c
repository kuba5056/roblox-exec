#include "shim.h"

#define JMP_SIZE 14

typedef struct {
    void*   target;
    uint8_t original[JMP_SIZE];
    int     installed;
} trampoline_t;

static trampoline_t g_tr_scheduler = {0};

static int write_jmp_abs(void* at, void* to)
{
    DWORD old;
    if (!VirtualProtect(at, JMP_SIZE, PAGE_EXECUTE_READWRITE, &old)) return 0;
    uint8_t* p = (uint8_t*)at;
    p[0] = 0xFF;  p[1] = 0x25;  *(uint32_t*)(p+2) = 0;
    *(uint64_t*)(p+6) = (uint64_t)to;
    VirtualProtect(at, JMP_SIZE, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, JMP_SIZE);
    return 1;
}

static int save_and_hook(void* target, void* detour, trampoline_t* tr)
{
    if (!target || tr->installed) return 0;
    DWORD old;
    if (!VirtualProtect(target, JMP_SIZE, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy(tr->original, target, JMP_SIZE);
    VirtualProtect(target, JMP_SIZE, old, &old);
    tr->target = target;
    if (!write_jmp_abs(target, detour)) return 0;
    tr->installed = 1;
    return 1;
}

static void restore(trampoline_t* tr)
{
    if (!tr->installed) return;
    DWORD old;
    VirtualProtect(tr->target, JMP_SIZE, PAGE_EXECUTE_READWRITE, &old);
    memcpy(tr->target, tr->original, JMP_SIZE);
    VirtualProtect(tr->target, JMP_SIZE, old, &old);
    FlushInstructionCache(GetCurrentProcess(), tr->target, JMP_SIZE);
    tr->installed = 0;
}

int rbx_hooks_install(rbx_vm_t* vm)
{
    (void)vm;
    return 1;
}

void rbx_hooks_remove(void)
{
    restore(&g_tr_scheduler);
}
