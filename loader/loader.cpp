// rbx-loader.exe — wine-side launcher. CreateProcess suspended on
// RobloxPlayerBeta.exe (or the sober-launched child), manual-maps
// shim64.dll, APC-queues the entrypoint, resumes.
//
// usage:
//   rbx-loader.exe --exe "Z:\\...\\RobloxPlayerBeta.exe" \
//                  --shim "Z:\\...\\shim64.dll" [--args ...]

#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

extern "C" bool manual_map(HANDLE hProc, const std::vector<uint8_t>& dll,
                           void** out_remote_base, void** out_entry);
extern "C" bool apc_inject_run(HANDLE hThread, void* remote_fn, void* arg);

static std::vector<uint8_t> read_file(const std::wstring& p)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(h, &sz);
    std::vector<uint8_t> buf((size_t)sz.QuadPart);
    DWORD got = 0;
    ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr);
    CloseHandle(h);
    buf.resize(got);
    return buf;
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring exe, shim, extra;
    for (int i = 1; i + 1 < argc; ++i) {
        std::wstring a = argv[i];
        if      (a == L"--exe")  exe  = argv[++i];
        else if (a == L"--shim") shim = argv[++i];
        else if (a == L"--args") extra = argv[++i];
    }
    if (exe.empty() || shim.empty()) {
        fwprintf(stderr, L"need --exe and --shim\n");
        return 2;
    }

    std::wstring cmd = L"\"" + exe + L"\" " + extra;
    STARTUPINFOW si{ sizeof(si) };
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError());
        return 3;
    }

    auto dll = read_file(shim);
    if (dll.empty()) {
        TerminateProcess(pi.hProcess, 1);
        fwprintf(stderr, L"shim unreadable\n");
        return 4;
    }

    void* remote_base = nullptr;
    void* remote_entry = nullptr;
    if (!manual_map(pi.hProcess, dll, &remote_base, &remote_entry)) {
        TerminateProcess(pi.hProcess, 1);
        fwprintf(stderr, L"manual map failed\n");
        return 5;
    }

    if (!apc_inject_run(pi.hThread, remote_entry, remote_base)) {
        TerminateProcess(pi.hProcess, 1);
        fwprintf(stderr, L"apc inject failed\n");
        return 6;
    }

    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
