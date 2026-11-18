#include "FileDialogs.h"

#include <commdlg.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <system_error>
#include <vector>

namespace {

std::wstring PickFile(HWND owner, const wchar_t* filter, const wchar_t* title) {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer.data()) : std::wstring();
}

}  // namespace

std::vector<std::wstring> PickVideoFiles(HWND owner) {
    Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }
    std::wstring videos;
    for (const wchar_t* extension : kVideoExtensions) {
        videos += (videos.empty() ? L"*" : L";*") + std::wstring(extension);
    }
    const COMDLG_FILTERSPEC types[] = {{L"동영상 파일", videos.c_str()}, {L"모든 파일", L"*.*"}};
    dialog->SetFileTypes(static_cast<UINT>(std::size(types)), types);
    dialog->SetTitle(L"재생할 동영상 선택 (여러 개 선택하면 재생 목록이 됩니다)");
    FILEOPENDIALOGOPTIONS options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);

    Microsoft::WRL::ComPtr<IShellItemArray> items;
    DWORD count = 0;
    if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResults(&items)) || FAILED(items->GetCount(&count))) {
        return {};
    }
    std::vector<std::wstring> paths;
    for (DWORD i = 0; i < count; ++i) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (SUCCEEDED(items->GetItemAt(i, &item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            paths.emplace_back(path);
            CoTaskMemFree(path);
        }
    }
    return paths;
}

std::wstring PickSubtitleFile(HWND owner) {
    return PickFile(owner,
                    L"자막 파일\0*.smi;*.sami;*.srt;*.ass;*.ssa;*.vtt;*.sub\0"
                    L"모든 파일\0*.*\0",
                    L"자막 파일 선택");
}

bool IsSubtitleFile(const std::wstring& path) {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring extension = path.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return std::find(kSubtitleExtensions.begin(), kSubtitleExtensions.end(), extension) != kSubtitleExtensions.end();
}

std::wstring FindVideoForSubtitle(const std::wstring& subtitle) {
    const std::filesystem::path subtitlePath(subtitle);
    const std::wstring subtitleName = subtitlePath.stem().wstring();

    std::wstring best;
    size_t bestLength = 0;
    size_t bestRank = kVideoExtensions.size();
    std::error_code error;
    for (std::filesystem::directory_iterator it(subtitlePath.parent_path(), error), end; !error && it != end; it.increment(error)) {
        const std::wstring extension = it->path().extension().wstring();
        const auto type = std::find_if(kVideoExtensions.begin(), kVideoExtensions.end(),
                                       [&](const wchar_t* known) { return _wcsicmp(known, extension.c_str()) == 0; });
        if (type == kVideoExtensions.end()) {
            continue;
        }
        const std::wstring videoName = it->path().stem().wstring();
        const bool exact = _wcsicmp(videoName.c_str(), subtitleName.c_str()) == 0;
        const bool tagged = subtitleName.size() > videoName.size() && subtitleName[videoName.size()] == L'.' &&
                            _wcsnicmp(videoName.c_str(), subtitleName.c_str(), videoName.size()) == 0;
        if (!exact && !tagged) {
            continue;
        }
        // The longest name is the closest match. Between formats, the list above decides.
        const size_t rank = static_cast<size_t>(type - kVideoExtensions.begin());
        if (videoName.size() > bestLength || (videoName.size() == bestLength && rank < bestRank)) {
            best = it->path().wstring();
            bestLength = videoName.size();
            bestRank = rank;
        }
    }
    return best;
}
