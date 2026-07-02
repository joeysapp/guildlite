// guildlite-inject.exe -- minimal LoadLibrary injector for Guild Wars (Gw.exe, 32-bit).
//
//   guildlite-inject [path\to\guildlite.dll] [ProcessName.exe]
//
// Defaults: guildlite.dll next to this exe; process Gw.exe. GW has no anti-cheat, so the
// classic VirtualAllocEx + WriteProcessMemory + CreateRemoteThread(LoadLibraryW) is the
// right technique -- no manual mapping needed. Must be run in the SAME interactive session
// as Gw.exe (a session-0 SSH shell can't CreateRemoteThread into the desktop session).
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "Log.h"

static std::vector<DWORD> FindPids(const wchar_t* name)
{
    std::vector<DWORD> pids;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

static bool Inject(DWORD pid, const std::wstring& dllPath)
{
    const HANDLE proc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!proc) { fwprintf(stderr, L"OpenProcess(%lu) failed: %lu\n", pid, GetLastError()); return false; }

    bool ok = false;
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote && WriteProcessMemory(proc, remote, dllPath.c_str(), bytes, nullptr)) {
        const auto loadlib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        const HANDLE thread = CreateRemoteThread(proc, nullptr, 0, loadlib, remote, 0, nullptr);
        if (thread) {
            WaitForSingleObject(thread, INFINITE);
            DWORD remoteModule = 0;               // LoadLibraryW's return (HMODULE, 32-bit here)
            GetExitCodeThread(thread, &remoteModule);
            CloseHandle(thread);
            if (remoteModule != 0) {
                wprintf(L"injected into pid %lu (module 0x%08lX)\n", pid, remoteModule);
                GL_INJLOG("LoadLibraryW OK in pid %lu -> module 0x%08lX", pid, remoteModule);
                ok = true;
            } else {
                fwprintf(stderr, L"LoadLibraryW returned 0 in the target -- DLL failed to load "
                                 L"(missing dep? wrong bitness? AV block?)\n");
                GL_INJLOG("LoadLibraryW returned 0 in pid %lu -- DLL failed to load", pid);
            }
        } else {
            fwprintf(stderr, L"CreateRemoteThread failed: %lu\n", GetLastError());
        }
    } else {
        fwprintf(stderr, L"VirtualAllocEx/WriteProcessMemory failed: %lu\n", GetLastError());
    }
    if (remote) VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
    CloseHandle(proc);
    return ok;
}

int wmain(int argc, wchar_t** argv)
{
    const std::filesystem::path self = argv[0];
    std::wstring dll = (argc > 1) ? argv[1] : (self.parent_path() / L"guildlite.dll").wstring();
    const wchar_t* procName = (argc > 2) ? argv[2] : L"Gw.exe";

    std::error_code ec;
    const std::filesystem::path dllPath = std::filesystem::absolute(dll, ec);
    if (!std::filesystem::exists(dllPath)) { fwprintf(stderr, L"dll not found: %ls\n", dllPath.c_str()); return 2; }

    const auto pids = FindPids(procName);
    GL_INJLOG("start: dll=%ls exists=%d, %zu %ls process(es) found",
              dllPath.c_str(), static_cast<int>(std::filesystem::exists(dllPath)), pids.size(), procName);
    if (pids.empty()) { fwprintf(stderr, L"no %ls process found -- start Guild Wars first\n", procName); GL_INJLOG("abort: no target process"); return 3; }
    if (pids.size() > 1)
        wprintf(L"note: %zu %ls processes; injecting the first (pid %lu). Pass a pid-specific build later.\n",
                pids.size(), procName, pids[0]);

    wprintf(L"injecting %ls -> %ls (pid %lu)\n", dllPath.c_str(), procName, pids[0]);
    const bool ok = Inject(pids[0], dllPath.wstring());
    GL_INJLOG("done: pid=%lu ok=%d", pids[0], static_cast<int>(ok));
    return ok ? 0 : 1;
}
