#include "shim.h"

rbx_vm_t g_vm = {0};
static HANDLE g_worker = NULL;
static volatile LONG g_stop = 0;

static DWORD WINAPI rbx_main(LPVOID param)
{
    HMODULE self = (HMODULE)param;
    rbx_ldr_unlink(self);

    for (int i = 0; i < 30 && !rbx_scan_roblox(&g_vm); ++i) {
        Sleep(500);
    }
    if (!g_vm.luau_load) {
        rbx_pipe_log("scan failed, no luau_load");
        return 1;
    }

    rbx_hooks_install(&g_vm);
    rbx_pipe_log("shim up, vm resolved");
    rbx_pipe_server();
    rbx_hooks_remove();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(h);
        g_worker = CreateThread(NULL, 0, rbx_main, h, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        InterlockedExchange(&g_stop, 1);
        break;
    }
    return TRUE;
}
