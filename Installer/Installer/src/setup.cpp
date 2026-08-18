#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <system_error>
#include "../res/resource.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace {
namespace fs = std::filesystem;
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

std::wstring SettingsPathNoCreate() {
    const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (local.empty()) return {};
    return JoinPath(JoinPath(local, L"VisualMediaPlayer"), L"settings.ini");
}

std::wstring NormalizePathKey(const std::wstring& raw) {
    if (raw.empty()) return {};
    std::wstring value = fs::path(raw).lexically_normal().wstring();
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return value;
}

void AddUniqueRoot(std::vector<std::wstring>& roots, const std::wstring& raw) {
    if (raw.empty()) return;
    const std::wstring key = NormalizePathKey(raw);
    if (key.empty()) return;
    for (const auto& existing : roots) {
        if (NormalizePathKey(existing) == key) return;
    }
    roots.push_back(fs::path(raw).lexically_normal().wstring());
}

std::vector<std::wstring> ReadKnownLibraryRoots() {
    std::vector<std::wstring> roots;
    const std::wstring settings = SettingsPathNoCreate();
    if (settings.empty() || GetFileAttributesW(settings.c_str()) == INVALID_FILE_ATTRIBUTES) return roots;

    wchar_t folder[32768]{};
    GetPrivateProfileStringW(L"Library", L"Folder", L"", folder, static_cast<DWORD>(_countof(folder)), settings.c_str());
    AddUniqueRoot(roots, folder);

    std::vector<wchar_t> section(65536, L'\0');
    const DWORD chars = GetPrivateProfileSectionW(L"CacheRoots", section.data(), static_cast<DWORD>(section.size()), settings.c_str());
    if (chars > 0 && static_cast<size_t>(chars) < section.size() - 2) {
        const wchar_t* p = section.data();
        while (*p) {
            std::wstring entry = p;
            const size_t eq = entry.find(L'=');
            if (eq != std::wstring::npos && eq + 1 < entry.size()) AddUniqueRoot(roots, entry.substr(eq + 1));
            p += entry.size() + 1;
        }
    }
    return roots;
}

bool IsDirectoryReparsePoint(const fs::path& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

bool TreeContainsReparsePoint(const fs::path& root) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) return true;
    for (; it != end; it.increment(ec)) {
        if (ec) return true;
        const DWORD attrs = GetFileAttributesW(it->path().c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return true;
        if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
    }
    return false;
}

struct CacheRemovalSummary {
    size_t removed = 0;
    size_t skipped = 0;
    size_t failed = 0;
};

CacheRemovalSummary RemoveVisualMediaPlayerCaches(const std::vector<std::wstring>& roots) {
    CacheRemovalSummary summary;
    for (const auto& rawRoot : roots) {
        const fs::path root = fs::path(rawRoot).lexically_normal();
        const DWORD rootAttrs = GetFileAttributesW(root.c_str());
        if (rootAttrs == INVALID_FILE_ATTRIBUTES || (rootAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (rootAttrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            ++summary.skipped;
            continue;
        }

        std::vector<fs::path> cacheDirs;
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        if (ec) {
            ++summary.skipped;
            continue;
        }
        bool scanFailed = false;
        for (; it != end; it.increment(ec)) {
            if (ec) { scanFailed = true; break; }
            const fs::path entry = it->path();
            const DWORD attrs = GetFileAttributesW(entry.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) { scanFailed = true; break; }
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
            if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                it.disable_recursion_pending();
                continue;
            }
            std::wstring name = entry.filename().wstring();
            std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
            if (name == L".visualmediaplayer-cache") {
                cacheDirs.push_back(entry.lexically_normal());
                it.disable_recursion_pending();
            }
        }
        if (scanFailed) {
            ++summary.skipped;
            continue;
        }

        for (const auto& cache : cacheDirs) {
            // Never recurse through junctions/symlinks that somebody may have placed
            // inside the cache folder. If anything is uncertain, leave the cache alone.
            if (IsDirectoryReparsePoint(cache) || TreeContainsReparsePoint(cache)) {
                ++summary.skipped;
                continue;
            }
            ec.clear();
            const uintmax_t removed = fs::remove_all(cache, ec);
            if (ec) ++summary.failed;
            else if (removed > 0) ++summary.removed;
        }
    }
    return summary;
}

bool ConfirmUninstall(bool& deleteCaches) {
    deleteCaches = false;
    TASKDIALOGCONFIG cfg{};
    cfg.cbSize = sizeof(cfg);
    cfg.hInstance = GetModuleHandleW(nullptr);
    cfg.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    cfg.dwCommonButtons = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
    cfg.pszWindowTitle = L"Uninstall Visual MediaPlayer";
    cfg.pszMainIcon = TD_WARNING_ICON;
    cfg.pszMainInstruction = L"Remove Visual MediaPlayer from this PC?";
    cfg.pszContent =
        L"The application, Start Menu shortcut, Open with registrations, and application settings will be removed.\n\n"
        L"Original media files are never deleted. If cache removal is selected, only folders named exactly "
        L".visualmediaplayer-cache inside known library roots are considered. Unavailable or uncertain locations are skipped.";
    cfg.pszVerificationText = L"Also delete Visual MediaPlayer .visualmediaplayer-cache folders";
    cfg.nDefaultButton = IDNO;

    int button = IDNO;
    BOOL checked = FALSE;
    const HRESULT hr = TaskDialogIndirect(&cfg, &button, nullptr, &checked);
    if (SUCCEEDED(hr)) {
        deleteCaches = checked != FALSE;
        return button == IDYES;
    }

    // Fail safe if TaskDialog is unavailable: uninstall can continue, but cache
    // deletion remains off because there is no checkbox to opt into it.
    const int answer = MessageBoxW(nullptr,
        L"Remove Visual MediaPlayer from this PC?\n\n"
        L"The application, shortcuts, registrations, and settings will be removed.\n"
        L"Media files and .visualmediaplayer-cache folders will be kept.",
        L"Uninstall Visual MediaPlayer", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    deleteCaches = false;
    return answer == IDYES;
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

    bool deleteCaches = false;
    if (!ConfirmUninstall(deleteCaches)) return false;

    // Read cache roots before settings are removed. Older versions at least stored
    // the active Library\Folder; newer versions also keep a history of library roots.
    const std::vector<std::wstring> cacheRoots = deleteCaches ? ReadKnownLibraryRoots() : std::vector<std::wstring>{};

    const std::wstring programFiles = KnownFolder(FOLDERID_ProgramFiles);
    const std::wstring commonPrograms = KnownFolder(FOLDERID_CommonPrograms);
    const std::wstring installDir = JoinPath(programFiles, kProductName);
    const std::wstring appPath = JoinPath(installDir, kAppExe);
    const std::wstring startShortcut = JoinPath(commonPrograms, L"Visual MediaPlayer.lnk");

    CacheRemovalSummary cacheSummary;
    if (deleteCaches) cacheSummary = RemoveVisualMediaPlayerCaches(cacheRoots);

    DeleteFileW(startShortcut.c_str());
    DeleteRegTree(HKEY_LOCAL_MACHINE, kUninstallKey);
    DeleteRegTree(HKEY_LOCAL_MACHINE, kAppPathsKey);
    DeleteRegTree(HKEY_LOCAL_MACHINE, kMachineAppKey);
    DeleteRegTree(HKEY_CURRENT_USER, kUserAppKey);
    RemoveUserSettings();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    DeleteFileW(appPath.c_str());

    // The running uninstaller cannot delete itself. Schedule only this executable
    // and then the now-empty install directory for deletion at the next reboot.
    // No unrelated files in Program Files are recursively removed.
    const std::wstring self = ModulePath();
    if (!self.empty()) MoveFileExW(self.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    MoveFileExW(installDir.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);

    std::wstring message = L"Visual MediaPlayer has been uninstalled.";
    if (deleteCaches) {
        message += L"\n\nCache cleanup:";
        message += L"\nRemoved: " + std::to_wstring(cacheSummary.removed);
        if (cacheSummary.skipped > 0) message += L"\nSkipped/unavailable: " + std::to_wstring(cacheSummary.skipped);
        if (cacheSummary.failed > 0) message += L"\nFailed: " + std::to_wstring(cacheSummary.failed);
        if (cacheRoots.empty()) message += L"\nNo saved library roots were available to scan.";
    }
    message += L"\n\nThe uninstaller file itself is removed by Windows after restart.";
    MessageBoxW(nullptr, message.c_str(), kProductName, MB_OK | MB_ICONINFORMATION);
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
