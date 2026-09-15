#include "Log.h"

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
FILE* g_file = nullptr;
std::mutex g_mutex;
}

void OpenLog(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = nullptr;
    fopen_s(&g_file, path.c_str(), "w");
}

void Log(const char* format, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file) return;
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::fprintf(g_file, "[%02u:%02u:%02u] ", time.wHour, time.wMinute, time.wSecond);
    va_list args;
    va_start(args, format);
    std::vfprintf(g_file, format, args);
    va_end(args);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

