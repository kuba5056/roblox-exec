#include "shim.h"

static HANDLE g_log_pipe = INVALID_HANDLE_VALUE;

void rbx_pipe_log(const char* s)
{
    if (g_log_pipe == INVALID_HANDLE_VALUE) return;
    rbx_msg_t m;
    m.kind = IPC_LOG;
    m.len  = (uint32_t)strlen(s);
    DWORD w = 0;
    WriteFile(g_log_pipe, &m, sizeof(m), &w, NULL);
    WriteFile(g_log_pipe, s, m.len, &w, NULL);
}

static int send_result(HANDLE h, int code, const char* text)
{
    rbx_msg_t m;
    m.kind = IPC_RESULT;
    m.len  = 4 + (uint32_t)(text ? strlen(text) : 0);
    DWORD w = 0;
    if (!WriteFile(h, &m, sizeof(m), &w, NULL)) return 0;
    if (!WriteFile(h, &code, 4, &w, NULL)) return 0;
    if (text && *text) WriteFile(h, text, (DWORD)strlen(text), &w, NULL);
    return 1;
}

static int read_full(HANDLE h, void* buf, DWORD n)
{
    DWORD got = 0;
    while (got < n) {
        DWORD k = 0;
        if (!ReadFile(h, (uint8_t*)buf + got, n - got, &k, NULL) || k == 0)
            return 0;
        got += k;
    }
    return 1;
}

int rbx_pipe_server(void)
{
    HANDLE pipe = CreateNamedPipeW(
        RBX_PIPE_NAME,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 1<<16, 1<<16, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) return -1;

    while (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
        g_log_pipe = pipe;
        rbx_pipe_log("ipc connected");

        for (;;) {
            rbx_msg_t m;
            if (!read_full(pipe, &m, sizeof(m))) break;

            char* payload = (char*)HeapAlloc(GetProcessHeap(), 0, m.len + 1);
            if (!payload) break;
            if (m.len && !read_full(pipe, payload, m.len)) {
                HeapFree(GetProcessHeap(), 0, payload);
                break;
            }
            payload[m.len] = 0;

            char err[512] = {0};
            int rc = 0;

            switch (m.kind) {
            case IPC_HELLO:
                send_result(pipe, RBX_SHIM_VERSION, "rbx-exec shim");
                break;
            case IPC_EXEC_BYTECODE:
                rc = rbx_luau_exec_bytecode(payload, m.len, err, sizeof(err));
                send_result(pipe, rc, err);
                break;
            case IPC_EXEC_SOURCE:
                rc = rbx_luau_exec_source(payload, m.len, err, sizeof(err));
                send_result(pipe, rc, err);
                break;
            case IPC_EJECT:
                HeapFree(GetProcessHeap(), 0, payload);
                goto done;
            default:
                send_result(pipe, -99, "unknown kind");
                break;
            }

            HeapFree(GetProcessHeap(), 0, payload);
        }
    done:
        g_log_pipe = INVALID_HANDLE_VALUE;
        DisconnectNamedPipe(pipe);
        break;
    }

    CloseHandle(pipe);
    return 0;
}
