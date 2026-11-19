#pragma once

#include <windows.h>

#include <commctrl.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Playlist.h"

// The playlist as a panel along the right of the main window: a title with the position,
// the list of files, and buttons for the play options. It only shows the list and reports
// what the user does with it; MainWindow owns the list and changes it.
//
// Files can be dropped into it and items dragged to another place. When the list has the
// keyboard, the keys for moving through it are its own; every other key goes to the player.
class PlaylistPanel {
public:
    enum class Option { AutoAdvance, RepeatOne, RepeatAll };

    struct Callbacks {
        std::function<void(size_t index)> play;
        std::function<void(const std::vector<size_t>& indices)> remove;
        // Moves the items at these indices, keeping their order, to just before the item
        // that is now at `before`. `before` may be the list's size, for the end.
        std::function<void(const std::vector<size_t>& indices, size_t before)> move;
        // Paths dropped from outside, which may include folders and files of other kinds.
        std::function<void(const std::vector<std::wstring>& paths, size_t before)> insert;
        std::function<void(Option option)> toggle;
        std::function<void()> close;
        // The user dragged the panel's edge. Device-independent pixels.
        std::function<void(int width)> resize;
    };

    // Starts hidden. Throws std::runtime_error if its windows can't be created.
    PlaylistPanel(HWND parent, Callbacks callbacks);
    ~PlaylistPanel();

    PlaylistPanel(const PlaylistPanel&) = delete;
    PlaylistPanel& operator=(const PlaylistPanel&) = delete;

    HWND Window() const { return window_; }
    // Client pixels of the parent.
    void Place(const RECT& bounds);
    void Show(bool visible);
    bool HasKeyboard() const;

    // current is empty while the file playing has been taken out of the list.
    void SetItems(const std::vector<std::wstring>& files, std::optional<size_t> current);
    void SetOptions(bool autoAdvance, RepeatMode repeat);
    void Select(size_t first, size_t count);

private:
    class DropTarget;
    enum class Hot { None, Close, AutoAdvance, RepeatOne, RepeatAll, Edge };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK ListProc(HWND list, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleListMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleNotify(NMHDR* header);
    LRESULT HandleCustomDraw(NMLVCUSTOMDRAW* draw);

    void UpdateMetrics();
    void Layout();
    void Paint(HDC dc);
    int Scale(int dips) const;
    RECT HeaderRect() const;
    RECT FooterRect() const;
    RECT CloseRect() const;
    RECT OptionRect(int which) const;
    Hot HitTest(POINT point) const;
    void SetHot(Hot hot);

    std::vector<size_t> SelectedIndices() const;
    std::optional<size_t> FocusedIndex() const;
    // Where an item dropped at this point of the panel would go.
    size_t InsertIndexAt(POINT panelPoint) const;
    void ShowInsertMark(std::optional<size_t> before);
    void ScrollNearEdge(POINT panelPoint);
    void BeginItemDrag();
    void EndItemDrag(bool drop);

    // From the drop target.
    bool HandleDragOver(POINT screenPoint);
    void HandleDragLeave();
    void HandleDrop(POINT screenPoint, const std::vector<std::wstring>& paths);

    HWND parent_;
    HWND window_ = nullptr;
    HWND list_ = nullptr;
    Callbacks callbacks_;
    DropTarget* dropTarget_ = nullptr;

    std::vector<std::wstring> names_;  // What each row shows.
    std::vector<std::wstring> paths_;  // The full path, shown on hover.
    std::optional<size_t> current_;
    bool autoAdvance_ = false;
    RepeatMode repeat_ = RepeatMode::None;

    UINT dpi_ = 0;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    HWND tooltip_ = nullptr;
    HIMAGELIST rowHeight_ = nullptr;  // An empty image list, only to make the rows taller.

    Hot hot_ = Hot::None;
    bool trackingLeave_ = false;
    std::optional<size_t> insertMark_;
    bool draggingItems_ = false;
    bool resizing_ = false;
    int resizeStartX_ = 0;  // Screen x where the edge was grabbed.
    int resizeStartWidth_ = 0;
};
