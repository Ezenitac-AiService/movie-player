#include "FileAssociation.h"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <utility>

#include "FileDialogs.h"

namespace {

constexpr wchar_t kProgId[] = L"MoviePlayer.Video";
constexpr wchar_t kProgIdKey[] = L"Software\\Classes\\MoviePlayer.Video";
constexpr wchar_t kSubtitleProgId[] = L"MoviePlayer.Subtitle";
constexpr wchar_t kSubtitleProgIdKey[] = L"Software\\Classes\\MoviePlayer.Subtitle";
constexpr wchar_t kApplicationKey[] = L"Software\\Classes\\Applications\\MoviePlayer.exe";
constexpr wchar_t kAppKey[] = L"Software\\MoviePlayer";
constexpr wchar_t kCapabilitiesKey[] = L"Software\\MoviePlayer\\Capabilities";
constexpr wchar_t kRegisteredApplicationsKey[] = L"Software\\RegisteredApplications";
constexpr wchar_t kRegisteredName[] = L"MoviePlayer";
constexpr wchar_t kAppName[] = L"Movie Player";

std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

std::wstring OpenCommand() {
    return L"\"" + ExecutablePath() + L"\" \"%1\"";
}

bool SetText(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetKeyValueW(HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(), bytes) == ERROR_SUCCESS;
}

std::wstring GetText(const std::wstring& key, const wchar_t* name) {
    wchar_t buffer[33000];
    DWORD bytes = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &bytes) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

bool KeyIsEmpty(const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
        return false;
    }
    DWORD subkeys = 1;
    DWORD values = 1;
    RegQueryInfoKeyW(handle, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(handle);
    return subkeys == 0 && values == 0;
}

void NotifyShell() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

}  // namespace

namespace {

// Version 0.3.0 set this to "Player", so that Explorer would offer "Open" for more than
// 15 files at once. As each file opens a window of its own, that only gives a screen full
// of windows all playing at once, so the value is removed. Explorer then keeps the limit.
constexpr wchar_t kMultiSelectValue[] = L"MultiSelectModel";

bool HasMultiSelectModel(const std::wstring& key) {
    return RegGetValueW(HKEY_CURRENT_USER, (key + L"\\shell\\open").c_str(), kMultiSelectValue, RRF_RT_ANY, nullptr, nullptr, nullptr) ==
           ERROR_SUCCESS;
}

}  // namespace

// True only if subtitles are covered too, and nothing from an older version is left, so
// such a registration can be redone.
bool IsFileAssociationRegistered() {
    for (const std::wstring key : {std::wstring(kProgIdKey), std::wstring(kSubtitleProgIdKey)}) {
        if (GetText(key + L"\\shell\\open\\command", nullptr) != OpenCommand() || HasMultiSelectModel(key)) {
            return false;
        }
    }
    return !HasMultiSelectModel(kApplicationKey);
}

bool HasFileAssociationEntries() {
    for (const wchar_t* key : {kProgIdKey, kSubtitleProgIdKey, kApplicationKey, kAppKey}) {
        HKEY handle = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &handle) == ERROR_SUCCESS) {
            RegCloseKey(handle);
            return true;
        }
    }
    if (RegGetValueW(HKEY_CURRENT_USER, kRegisteredApplicationsKey, kRegisteredName, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        return true;
    }
    const auto listedFor = [](const auto& extensions, const wchar_t* id) {
        for (const wchar_t* extension : extensions) {
            const std::wstring key = std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithProgids";
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), id, RRF_RT_ANY, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                return true;
            }
        }
        return false;
    };
    return listedFor(kVideoExtensions, kProgId) || listedFor(kSubtitleExtensions, kSubtitleProgId);
}

bool RegisterFileAssociation() {
    const std::wstring executable = ExecutablePath();
    const std::wstring command = OpenCommand();
    const std::wstring icon = L"\"" + executable + L"\",0";
    const std::wstring progId(kProgIdKey);
    const std::wstring application(kApplicationKey);
    const std::wstring capabilities(kCapabilitiesKey);

    bool ok = true;
    for (const auto& [key, name] : {std::pair{progId, L"Movie Player 동영상"}, std::pair{std::wstring(kSubtitleProgIdKey), L"Movie Player 자막"}}) {
        ok = SetText(key, nullptr, name) && ok;
        ok = SetText(key + L"\\DefaultIcon", nullptr, icon) && ok;
        RegDeleteKeyValueW(HKEY_CURRENT_USER, (key + L"\\shell\\open").c_str(), kMultiSelectValue);
        ok = SetText(key + L"\\shell\\open\\command", nullptr, command) && ok;
    }

    ok = SetText(application, L"FriendlyAppName", kAppName) && ok;
    ok = SetText(application + L"\\DefaultIcon", nullptr, icon) && ok;
    RegDeleteKeyValueW(HKEY_CURRENT_USER, (application + L"\\shell\\open").c_str(), kMultiSelectValue);
    ok = SetText(application + L"\\shell\\open\\command", nullptr, command) && ok;

    ok = SetText(capabilities, L"ApplicationName", kAppName) && ok;
    ok = SetText(capabilities, L"ApplicationDescription", L"mpv 기반 동영상 플레이어") && ok;
    ok = SetText(capabilities, L"ApplicationIcon", icon) && ok;

    const auto addTypes = [&](const auto& extensions, const wchar_t* id) {
        for (const wchar_t* extension : extensions) {
            const std::wstring classesKey = std::wstring(L"Software\\Classes\\") + extension + L"\\OpenWithProgids";
            ok = RegSetKeyValueW(HKEY_CURRENT_USER, classesKey.c_str(), id, REG_NONE, nullptr, 0) == ERROR_SUCCESS && ok;
            ok = SetText(capabilities + L"\\FileAssociations", extension, id) && ok;
            ok = SetText(application + L"\\SupportedTypes", extension, L"") && ok;
        }
    };
    addTypes(kVideoExtensions, kProgId);
    addTypes(kSubtitleExtensions, kSubtitleProgId);

    ok = SetText(kRegisteredApplicationsKey, kRegisteredName, capabilities) && ok;
    NotifyShell();
    return ok;
}

void UnregisterFileAssociation() {
    RegDeleteTreeW(HKEY_CURRENT_USER, kProgIdKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kSubtitleProgIdKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kApplicationKey);
    RegDeleteTreeW(HKEY_CURRENT_USER, kAppKey);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kRegisteredApplicationsKey, kRegisteredName);

    const auto removeTypes = [](const auto& extensions, const wchar_t* id) {
        for (const wchar_t* extension : extensions) {
            const std::wstring extensionKey = std::wstring(L"Software\\Classes\\") + extension;
            const std::wstring openWith = extensionKey + L"\\OpenWithProgids";
            RegDeleteKeyValueW(HKEY_CURRENT_USER, openWith.c_str(), id);
            // Remove only what registering would have created, and only if nothing else uses it.
            if (KeyIsEmpty(openWith)) {
                RegDeleteKeyW(HKEY_CURRENT_USER, openWith.c_str());
                if (KeyIsEmpty(extensionKey)) {
                    RegDeleteKeyW(HKEY_CURRENT_USER, extensionKey.c_str());
                }
            }
        }
    };
    removeTypes(kVideoExtensions, kProgId);
    removeTypes(kSubtitleExtensions, kSubtitleProgId);
    NotifyShell();
}

void OpenDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps?registeredAppUser=MoviePlayer", nullptr, nullptr, SW_SHOWNORMAL);
}
