#include "shim.h"

static uintptr_t module_text_base(HMODULE mod, size_t* size_out)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)mod;
    IMAGE_NT_HEADERS* nt  = (IMAGE_NT_HEADERS*)((uint8_t*)mod + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (memcmp(sec[i].Name, ".text", 5) == 0) {
            if (size_out) *size_out = sec[i].Misc.VirtualSize;
            return (uintptr_t)mod + sec[i].VirtualAddress;
        }
    }
    return 0;
}

uintptr_t rbx_pe_text(HMODULE mod, size_t* out_size)
{
    return module_text_base(mod, out_size);
}

HMODULE rbx_pe_find_roblox(void)
{
    HMODULE m = GetModuleHandleW(L"RobloxPlayerBeta.exe");
    if (m) return m;
    m = GetModuleHandleW(NULL);
    return m;
}
