/*
 * MinHook - The Minimalistic API Hooking Library
 * Simplified implementation for EniBox Loader
 * 
 * Original MinHook by Tsuda Kageyu (BSD 2-Clause License)
 * This is a simplified standalone implementation.
 */

#include "MinHook.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- Hook entry management ---- */

#define MAX_HOOKS 128

typedef struct _HOOK_ENTRY {
    void*   target;       /* Original function address */
    void*   detour;       /* Detour function address */
    void*   original;     /* Trampoline (call original from here) */
    void*   trampoline;   /* Allocated trampoline memory */
    size_t  trampolineSize;
    BOOL    enabled;
    BOOL    created;
    /* Original bytes saved for unhooking */
    uint8_t origBytes[16];
    size_t  origSize;
} HOOK_ENTRY;

static HOOK_ENTRY g_hooks[MAX_HOOKS];
static int g_hookCount = 0;
static BOOL g_initialized = FALSE;

/* ---- x86/x64 instruction length decoding (simplified) ---- */

/* Get the length of the instruction at pCode so we know how many bytes to overwrite.
 * This is a simplified version - a full implementation would use a proper disassembler.
 * For common function prologues, this is sufficient. */
static size_t GetInstructionLength(void* pCode) {
    uint8_t* p = (uint8_t*)pCode;
    uint8_t opcode = p[0];

    /* Common single-byte opcodes */
    if (opcode == 0xC3 || opcode == 0xCB || opcode == 0xC2 || opcode == 0xCA) return 1; /* ret/retn */
    if (opcode == 0xCC) return 1; /* int3 */
    if (opcode == 0x90) return 1; /* nop */

    /* Two-byte opcodes with ModRM */
    if (opcode == 0x89 || opcode == 0x8B || opcode == 0x8D ||  /* mov */
        opcode == 0x83 || opcode == 0x81 ||                     /* add/or/adc/sbb/and/sub/xor/cmp imm */
        opcode == 0x85 || opcode == 0x84 ||                     /* test */
        opcode == 0x39 || opcode == 0x3B ||                     /* cmp */
        opcode == 0x23 || opcode == 0x0B ||                     /* and/or */
        opcode == 0x33 || opcode == 0x31 ||                     /* xor */
        opcode == 0x29 || opcode == 0x2B ||                     /* sub */
        opcode == 0x01 || opcode == 0x03 ||                     /* add */
        opcode == 0x50 || opcode == 0x51 || opcode == 0x52 ||   /* push r32 */
        opcode == 0x53 || opcode == 0x54 || opcode == 0x55 ||
        opcode == 0x56 || opcode == 0x57 ||
        opcode == 0x58 || opcode == 0x59 || opcode == 0x5A ||   /* pop r32 */
        opcode == 0x5B || opcode == 0x5C || opcode == 0x5D ||
        opcode == 0x5E || opcode == 0x5F) {
        /* Check for ModRM */
        uint8_t modrm = p[1];
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t rm = modrm & 7;
        if (mod == 0 && rm == 5) return 6; /* disp32 */
        if (mod == 0 && rm == 4) return 3; /* SIB */
        if (mod == 1) return 3; /* disp8 */
        if (mod == 2) return 6; /* disp32 */
        return 2;
    }

    /* push imm8 / push imm32 */
    if (opcode == 0x6A) return 2;
    if (opcode == 0x68) return 5;

    /* mov r32, imm32 */
    if (opcode >= 0xB8 && opcode <= 0xBF) return 5;

    /* call rel32 / jmp rel32 */
    if (opcode == 0xE8 || opcode == 0xE9) return 5;

    /* jmp rel8 */
    if (opcode == 0xEB) return 2;

    /* lea / mov with ModRM and possible SIB */
    if (opcode == 0xFF) {
        uint8_t modrm = p[1];
        uint8_t mod = (modrm >> 6) & 3;
        uint8_t rm = modrm & 7;
        if (mod == 0 && rm == 4) return 3;
        if (mod == 1) return 3;
        if (mod == 2) return 6;
        return 2;
    }

    /* sub/add/cmp esp, imm8 */
    if (opcode == 0x83) return 4;

#ifdef _WIN64
    /* REX prefix for x64 */
    if ((opcode & 0xF0) == 0x40) {
        /* REX + opcode + ModRM */
        return 3; /* Simplified - most common case */
    }
#endif

    /* Default: assume 1 byte (will be overridden for common prologues) */
    return 1;
}

/* Calculate the number of bytes we need to steal from the target function.
 * Must be >= 5 (for jmp rel32) on x86, or >= 14 (for jmp abs) on x64. */
static size_t GetHookSize(void* pTarget) {
    size_t totalSize = 0;
    uint8_t* p = (uint8_t*)pTarget;

#ifdef _WIN64
    /* On x64, we need at least 14 bytes for an absolute jump:
     * mov rax, addr (10 bytes) + jmp rax (2 bytes) = 12 bytes
     * Or: push rax (1) + mov rax,addr (10) + xchg [rsp],rax (3) = 14 bytes */
    while (totalSize < 14) {
        size_t len = GetInstructionLength(p + totalSize);
        if (len == 0) return 0;
        totalSize += len;
    }
#else
    /* On x86, we need at least 5 bytes for: jmp rel32 */
    while (totalSize < 5) {
        size_t len = GetInstructionLength(p + totalSize);
        if (len == 0) return 0;
        totalSize += len;
    }
#endif

    return totalSize;
}

/* ---- Trampoline and patching ---- */

static MH_STATUS CreateTrampoline(HOOK_ENTRY* hook) {
    size_t hookSize = GetHookSize(hook->target);
    if (hookSize == 0 || hookSize > sizeof(hook->origBytes))
        return MH_ERROR_UNSUPPORTED_FUNCTION;

    /* Save original bytes */
    memcpy(hook->origBytes, hook->target, hookSize);
    hook->origSize = hookSize;

    /* Allocate executable memory for trampoline */
    size_t trampolineSize = hookSize + 16; /* Original bytes + jump back */
    void* trampoline = VirtualAlloc(NULL, trampolineSize,
                                     MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline)
        return MH_ERROR_MEMORY_ALLOC;

    hook->trampoline = trampoline;
    hook->trampolineSize = trampolineSize;

    /* Copy original instructions to trampoline */
    memcpy(trampoline, hook->origBytes, hookSize);

    /* Append a jump back to the original function (after the stolen bytes) */
    uint8_t* jumpBack = (uint8_t*)trampoline + hookSize;
    uintptr_t targetAddr = (uintptr_t)hook->target + hookSize;

#ifdef _WIN64
    /* x64: mov rax, addr; jmp rax */
    jumpBack[0] = 0x48; /* REX.W */
    jumpBack[1] = 0xB8; /* mov rax, imm64 */
    *(uintptr_t*)(jumpBack + 2) = targetAddr;
    jumpBack[10] = 0xFF; /* jmp rax */
    jumpBack[11] = 0xE0;
#else
    /* x86: jmp rel32 */
    jumpBack[0] = 0xE9; /* jmp rel32 */
    *(int32_t*)(jumpBack + 1) = (int32_t)(targetAddr - ((uintptr_t)jumpBack + 5));
#endif

    hook->original = trampoline;
    return MH_OK;
}

static MH_STATUS PatchTarget(HOOK_ENTRY* hook) {
    /* Change memory protection */
    DWORD oldProtect;
    if (!VirtualProtect(hook->target, hook->origSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return MH_ERROR_MEMORY_PROTECT;

    uint8_t* pTarget = (uint8_t*)hook->target;

#ifdef _WIN64
    /* x64: push rax; mov rax, detour; xchg [rsp], rax; ret */
    /* This pushes the return address, then jumps to detour.
     * Simpler approach: mov rax, addr; jmp rax (12 bytes) */
    pTarget[0] = 0x48; /* REX.W */
    pTarget[1] = 0xB8; /* mov rax, imm64 */
    *(uintptr_t*)(pTarget + 2) = (uintptr_t)hook->detour;
    pTarget[10] = 0xFF; /* jmp rax */
    pTarget[11] = 0xE0;
    /* Fill remaining with NOPs */
    for (size_t i = 12; i < hook->origSize; i++)
        pTarget[i] = 0x90;
#else
    /* x86: jmp rel32 */
    pTarget[0] = 0xE9; /* jmp rel32 */
    *(int32_t*)(pTarget + 1) = (int32_t)((uintptr_t)hook->detour - ((uintptr_t)hook->target + 5));
    /* Fill remaining with NOPs */
    for (size_t i = 5; i < hook->origSize; i++)
        pTarget[i] = 0x90;
#endif

    /* Restore protection */
    DWORD dummy;
    VirtualProtect(hook->target, hook->origSize, oldProtect, &dummy);

    /* Flush instruction cache */
    FlushInstructionCache(GetCurrentProcess(), hook->target, hook->origSize);

    return MH_OK;
}

static MH_STATUS UnpatchTarget(HOOK_ENTRY* hook) {
    DWORD oldProtect;
    if (!VirtualProtect(hook->target, hook->origSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return MH_ERROR_MEMORY_PROTECT;

    /* Restore original bytes */
    memcpy(hook->target, hook->origBytes, hook->origSize);

    DWORD dummy;
    VirtualProtect(hook->target, hook->origSize, oldProtect, &dummy);
    FlushInstructionCache(GetCurrentProcess(), hook->target, hook->origSize);

    return MH_OK;
}

/* ---- MinHook API implementation ---- */

MH_STATUS MH_Initialize(void) {
    if (g_initialized)
        return MH_ERROR_ALREADY_INITIALIZED;

    memset(g_hooks, 0, sizeof(g_hooks));
    g_hookCount = 0;
    g_initialized = TRUE;
    return MH_OK;
}

MH_STATUS MH_Uninitialize(void) {
    if (!g_initialized)
        return MH_ERROR_NOT_INITIALIZED;

    /* Disable and free all hooks */
    for (int i = 0; i < g_hookCount; i++) {
        if (g_hooks[i].enabled)
            UnpatchTarget(&g_hooks[i]);
        if (g_hooks[i].trampoline)
            VirtualFree(g_hooks[i].trampoline, 0, MEM_RELEASE);
    }

    g_hookCount = 0;
    g_initialized = FALSE;
    return MH_OK;
}

MH_STATUS MH_CreateHook(void* pTarget, void* pDetour, void** ppOriginal) {
    if (!g_initialized)
        return MH_ERROR_NOT_INITIALIZED;
    if (!pTarget || !pDetour || !ppOriginal)
        return MH_ERROR_UNSUPPORTED_FUNCTION;
    if (g_hookCount >= MAX_HOOKS)
        return MH_ERROR_MAX_HOOKS;

    /* Check if already hooked */
    for (int i = 0; i < g_hookCount; i++) {
        if (g_hooks[i].target == pTarget) {
            *ppOriginal = g_hooks[i].original;
            return MH_ERROR_ENABLED_HOOK_EXISTS;
        }
    }

    HOOK_ENTRY* hook = &g_hooks[g_hookCount];
    hook->target = pTarget;
    hook->detour = pDetour;
    hook->enabled = FALSE;
    hook->created = TRUE;

    MH_STATUS status = CreateTrampoline(hook);
    if (status != MH_OK) return status;

    *ppOriginal = hook->original;
    g_hookCount++;
    return MH_OK;
}

MH_STATUS MH_EnableHook(void* pTarget) {
    if (!g_initialized)
        return MH_ERROR_NOT_INITIALIZED;

    if (pTarget == MH_ALL_HOOKS) {
        MH_STATUS result = MH_OK;
        for (int i = 0; i < g_hookCount; i++) {
            if (g_hooks[i].created && !g_hooks[i].enabled) {
                MH_STATUS s = PatchTarget(&g_hooks[i]);
                if (s == MH_OK)
                    g_hooks[i].enabled = TRUE;
                else
                    result = s;
            }
        }
        return result;
    }

    for (int i = 0; i < g_hookCount; i++) {
        if (g_hooks[i].target == pTarget) {
            if (g_hooks[i].enabled)
                return MH_OK;
            MH_STATUS s = PatchTarget(&g_hooks[i]);
            if (s == MH_OK)
                g_hooks[i].enabled = TRUE;
            return s;
        }
    }

    return MH_ERROR_HOOK_NOT_FOUND;
}

MH_STATUS MH_DisableHook(void* pTarget) {
    if (!g_initialized)
        return MH_ERROR_NOT_INITIALIZED;

    if (pTarget == MH_ALL_HOOKS) {
        MH_STATUS result = MH_OK;
        for (int i = 0; i < g_hookCount; i++) {
            if (g_hooks[i].enabled) {
                MH_STATUS s = UnpatchTarget(&g_hooks[i]);
                if (s == MH_OK)
                    g_hooks[i].enabled = FALSE;
                else
                    result = s;
            }
        }
        return result;
    }

    for (int i = 0; i < g_hookCount; i++) {
        if (g_hooks[i].target == pTarget) {
            if (!g_hooks[i].enabled)
                return MH_OK;
            MH_STATUS s = UnpatchTarget(&g_hooks[i]);
            if (s == MH_OK)
                g_hooks[i].enabled = FALSE;
            return s;
        }
    }

    return MH_ERROR_HOOK_NOT_FOUND;
}

MH_STATUS MH_QueueHook(void* pTarget) {
    /* Simplified - just enable immediately */
    return MH_EnableHook(pTarget);
}

MH_STATUS MH_DequeueHook(void* pTarget) {
    return MH_DisableHook(pTarget);
}

MH_STATUS MH_ApplyQueued(void) {
    return MH_OK;
}
