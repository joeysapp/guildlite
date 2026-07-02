#pragma once
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>

// Minimal append-only file logger shared by the loader and the payload. Uses only kernel32/CRT
// (safe to call from DllMain) and does NOT create directories -- the deploy dir already exists.
// Lets us debug injection over SSH instead of reading the game's console.
namespace gl {
    inline std::filesystem::path LogDir()
    {
        wchar_t home[MAX_PATH]{};
        GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        return std::filesystem::path(home) / L"Documents" / L"guildlite";
    }

    inline void LogTo(const wchar_t* file, const char* fmt, ...)
    {
        char msg[1024];
        va_list ap;
        va_start(ap, fmt);
        _vsnprintf_s(msg, sizeof(msg), _TRUNCATE, fmt, ap);
        va_end(ap);
        SYSTEMTIME st;
        GetLocalTime(&st);
        char line[1200];
        const int n = _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d.%03d] %s\r\n",
                                  st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);
        const HANDLE h = CreateFileW((LogDir() / file).c_str(), FILE_APPEND_DATA,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(h, line, n > 0 ? static_cast<DWORD>(n) : 0, &written, nullptr);
            CloseHandle(h);
        }
    }
}

#define GL_DLLLOG(...) gl::LogTo(L"guildlite.log", __VA_ARGS__)
#define GL_INJLOG(...) gl::LogTo(L"inject.log", __VA_ARGS__)
