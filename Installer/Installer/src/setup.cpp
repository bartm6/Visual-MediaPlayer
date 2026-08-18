#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include "../res/resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
constexpr wchar_t kProductName[] = L"Visual MediaPlayer";
constexpr wchar_t kVersion[] = L"12.5.5";
constexpr wchar_t kAppExe[] = L"VisualMediaPlayer.exe";
constexpr wchar_t kUninstallExe[] = L"Uninstall.exe";
constexpr wchar_t kMainWindowClass[] = L"VisualMediaPlayerMain";
constexpr wchar_t kUninstallKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\VisualMediaPlayer";
constexpr wchar_t kAppPathsKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\VisualMediaPlayer.exe";
constexpr wchar_t kMachineAppKey[] = L"SOFTWARE\\Classes\\Applications\\VisualMediaPlayer.exe";
constexpr wchar_t kUserAppKey[] = L"Software\\Classes\\Applications\\VisualMediaPlayer.exe";

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\') return a + b;
    return a + L"\\" + b;
}

std::wstring ModulePath() {
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (!n || n >= buf.size()) return {};
    return std::wstring(buf.data(), n);
}

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) || !p) return {};
    std::wstring result(p);
    CoTaskMemFree(p);
    return result;
}

bool SetRegString(HKEY root, const std::wstring& key, const wchar_t* valueName, const std::wstring& value) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(h, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}

bool SetRegDword(HKEY root, const std::wstring& key, const wchar_t* valueName, DWORD value) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS) return false;
    const LONG rc = RegSetValueExW(h, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(h);
    return rc == ERROR_SUCCESS;
}

bool SetRegEmptyString(HKEY root, const std::wstring& key, const wchar_t* valueName) {
    return SetRegString(root, key, valueName, L"");
}

void DeleteRegTree(HKEY root, const std::wstring& key) {
    RegDeleteTreeW(root, key.c_str());
}

bool WriteResourceToFile(int resourceId, const std::wstring& path) {
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return false;
    HGLOBAL loaded = LoadResource(nullptr, resource);
    if (!loaded) return false;
    const DWORD size = SizeofResource(nullptr, resource);
    const void* data = LockResource(loaded);
    if (!data || !size) return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(file, data, size, &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    return ok && written == size;
}

bool CreateShortcut(const std::wstring& shortcutPath, const std::wstring& targetPath, const std::wstring& workingDir) {
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (FAILED(hr) || !link) return false;
    link->SetPath(targetPath.c_str());
    link->SetWorkingDirectory(workingDir.c_str());
    link->SetDescription(kProductName);
    link->SetIconLocation(targetPath.c_str(), 0);

    IPersistFile* persist = nullptr;
    hr = link->QueryInterface(IID_PPV_ARGS(&persist));
    bool ok = false;
    if (SUCCEEDED(hr) && persist) {
        ok = SUCCEEDED(persist->Save(shortcutPath.c_str(), TRUE));
        persist->Release();
    }
    link->Release();
    return ok;
}

bool AppIsRunning() {
    return FindWindowW(kMainWindowClass, nullptr) != nullptr;
}

bool IsUninstallCommand() {
    // The installed copy is named Uninstall.exe. Treat launching it directly as an
    // uninstall request too; otherwise it would accidentally enter the installer path.
    std::wstring selfName = ModulePath();
    const size_t slash = selfName.find_last_of(L"\\/");
    if (slash != std::wstring::npos) selfName = selfName.substr(slash + 1);
    std::transform(selfName.begin(), selfName.end(), selfName.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    if (selfName == L"uninstall.exe") return true;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool result = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg(argv[i]);
            std::transform(arg.begin(), arg.end(), arg.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            if (arg == L"/uninstall" || arg == L"-uninstall") result = true;
        }
        LocalFree(argv);
    }
    return result;
}

void RegisterOpenWith(const std::wstring& appPath) {
    const std::wstring command = L"\"" + appPath + L"\" \"%1\"";
    SetRegString(HKEY_LOCAL_MACHINE, kMachineAppKey, L"FriendlyAppName", kProductName);
    SetRegString(HKEY_LOCAL_MACHINE, std::wstring(kMachineAppKey) + L"\\shell\\open\\command", nullptr, command);

    static const wchar_t* supported[] = {
        L".mp4", L".mkv", L".mov", L".m4v", L".avi", L".webm", L".wmv", L".mts", L".m2ts",
        L".jpg", L".jpeg", L".png", L".bmp", L".gif", L".tif", L".tiff", L".webp", L".heic", L".heif", L".avif"
    };
    const std::wstring typesKey = std::wstring(kMachineAppKey) + L"\\SupportedTypes";
    for (const wchar_t* ext : supported) SetRegEmptyString(HKEY_LOCAL_MACHINE, typesKey, ext);
}

void RegisterInstalledApp(const std::wstring& installDir, const std::wstring& appPath, const std::wstring& uninstallPath) {
    SetRegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayName", kProductName);
    SetRegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayVersion", kVersion);
    SetRegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"DisplayIcon", appPath);
    SetRegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"InstallLocation", installDir);
    SetRegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"UninstallString", L"\"" + uninstallPath + L"\" /uninstall");
    SetRegString(HKEY_LOCAL_MACHINE, kUninstallKey, L"Publisher", L"Visual MediaPlayer");
    SetRegDword(HKEY_LOCAL_MACHINE, kUninstallKey, L"NoModify", 1);
    SetRegDword(HKEY_LOCAL_MACHINE, kUninstallKey, L"NoRepair", 1);

    SetRegString(HKEY_LOCAL_MACHINE, kAppPathsKey, nullptr, appPath);
    SetRegString(HKEY_LOCAL_MACHINE, kAppPathsKey, L"Path", installDir);
    RegisterOpenWith(appPath);
}

bool Install() {
    if (AppIsRunning()) {
        MessageBoxW(nullptr, L"Close Visual MediaPlayer before installing or updating it.", kProductName, MB_OK | MB_ICONWARNING);
        return false;
    }

    const int answer = MessageBoxW(nullptr,
        L"Install Visual MediaPlayer on this PC?\n\n"
        L"It will be installed in Program Files and added to the Start Menu and Windows Installed Apps.",
        L"Visual MediaPlayer Setup", MB_OKCANCEL | MB_ICONINFORMATION);
    if (answer != IDOK) return false;

    const std::wstring programFiles = KnownFolder(FOLDERID_ProgramFiles);
    const std::wstring commonPrograms = KnownFolder(FOLDERID_CommonPrograms);
    if (programFiles.empty() || commonPrograms.empty()) {
        MessageBoxW(nullptr, L"Windows installation folders could not be located.", L"Setup error", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring installDir = JoinPath(programFiles, kProductName);
    const std::wstring appPath = JoinPath(installDir, kAppExe);
    const std::wstring uninstallPath = JoinPath(installDir, kUninstallExe);
    const std::wstring startShortcut = JoinPath(commonPrograms, L"Visual MediaPlayer.lnk");

    const int dirResult = SHCreateDirectoryExW(nullptr, installDir.c_str(), nullptr);
    if (dirResult != ERROR_SUCCESS && dirResult != ERROR_ALREADY_EXISTS && dirResult != ERROR_FILE_EXISTS) {
        if (GetFileAttributesW(installDir.c_str()) == INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(nullptr, L"Could not create the installation folder.", L"Setup error", MB_OK | MB_ICONERROR);
            return false;
        }
    }

    if (!WriteResourceToFile(IDR_APP_PAYLOAD, appPath)) {
        MessageBoxW(nullptr, L"Could not install VisualMediaPlayer.exe.", L"Setup error", MB_OK | MB_ICONERROR);
        return false;
    }

    const std::wstring self = ModulePath();
    if (self.empty() || !CopyFileW(self.c_str(), uninstallPath.c_str(), FALSE)) {
        DeleteFileW(appPath.c_str());
        MessageBoxW(nullptr, L"Could not create the uninstaller.", L"Setup error", MB_OK | MB_ICONERROR);
        return false;
    }

    CreateShortcut(startShortcut, appPath, installDir);
    RegisterInstalledApp(installDir, appPath, uninstallPath);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    const int launch = MessageBoxW(nullptr,
        L"Visual MediaPlayer was installed successfully.\n\nOpen it now?",
        L"Installation complete", MB_YESNO | MB_ICONINFORMATION);
    if (launch == IDYES) ShellExecuteW(nullptr, L"open", appPath.c_str(), nullptr, installDir.c_str(), SW_SHOWNORMAL);
    return true;
}

void RemoveUserSettings() {
    const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (local.empty()) return;
    const std::wstring settingsDir = JoinPath(local, L"VisualMediaPlayer");
    DeleteFileW(JoinPath(settingsDir, L"settings.ini").c_str());
    RemoveDirectoryW(settingsDir.c_str());
}

bool Uninstall() {
    if (AppIsRunning()) {
        MessageBoxW(nullptr, L"Close Visual MediaPlayer before uninstalling it.", kProductName, MB_OK | MB_ICONWARNING);
        return false;
    }

    const int answer = MessageBoxW(nullptr,
        L"Remove Visual MediaPlayer from this PC?\n\n"
        L"Your original media files and .visualmediaplayer-cache folders will not be deleted.",
        L"Uninstall Visual MediaPlayer", MB_YESNO | MB_ICONWARNING);
    if (answer != IDYES) return false;

    const std::wstring programFiles = KnownFolder(FOLDERID_ProgramFiles);
    const std::wstring commonPrograms = KnownFolder(FOLDERID_CommonPrograms);
    const std::wstring installDir = JoinPath(programFiles, kProductName);
    const std::wstring appPath = JoinPath(installDir, kAppExe);
    const std::wstring startShortcut = JoinPath(commonPrograms, L"Visual MediaPlayer.lnk");

    DeleteFileW(startShortcut.c_str());
    DeleteRegTree(HKEY_LOCAL_MACHINE, kUninstallKey);
    DeleteRegTree(HKEY_LOCAL_MACHINE, kAppPathsKey);
    DeleteRegTree(HKEY_LOCAL_MACHINE, kMachineAppKey);
    DeleteRegTree(HKEY_CURRENT_USER, kUserAppKey);
    RemoveUserSettings();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    DeleteFileW(appPath.c_str());

    // The running uninstaller cannot reliably remove its own executable immediately.
    // Schedule the final two deletions for Windows startup; the app itself is already gone.
    const std::wstring self = ModulePath();
    if (!self.empty()) MoveFileExW(self.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(installDir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    MessageBoxW(nullptr, L"Visual MediaPlayer has been uninstalled.", kProductName, MB_OK | MB_ICONINFORMATION);
    return true;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninstall = IsUninstallCommand();
    const bool ok = uninstall ? Uninstall() : Install();
    if (SUCCEEDED(coHr)) CoUninitialize();
    return ok ? 0 : 1;
}
