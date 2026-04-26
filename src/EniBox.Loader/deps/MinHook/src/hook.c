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

/* ---- x86/x64 instruction length decoding (enhanced) ---- */

/* Decode ModRM byte and return the total size of the ModRM + SIB + displacement bytes.
 * This handles all addressing modes: register, memory with SIB, disp8, disp32, RIP-relative. */
static size_t DecodeModRM(const uint8_t* pModRM, uint8_t opcode) {
    uint8_t modrm = pModRM[0];
    uint8_t mod = (modrm >> 6) & 3;
    uint8_t rm = modrm & 7;
    size_t size = 1; /* ModRM byte itself */

#ifdef _WIN64
    /* In 64-bit mode, mod=00 rm=5 means RIP-relative (disp32) */
    if (mod == 0 && rm == 5) return size + 4; /* disp32 */
#endif

    /* SIB byte follows if mod != 3 and rm == 4 (ESP/RSP) */
    if (mod != 3 && rm == 4) {
        size += 1; /* SIB byte */
        uint8_t sib = pModRM[1];
        uint8_t base = sib & 7;
        /* SIB with mod=00 and base=5 means disp32 (no base register) */
        if (mod == 0 && base == 5) return size + 4; /* disp32 */
    }

    /* Displacement based on mod */
    if (mod == 1) size += 1; /* disp8 */
    else if (mod == 2) size += 4; /* disp32 */

    /* Immediate operand size based on opcode group */
    if (opcode == 0x81 || opcode == 0xC1 || opcode == 0xC7) size += 4; /* imm32 */
    else if (opcode == 0x80 || opcode == 0x83 || opcode == 0xC0 || opcode == 0xC6) size += 1; /* imm8 */

    return size;
}

/* Get the length of the instruction at pCode so we know how many bytes to overwrite.
 * Enhanced version covering: REX prefixes, 0F two-byte opcodes, common SSE/AVX patterns,
 * ModRM/SIB addressing, and immediate operands.
 * Returns 0 for unrecognized opcodes (should not happen for valid function prologues). */
static size_t GetInstructionLength(void* pCode) {
    uint8_t* p = (uint8_t*)pCode;
    uint8_t opcode = p[0];
    size_t offset = 0;

#ifdef _WIN64
    /* REX prefix: 0x40-0x4F */
    uint8_t rex = 0;
    if ((opcode & 0xF0) == 0x40) {
        rex = opcode;
        opcode = p[++offset];
    }
#endif

    /* Single-byte opcodes */
    switch (opcode) {
    /* 1-byte: no operands */
    case 0xC3: case 0xCB: case 0xCC: case 0x90: case 0xF4: /* ret/retn/int3/nop/hlt */
        return offset + 1;
    case 0xC2: case 0xCA: /* ret imm16 / retf imm16 */
        return offset + 3;

    /* push/pop r32 (no REX) or push/pop r64 (with REX.W) */
    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        return offset + 1;

    /* push imm8 / push imm32 */
    case 0x6A: return offset + 2;
    case 0x68:
#ifdef _WIN64
        return offset + (rex ? 9 : 5); /* REX.W push imm64, else push imm32 */
#else
        return offset + 5;
#endif

    /* mov r32/64, imm32/64 */
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
#ifdef _WIN64
        return offset + (rex & 0x08 ? 9 : 5); /* REX.W: mov r64, imm64 (8 bytes), else imm32 */
#else
        return offset + 5;
#endif

    /* call rel32 / jmp rel32 */
    case 0xE8: case 0xE9: return offset + 5;
    /* jmp rel8 */
    case 0xEB: return offset + 2;
    /* jcc rel8 */
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
        return offset + 2;

    /* ALU opcodes with ModRM: add/or/adc/sbb/and/sub/xor/cmp r/m, r or r, r/m */
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x30: case 0x31: case 0x32: case 0x33:
    case 0x38: case 0x39: case 0x3A: case 0x3B:
    /* mov, lea, test, xchg */
    case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B:
    case 0x8D:
    /* inc/dec r/m (32/64-bit mode) */
    case 0xFF:
    /* group1: add/or/adc/sbb/and/sub/xor/cmp r/m, imm */
    case 0x80: case 0x81: case 0x83:
    /* shift group2 */
    case 0xC0: case 0xC1:
    /* mov r/m, imm */
    case 0xC6: case 0xC7:
    /* fpu modrm */
    case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
        return offset + 1 + DecodeModRM(p + offset + 1, opcode);

    /* 0F two-byte opcode prefix */
    case 0x0F: {
        uint8_t opcode2 = p[offset + 1];
        /* jcc rel32 (0F 80-8F) */
        if (opcode2 >= 0x80 && opcode2 <= 0x8F) return offset + 6;
        /* near jmp (0F FF), near call (0F FE) - ModRM */
        if (opcode2 == 0xFF || opcode2 == 0xFE) return offset + 2 + DecodeModRM(p + offset + 2, opcode2);
        /* movq/movdqa/movdqu and other SSE2 with ModRM (0F 6F, 0F 7F, 0F 28-2F, etc.) */
        if ((opcode2 >= 0x10 && opcode2 <= 0x17) || /* movups/movss/movupd/movsd */
            (opcode2 >= 0x28 && opcode2 <= 0x2F) || /* movaps/movss/movapd/movsd */
            (opcode2 >= 0x50 && opcode2 <= 0x5F) || /* movmskps/sqrtps/andps/etc */
            (opcode2 >= 0x60 && opcode2 <= 0x6F) || /* punpcklwd/etc/packuswb/movq */
            (opcode2 >= 0x70 && opcode2 <= 0x76) || /* pshufd/etc/pcmpeqd/emms */
            opcode2 == 0x7E || opcode2 == 0x7F ||    /* movq/movdqa */
            (opcode2 >= 0xA0 && opcode2 <= 0xAF) || /* push/pop fs/gs, imul, bsf/bsr, movsx */
            (opcode2 >= 0xB0 && opcode2 <= 0xBF) || /* movsx/movzx with ModRM */
            (opcode2 >= 0xC0 && opcode2 <= 0xC6) || /* xadd/bswap/cmpxchg/etc */
            (opcode2 >= 0xD0 && opcode2 <= 0xDF) || /* psrlw/etc/pavgb/etc */
            (opcode2 >= 0xE0 && opcode2 <= 0xEF) || /* pshuflw/etc/pxor/etc */
            (opcode2 >= 0xF0 && opcode2 <= 0xFF))   /* lddqu/psadbw/maskmovdqu/etc */
        {
            /* 0F + opcode2 + ModRM */
            return offset + 2 + DecodeModRM(p + offset + 2, opcode2);
        }
        /* 0F 1F: multi-byte NOP (with ModRM) */
        if (opcode2 == 0x1F) return offset + 2 + DecodeModRM(p + offset + 2, opcode2);
        /* 0F 0D: prefetchw (with ModRM) */
        if (opcode2 == 0x0D) return offset + 2 + DecodeModRM(p + offset + 2, opcode2);
        /* Default for unrecognized 0F: assume ModRM follows */
        return offset + 2 + DecodeModRM(p + offset + 2, opcode2);
    }

#ifdef _WIN64
    /* VEX 2-byte prefix: C5 + R vvvv L pp + opcode + ModRM
     * C5 encodes VEX with R=1 (no REX.R inversion), map 1 only.
     * Format: C5 [R vvvv L pp] opcode ModRM [imm8] */
    case 0xC5: {
        uint8_t byte2 = p[offset + 1];
        uint8_t opcode2 = p[offset + 2];
        /* VEX.C5 always maps to 0F opcode map (map1) */
        size_t len = 3 + DecodeModRM(p + offset + 3, opcode2);
        /* Some VEX-encoded instructions have an imm8 operand */
        /* Check for instructions that need imm8: VPSHUFB, VROUNDPS, etc. */
        /* For safety, check common VEX map1 opcodes with imm8 */
        if (opcode2 == 0x70 || opcode2 == 0x71 || opcode2 == 0x72 || opcode2 == 0x73 ||  /* VPSHUFD/VPSHUFLW/VPSHUFHW/VPSHUFB */
            opcode2 == 0x0F || opcode2 == 0x1F)  /* VRNDSCALE/... */
            len += 1; /* imm8 */
        return offset + len;
    }

    /* VEX 3-byte prefix: C4 + [R X B mmmmm] + [W vvvv L pp] + opcode + ModRM [imm8]
     * C4 can encode maps 0F, 0F38, 0F3A, and also XOP maps 8/9/A. */
    case 0xC4: {
        uint8_t byte2 = p[offset + 1];
        uint8_t byte3 = p[offset + 2];
        uint8_t opcode2 = p[offset + 3];
        uint8_t map_select = byte2 & 0x1F; /* mmmmm field */
        size_t len = 4 + DecodeModRM(p + offset + 4, opcode2);
        /* Map 0F3A (map_select == 3) instructions have an imm8 */
        if (map_select == 3) len += 1;
        /* Some map 0F (map_select == 1) instructions also have imm8 */
        if (map_select == 1 && (opcode2 == 0x70 || opcode2 == 0x71 || opcode2 == 0x72 || opcode2 == 0x73))
            len += 1;
        return offset + len;
    }

    /* EVEX 4-byte prefix: 62 + [R X B R' 00 mmmm] + [W vvvv 1 pp z L' L b] + opcode + ModRM [imm8]
     * EVEX extends VEX for AVX-512. Only valid in 64-bit mode. */
    case 0x62: {
        uint8_t byte2 = p[offset + 1];
        uint8_t byte3 = p[offset + 2];
        uint8_t opcode2 = p[offset + 4];
        uint8_t map_select = byte2 & 0x0F; /* mmmm field (4 bits for EVEX) */
        size_t len = 5 + DecodeModRM(p + offset + 5, opcode2);
        /* EVEX map 3 (0F3A equivalent) has imm8 */
        if (map_select == 3) len += 1;
        return offset + len;
    }
#endif

    default:
        /* Unrecognized opcode - return 1 byte as fallback */
        return offset + 1;
    }
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
