#include "Playlist.h"

#include <windows.h>

#include <shlwapi.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <system_error>

#include "FileDialogs.h"

namespace {

bool IsHexDigit(wchar_t c) {
    return std::iswxdigit(c) != 0;
}

// The part of a name that stays the same across a series: numbers become "#", and a
// release group's checksum such as "[27AB50FF]" is left out, since it differs per file.
std::wstring SeriesKey(const std::wstring& name) {
    std::wstring key;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == L'[' && i + 9 < name.size() && name[i + 9] == L']' &&
            std::all_of(name.begin() + static_cast<std::ptrdiff_t>(i) + 1, name.begin() + static_cast<std::ptrdiff_t>(i) + 9, IsHexDigit)) {
            i += 9;
            continue;
        }
        if (std::iswdigit(name[i])) {
            key += L'#';
            while (i + 1 < name.size() && std::iswdigit(name[i + 1])) {
                ++i;
            }
            continue;
        }
        key += static_cast<wchar_t>(std::towlower(name[i]));
    }
    return key;
}

bool IsVideo(const std::filesystem::path& path) {
    const std::wstring extension = path.extension().wstring();
    return std::any_of(kVideoExtensions.begin(), kVideoExtensions.end(),
                       [&](const wchar_t* known) { return _wcsicmp(known, extension.c_str()) == 0; });
}

}  // namespace

void SortByName(std::vector<std::wstring>& paths) {
    std::stable_sort(paths.begin(), paths.end(), [](const std::wstring& a, const std::wstring& b) {
        return StrCmpLogicalW(std::filesystem::path(a).filename().c_str(), std::filesystem::path(b).filename().c_str()) < 0;
    });
}

std::vector<std::wstring> CollectVideos(const std::vector<std::wstring>& paths) {
    std::vector<std::filesystem::path> videos;
    for (const std::wstring& item : paths) {
        const std::filesystem::path path(item);
        std::error_code error;
        if (std::filesystem::is_directory(path, error)) {
            const auto options = std::filesystem::directory_options::skip_permission_denied;
            for (std::filesystem::recursive_directory_iterator it(path, options, error), end; !error && it != end; it.increment(error)) {
                std::error_code fileError;
                if (it->is_regular_file(fileError) && IsVideo(it->path())) {
                    videos.push_back(it->path());
                }
            }
        } else if (IsVideo(path)) {
            videos.push_back(path);
        }
    }
    // Folder by folder, each one's name compared like Explorer does, so a folder's own
    // files come before those of its subfolders, and "2" comes before "10".
    const auto comparePaths = [](const std::filesystem::path& a, const std::filesystem::path& b) {
        auto x = a.begin();
        auto y = b.begin();
        for (; x != a.end() && y != b.end(); ++x, ++y) {
            if (const int order = StrCmpLogicalW(x->c_str(), y->c_str()); order != 0) {
                return order;
            }
        }
        return x == a.end() ? (y == b.end() ? 0 : -1) : 1;
    };
    std::stable_sort(videos.begin(), videos.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
        const int folders = comparePaths(a.parent_path(), b.parent_path());
        return folders != 0 ? folders < 0 : StrCmpLogicalW(a.filename().c_str(), b.filename().c_str()) < 0;
    });
    std::vector<std::wstring> result;
    result.reserve(videos.size());
    for (const std::filesystem::path& video : videos) {
        result.push_back(video.wstring());
    }
    return result;
}

std::vector<std::wstring> FindSeries(const std::wstring& video) {
    const std::filesystem::path videoPath(video);
    const std::wstring key = SeriesKey(videoPath.stem().wstring());
    // A name with no number to tell episodes apart isn't part of a series.
    if (key.find(L'#') == std::wstring::npos) {
        return {video};
    }

    std::vector<std::wstring> series;
    bool foundItself = false;
    std::error_code error;
    for (std::filesystem::directory_iterator it(videoPath.parent_path(), error), end; !error && it != end; it.increment(error)) {
        if (IsVideo(it->path()) && SeriesKey(it->path().stem().wstring()) == key) {
            series.push_back(it->path().wstring());
            foundItself = foundItself || _wcsicmp(it->path().filename().c_str(), videoPath.filename().c_str()) == 0;
        }
    }
    if (!foundItself) {
        series.push_back(video);  // The folder couldn't be read, or the file is gone.
    }
    SortByName(series);
    return series;
}
