#include <windows.h>

#include <ole2.h>
#include <shellapi.h>

#include <exception>
#include <string>
#include <vector>

#include "FileDialogs.h"
#include "MainWindow.h"

namespace {

std::wstring PathFromCommandLine() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring path;
    if (arguments && count > 1) {
        path = arguments[1];
    }
    LocalFree(arguments);

    // URLs and the like are left alone.
    if (!path.empty() && path.find(L"://") == std::wstring::npos) {
        std::wstring full(32768, L'\0');
        const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length > 0 && length < full.size()) {
            full.resize(length);
            path = std::move(full);
        }
    }
    return path;
}

int RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

}  // namespace

// Every start is a window of its own, even with another already open: Explorer starts
// the program once for each selected file, and each of them plays in its own window.
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // OLE rather than plain COM, so the playlist panel can take files dragged onto it.
    const HRESULT comResult = OleInitialize(nullptr);

    int exitCode = 0;
    const std::wstring path = PathFromCommandLine();
    std::vector<std::wstring> chosen;
    if (path.empty()) {
        chosen = PickVideoFiles(nullptr);
    }
    if (!path.empty() || !chosen.empty()) {
        try {
            MainWindow window(instance, LoadSettings());
            window.Show(showCommand);
            if (chosen.empty()) {
                window.OpenFromExplorer(path);
            } else {
                window.OpenFiles(chosen);
            }
            exitCode = RunMessageLoop();
        } catch (const std::exception& error) {
            MessageBoxA(nullptr, error.what(), "Movie Player", MB_OK | MB_ICONERROR);
            exitCode = 1;
        }
    }

    if (SUCCEEDED(comResult)) {
        OleUninitialize();
    }
    return exitCode;
}
