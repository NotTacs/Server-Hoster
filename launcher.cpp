#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

static const fs::path kBuildPath   = LR"(D:\Fortnite Builds\19.10-CL-18675304\Fortnite 19.10)";
static const fs::path kCobaltDll   = LR"(C:\Users\Admin\Documents\GitHub\Cobalt\x64\Release\Cobalt.dll)";
static const fs::path kArtemisDll  = LR"(C:\Users\Admin\Documents\GitHub\Artemis\x64\Release\Artemis.dll)";
static const DWORD    kInjectDelay = 300;
static const DWORD    CInjectDelay = 100;

static void Fail(const std::string& msg) {
    std::cerr << "[!] " << msg << " (GetLastError=" << GetLastError() << ")\n";
    std::cin.get();
    ExitProcess(1);
}

static bool Spawn(const fs::path& exe, const fs::path& cwd, std::wstring args, DWORD flags, PROCESS_INFORMATION& pi) {
    STARTUPINFOW si{ sizeof(si) };
    std::wstring cmd = L"\"" + exe.wstring() + L"\" " + args;
    return CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags,
                          nullptr, cwd.c_str(), &si, &pi);
}

static bool Inject(HANDLE process, const fs::path& dll) {
    const std::wstring path = dll.wstring();
    const SIZE_T size = (path.size() + 1) * sizeof(wchar_t);

    LPVOID remote = VirtualAllocEx(process, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    if (!WriteProcessMemory(process, remote, path.c_str(), size, nullptr)) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    auto loadLib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!thread) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(thread, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeThread(thread, &exitCode);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return exitCode != 0;
}

static void KillByName(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{ sizeof(pe) };
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, name) != 0) continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
        if (!h) continue;
        TerminateProcess(h, 0);
        WaitForSingleObject(h, 5000);
        CloseHandle(h);
        std::wcout << L"[-] Killed " << name << L" (PID " << pe.th32ProcessID << L")\n";
    }
    CloseHandle(snap);
}

static void LaunchOnce(const std::wstring& accessToken) {
    const fs::path bin = kBuildPath / L"FortniteGame/Binaries/Win64";
    if (!fs::exists(bin)) Fail("Binaries path not found: " + bin.string());

    PROCESS_INFORMATION launcherPi{}, bePi{};
    const fs::path launcherExe = bin / L"FortniteLauncher.exe";
    const fs::path beExe       = bin / L"FortniteClient-Win64-Shipping_BE.exe";
    if (fs::exists(launcherExe)) Spawn(launcherExe, bin, L"", CREATE_SUSPENDED, launcherPi);
    if (fs::exists(beExe))       Spawn(beExe,       bin, L"", CREATE_SUSPENDED, bePi);

    const fs::path shippingExe = bin / L"FortniteClient-Win64-Shipping.exe";
    if (!fs::exists(shippingExe)) Fail("FortniteClient-Win64-Shipping.exe not found.");

    const std::wstring args =
        L"-epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -noeac "
        L"-nullrhi -nosplash -server "
        L"-fromfl=be -fltoken=h1cdhchd10150221h130eB56 -skippatchcheck "
        L"-AUTH_TYPE=epic -AUTH_LOGIN=jamal@artemis.dev -AUTH_PASSWORD=Artemis "
        L"-AllowAllPlaylistsInShipping ";

    PROCESS_INFORMATION shipPi{};
    if (!Spawn(shippingExe, bin, args, 0, shipPi)) Fail("Failed to launch Shipping");
    std::cout << "[+] Shipping launched (PID " << shipPi.dwProcessId << ")\n";
    Sleep(CInjectDelay);
        if (Inject(shipPi.hProcess, kCobaltDll)) std::cout << "[+] Cobalt injected\n";
    else                                      std::cerr << "[!] Cobalt injection failed (GetLastError=" << GetLastError() << ")\n";
    Sleep(kInjectDelay);
    if (Inject(shipPi.hProcess, kArtemisDll)) std::cout << "[+] Artemis injected\n";
    else                                      std::cerr << "[!] Artemis injection failed (GetLastError=" << GetLastError() << ")\n";

    std::cout << "[+] Launched successfully. Waiting for game to close...\n";
    WaitForSingleObject(shipPi.hProcess, INFINITE);

    for (auto* pi : { &launcherPi, &bePi }) {
        if (pi->hProcess) {
            TerminateProcess(pi->hProcess, 0);
            CloseHandle(pi->hThread);
            CloseHandle(pi->hProcess);
        }
    }
    CloseHandle(shipPi.hThread);
    CloseHandle(shipPi.hProcess);
}

int wmain(int argc, wchar_t** argv) {
    const std::wstring accessToken = argc > 1 ? argv[1] : L"unused";

    for (int run = 1;; ++run) {
        std::cout << "\n===== Run " << run << " =====\n";
        KillByName(L"FortniteClient-Win64-Shipping.exe");
        KillByName(L"FortniteClient-Win64-Shipping_BE.exe");
        KillByName(L"FortniteLauncher.exe");
        LaunchOnce(accessToken);
        std::cout << "[+] Game closed, restarting in 5 seconds...\n";
        Sleep(5000);
    }
}
