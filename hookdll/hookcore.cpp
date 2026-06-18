#include "hook.h"
#include "MinHook.h"

#include <cstring>

Encode_t g_orig_encode = nullptr;
extern "C" volatile float g_gain     = 1.0f;
extern "C" volatile LONG  g_inflight = 0;
bool g_hookActive = false;

namespace {

// WebRtcOpus_Encode_wrapper в discord_voice.node, первые 16 байт пролога
constexpr uintptr_t kEncodeRva = 0x713280;
const uint8_t kPrologue[16] = {
    0x56, 0x57, 0x48, 0x83, 0xEC, 0x28, 0xBF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x49, 0x81, 0xF8, 0x80, 0x16
};

int16_t ClampI16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

bool BytesEq(uintptr_t a, const uint8_t* p, size_t n) {
    __try { return memcmp((void*)a, p, n) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// если по rva не сошлось (другая версия дискорда) — ищем по сигнатуре
uintptr_t Scan(HMODULE m, const uint8_t* p, size_t n) {
    __try {
        auto* dos = (IMAGE_DOS_HEADER*)m;
        auto* nt  = (IMAGE_NT_HEADERS64*)((uint8_t*)m + dos->e_lfanew);
        uint8_t* base = (uint8_t*)m;
        size_t size = nt->OptionalHeader.SizeOfImage;
        uintptr_t hit = 0;
        for (size_t i = 0; i + n <= size; ++i) {
            if (base[i] == p[0] && memcmp(base + i, p, n) == 0) {
                if (hit) return 0;   // не уникально — не рискуем
                hit = (uintptr_t)(base + i);
            }
        }
        return hit;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

uintptr_t Resolve(HMODULE m) {
    uintptr_t a = (uintptr_t)m + kEncodeRva;
    if (BytesEq(a, kPrologue, sizeof kPrologue)) return a;
    return Scan(m, kPrologue, sizeof kPrologue);
}

}  // namespace

// перед энкодом множим pcm на гейн, жёстко клампим в int16
int __fastcall Hooked_Encode(void* inst, int16_t* pcm, uint64_t spc,
                             int maxb, uint8_t* out) {
    InterlockedIncrement(&g_inflight);

    uint64_t ch = 2;
    if (inst) {
        __try {
            uint64_t c = *(uint64_t*)((uint8_t*)inst + 0x10);
            if (c >= 1 && c <= 8) ch = c;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    const float g = g_gain;
    if (pcm && g != 1.0f) {
        const uint64_t n = spc * ch;
        for (uint64_t i = 0; i < n; ++i) {
            pcm[i] = ClampI16((int32_t)(pcm[i] * g));
        }
    }

    int r = 0;
    __try { r = g_orig_encode(inst, pcm, spc, maxb, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { r = -1; }

    InterlockedDecrement(&g_inflight);
    return r;
}

bool InstallHook() {
    HMODULE voice = nullptr;
    for (int i = 0; i < 120 && !voice; ++i) {
        voice = GetModuleHandleW(L"discord_voice.node");
        if (!voice) Sleep(500);
    }
    if (!voice) return false;

    uintptr_t addr = Resolve(voice);
    if (!addr) return false;

    if (MH_Initialize() != MH_OK) return false;
    if (MH_CreateHook((LPVOID)addr, (LPVOID)&Hooked_Encode,
                      (LPVOID*)&g_orig_encode) != MH_OK) {
        MH_Uninitialize();
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_Uninitialize();
        return false;
    }
    g_hookActive = true;
    return true;
}

void UninstallHook() {
    if (!g_hookActive) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();   // возвращает оригинальные байты функции
    g_hookActive = false;
}
