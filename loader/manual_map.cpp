#include <windows.h>
#include <vector>
#include <cstdint>
#include <cstring>

struct Reloc { uint16_t off : 12; uint16_t type : 4; };

extern "C" bool manual_map(HANDLE hProc, const std::vector<uint8_t>& dll,
                           void** out_remote_base, void** out_entry)
{
    auto* dos = (const IMAGE_DOS_HEADER*)dll.data();
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt  = (const IMAGE_NT_HEADERS64*)(dll.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    SIZE_T image_size = nt->OptionalHeader.SizeOfImage;
    void* remote = VirtualAllocEx(hProc, nullptr, image_size,
                                  MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remote) return false;

    std::vector<uint8_t> local(image_size, 0);
    std::memcpy(local.data(), dll.data(), nt->OptionalHeader.SizeOfHeaders);

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (sec[i].SizeOfRawData) {
            std::memcpy(local.data() + sec[i].VirtualAddress,
                        dll.data()  + sec[i].PointerToRawData,
                        sec[i].SizeOfRawData);
        }
    }

    uint64_t delta = (uint64_t)remote - nt->OptionalHeader.ImageBase;
    const auto& reloc_dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && reloc_dir.Size) {
        auto* block = (IMAGE_BASE_RELOCATION*)(local.data() + reloc_dir.VirtualAddress);
        auto* end   = (IMAGE_BASE_RELOCATION*)((uint8_t*)block + reloc_dir.Size);
        while (block < end && block->SizeOfBlock) {
            size_t count = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2;
            auto* r = (Reloc*)((uint8_t*)block + sizeof(IMAGE_BASE_RELOCATION));
            for (size_t j = 0; j < count; ++j) {
                if (r[j].type == IMAGE_REL_BASED_DIR64) {
                    auto* p = (uint64_t*)(local.data() + block->VirtualAddress + r[j].off);
                    *p += delta;
                }
            }
            block = (IMAGE_BASE_RELOCATION*)((uint8_t*)block + block->SizeOfBlock);
        }
    }

    const auto& imp_dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imp_dir.Size) {
        auto* imp = (IMAGE_IMPORT_DESCRIPTOR*)(local.data() + imp_dir.VirtualAddress);
        for (; imp->Name; ++imp) {
            const char* dllname = (const char*)(local.data() + imp->Name);
            HMODULE mod = LoadLibraryA(dllname);
            if (!mod) return false;
            auto* orig  = (uint64_t*)(local.data() +
                (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            auto* first = (uint64_t*)(local.data() + imp->FirstThunk);
            for (; *orig; ++orig, ++first) {
                FARPROC fn = nullptr;
                if (*orig & IMAGE_ORDINAL_FLAG64) {
                    fn = GetProcAddress(mod, (LPCSTR)(*orig & 0xFFFF));
                } else {
                    auto* name = (IMAGE_IMPORT_BY_NAME*)(local.data() + *orig);
                    fn = GetProcAddress(mod, name->Name);
                }
                if (!fn) return false;
                *first = (uint64_t)fn;
            }
        }
    }

    SIZE_T written = 0;
    if (!WriteProcessMemory(hProc, remote, local.data(), image_size, &written))
        return false;

    *out_remote_base  = remote;
    *out_entry        = (uint8_t*)remote + nt->OptionalHeader.AddressOfEntryPoint;
    return true;
}
