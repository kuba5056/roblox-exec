#include <windows.h>
#include <cstdint>

// Build a tiny shellcode stub that calls DllMain(base, DLL_PROCESS_ATTACH, 0).
// Queue it as a user-mode APC on the suspended main thread.

static const uint8_t STUB[] = {
    // mov rcx, imm64   (base)
    0x48, 0xB9, 0,0,0,0,0,0,0,0,
    // mov rdx, 1       (DLL_PROCESS_ATTACH)
    0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00,
    // xor r8, r8
    0x4D, 0x31, 0xC0,
    // mov rax, imm64   (entry)
    0x48, 0xB8, 0,0,0,0,0,0,0,0,
    // sub rsp, 0x28
    0x48, 0x83, 0xEC, 0x28,
    // call rax
    0xFF, 0xD0,
    // add rsp, 0x28
    0x48, 0x83, 0xC4, 0x28,
    // ret
    0xC3,
};

extern "C" bool apc_inject_run(HANDLE hThread, void* remote_fn, void* arg)
{
    HANDLE hProc = GetCurrentProcess();
    // The thread is suspended in the target process; reach back to that proc.
    DWORD tid = GetThreadId(hThread);
    (void)tid;

    HANDLE target = NULL;
    DuplicateHandle(hProc, GetCurrentProcess(), hProc, &target,
                    0, FALSE, DUPLICATE_SAME_ACCESS);

    // Query the thread's owning process via NT.
    typedef NTSTATUS (NTAPI *NtQueryInfoThread_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto qit = (NtQueryInfoThread_t)GetProcAddress(nt, "NtQueryInformationThread");
    struct { NTSTATUS ExitStatus; PVOID TebBaseAddress;
             struct { HANDLE UniqueProcess; HANDLE UniqueThread; } Cid;
             KAFFINITY AffinityMask; LONG Priority; LONG BasePriority; } tbi{};
    ULONG rl = 0;
    qit(hThread, 0, &tbi, sizeof(tbi), &rl);
    HANDLE hTargetProc = OpenProcess(
        PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ|PROCESS_CREATE_THREAD,
        FALSE, (DWORD)(uintptr_t)tbi.Cid.UniqueProcess);
    if (!hTargetProc) return false;

    size_t stub_size = sizeof(STUB);
    void* rstub = VirtualAllocEx(hTargetProc, nullptr, stub_size,
                                 MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!rstub) { CloseHandle(hTargetProc); return false; }

    uint8_t buf[sizeof(STUB)];
    memcpy(buf, STUB, sizeof(STUB));
    *(uint64_t*)(buf +  2) = (uint64_t)arg;        // base
    *(uint64_t*)(buf + 22) = (uint64_t)remote_fn;  // entry

    SIZE_T w = 0;
    if (!WriteProcessMemory(hTargetProc, rstub, buf, stub_size, &w)) {
        CloseHandle(hTargetProc);
        return false;
    }

    QueueUserAPC((PAPCFUNC)rstub, hThread, 0);
    CloseHandle(hTargetProc);
    return true;
}
