// Host-side unit test for rbx_scan_roblox's pattern engine.
// Build native (linux) with the scan logic re-linked in a tiny
// stub rbx_vm_t. Not for shipping — only `make test`.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

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

static size_t find_sig(const uint8_t* mem, size_t size, const sig_t* s)
{
    size_t plen = strlen(s->mask);
    for (size_t i = 0; i + plen < size; ++i) {
        if (match(mem + i, (const uint8_t*)s->pattern, s->mask)) {
            return i;
        }
    }
    return (size_t)-1;
}

int main(void)
{
    uint8_t blob[4096] = {0};
    for (size_t i = 0; i < sizeof(blob); ++i) blob[i] = (uint8_t)(i * 7);

    // Plant the luau_load prologue at offset 0x400.
    const uint8_t luau_load_bytes[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08,
        0x48, 0x89, 0x74, 0x24, 0x10,
        0x57, 0x48, 0x83, 0xEC, 0x40,
        0x49, 0x8B, 0xF0
    };
    memcpy(blob + 0x400, luau_load_bytes, sizeof(luau_load_bytes));

    sig_t s = {
        "luau_load",
        "\x48\x89\x5C\x24\x00\x48\x89\x74\x24\x00\x57\x48\x83\xEC\x00\x49\x8B\xF0",
        "xxxx?xxxx?xxxx?xxx", 0, 0
    };

    size_t hit = find_sig(blob, sizeof(blob), &s);
    if (hit != 0x400) {
        fprintf(stderr, "FAIL: expected 0x400, got 0x%zx\n", hit);
        return 1;
    }
    printf("PASS: scan_test hit luau_load at 0x%zx\n", hit);

    // Negative test: wipe the plant, expect miss.
    memset(blob + 0x400, 0xCC, sizeof(luau_load_bytes));
    size_t miss = find_sig(blob, sizeof(blob), &s);
    if (miss != (size_t)-1) {
        fprintf(stderr, "FAIL: negative case got 0x%zx\n", miss);
        return 2;
    }
    printf("PASS: scan_test negative case clean\n");
    return 0;
}
