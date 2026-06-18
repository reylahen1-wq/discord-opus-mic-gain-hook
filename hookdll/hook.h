#pragma once

#include <windows.h>
#include <cstdint>

using Encode_t = int (__fastcall*)(void*, int16_t*, uint64_t, int, uint8_t*);

extern Encode_t g_orig_encode;

// крутилка мика (1.0 = без изменений), пишет gui, читает аудио-поток
extern "C" volatile float g_gain;
// сколько потоков сейчас внутри хука, нужно для чистой выгрузки
extern "C" volatile LONG  g_inflight;
extern bool g_hookActive;

bool InstallHook();
void UninstallHook();
void RunGui();
