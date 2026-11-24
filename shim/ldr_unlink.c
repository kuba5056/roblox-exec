#include "shim.h"

typedef struct _UNICODE_STRING_X {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING_X;

typedef struct _LDR_DATA_TABLE_ENTRY_X {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID      DllBase;
    PVOID      EntryPoint;
    ULONG      SizeOfImage;
    UNICODE_STRING_X FullDllName;
    UNICODE_STRING_X BaseDllName;
    ULONG      Flags;
    USHORT     LoadCount;
    USHORT     TlsIndex;
    LIST_ENTRY HashLinks;
} LDR_DATA_TABLE_ENTRY_X;

typedef struct _PEB_LDR_DATA_X {
    ULONG      Length;
    BOOLEAN    Initialized;
    PVOID      SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} PEB_LDR_DATA_X;

typedef struct _PEB_X {
    BYTE Reserved1[2];
    BYTE BeingDebugged;
    BYTE Reserved2[1];
    PVOID Reserved3[2];
    PEB_LDR_DATA_X* Ldr;
} PEB_X;

static PEB_X* get_peb(void)
{
#if defined(__x86_64__)
    return (PEB_X*)__readgsqword(0x60);
#else
    return (PEB_X*)__readfsdword(0x30);
#endif
}

static void unlink(LIST_ENTRY* e)
{
    e->Blink->Flink = e->Flink;
    e->Flink->Blink = e->Blink;
    e->Flink = e->Blink = e;
}

void rbx_ldr_unlink(HMODULE self)
{
    PEB_X* peb = get_peb();
    if (!peb || !peb->Ldr) return;
    LIST_ENTRY* head = &peb->Ldr->InLoadOrderModuleList;
    for (LIST_ENTRY* it = head->Flink; it && it != head; it = it->Flink) {
        LDR_DATA_TABLE_ENTRY_X* e =
            (LDR_DATA_TABLE_ENTRY_X*)((uint8_t*)it - 0);
        if (e->DllBase == (PVOID)self) {
            unlink(&e->InLoadOrderLinks);
            unlink(&e->InMemoryOrderLinks);
            unlink(&e->InInitializationOrderLinks);
            unlink(&e->HashLinks);
            if (e->BaseDllName.Buffer) {
                for (USHORT i = 0; i < e->BaseDllName.Length/2; ++i)
                    e->BaseDllName.Buffer[i] = 0;
            }
            if (e->FullDllName.Buffer) {
                for (USHORT i = 0; i < e->FullDllName.Length/2; ++i)
                    e->FullDllName.Buffer[i] = 0;
            }
            return;
        }
    }
}
