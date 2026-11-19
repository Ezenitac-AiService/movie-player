#include "PlaylistPanel.h"

#include <commctrl.h>
#include <ole2.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace {

constexpr wchar_t kPanelClass[] = L"MoviePlayer.PlaylistPanel";
constexpr UINT_PTR kScrollTimer = 1;
constexpr UINT kScrollIntervalMs = 60;

// Device-independent pixels.
constexpr int kEdgeWidth = 5;  // The strip along the left that resizes the panel.
constexpr int kHeaderHeight = 38;
constexpr int kFooterHeight = 46;
constexpr int kPadding = 10;
constexpr int kButtonGap = 4;
constexpr int kButtonWidth = 38;
constexpr int kButtonHeight = 30;
constexpr int kIconSize = 16;
constexpr int kCloseSize = 28;
constexpr int kRowHeight = 26;

constexpr COLORREF kBackground = RGB(28, 28, 28);
constexpr COLORREF kEdge = RGB(58, 58, 58);
constexpr COLORREF kEdgeHot = RGB(110, 110, 110);
constexpr COLORREF kTitleText = RGB(236, 236, 236);
constexpr COLORREF kDimText = RGB(150, 150, 150);
constexpr COLORREF kRowText = RGB(218, 218, 218);
constexpr COLORREF kCurrentText = RGB(255, 190, 90);
constexpr COLORREF kHotFill = RGB(58, 58, 58);
constexpr COLORREF kButtonOn = RGB(40, 92, 158);
constexpr COLORREF kButtonOnHot = RGB(52, 110, 184);
constexpr COLORREF kInsertMark = RGB(255, 190, 90);

// Icons from the font Windows draws its own with: next, repeat one, repeat all.
constexpr const wchar_t* kOptionIcons[] = {L"", L"", L""};
// Shown on hover, in the same words as the F6 list.
constexpr const wchar_t* kOptionNames[] = {L"다음 파일 자동 재생", L"한 개 반복", L"전체 목록 반복"};
// Windows 11 has the first. Windows 10 has the second, with the same icons at the same places.
constexpr const wchar_t* kIconFonts[] = {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"};

bool FontExists(const wchar_t* face) {
    LOGFONTW wanted{};
    wanted.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(wanted.lfFaceName, face, _TRUNCATE);
    bool found = false;
    const HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &wanted, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM data) -> int {
        *reinterpret_cast<bool*>(data) = true;
        return 0;
    }, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

void FillColor(HDC dc, const RECT& rect, COLORREF color) {
    SetDCBrushColor(dc, color);
    FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void DrawRoundedFill(HDC dc, const RECT& rect, COLORREF color, int radius) {
    SetDCBrushColor(dc, color);
    SetDCPenColor(dc, color);
    const HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(DC_BRUSH));
    const HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void CopyText(const std::wstring& text, wchar_t* buffer, int size) {
    if (buffer && size > 0) {
        wcsncpy_s(buffer, static_cast<size_t>(size), text.c_str(), _TRUNCATE);
    }
}

}  // namespace

// Accepts files dragged in from Explorer and the like, and shows where they would go.
class PlaylistPanel::DropTarget : public IDropTarget {
public:
    explicit DropTarget(PlaylistPanel& owner) : owner_(owner) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *object = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = InterlockedDecrement(&references_);
        if (left == 0) {
            delete this;
        }
        return left;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        accepting_ = data && data->QueryGetData(&format) == S_OK;
        return DragOver(0, point, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD* effect) override {
        const bool accepted = accepting_ && owner_.HandleDragOver({point.x, point.y});
        *effect = accepted ? Effect(*effect) : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        owner_.HandleDragLeave();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL point, DWORD* effect) override {
        std::vector<std::wstring> paths;
        FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM medium{};
        if (accepting_ && data && SUCCEEDED(data->GetData(&format, &medium))) {
            if (const auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal))) {
                const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < count; ++i) {
                    const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                    std::wstring path(length, L'\0');
                    if (length > 0 && DragQueryFileW(drop, i, path.data(), length + 1) == length) {
                        paths.push_back(std::move(path));
                    }
                }
                GlobalUnlock(medium.hGlobal);
            }
            ReleaseStgMedium(&medium);
        }
        *effect = paths.empty() ? DROPEFFECT_NONE : Effect(*effect);
        owner_.HandleDrop({point.x, point.y}, paths);
        return S_OK;
    }

private:
    // The files stay where they are, so this is a copy if the source allows it.
    static DWORD Effect(DWORD allowed) {
        for (const DWORD effect : {DROPEFFECT_COPY, DROPEFFECT_LINK, DROPEFFECT_MOVE}) {
            if (allowed & effect) {
                return effect;
            }
        }
        return DROPEFFECT_NONE;
    }

    PlaylistPanel& owner_;
    LONG references_ = 1;
    bool accepting_ = false;
};

PlaylistPanel::PlaylistPanel(HWND parent, Callbacks callbacks) : parent_(parent), callbacks_(std::move(callbacks)) {
    const HINSTANCE instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &PlaylistPanel::WindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kPanelClass;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw std::runtime_error("RegisterClassEx failed for the playlist panel");
    }
    window_ = CreateWindowExW(0, kPanelClass, nullptr, WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr,
                              instance, this);
    if (!window_) {
        throw std::runtime_error("CreateWindowEx failed for the playlist panel");
    }
    list_ = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS |
                                LVS_OWNERDATA | LVS_SHAREIMAGELISTS,
                            0, 0, 0, 0, window_, nullptr, instance, nullptr);
    if (!list_) {
        throw std::runtime_error("CreateWindowEx failed for the playlist");
    }
    // The dark theme gives the list dark scroll bars to match the panel.
    SetWindowTheme(list_, L"DarkMode_Explorer", nullptr);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    ListView_SetBkColor(list_, kBackground);
    ListView_SetTextBkColor(list_, kBackground);
    ListView_SetTextColor(list_, kRowText);
    LVCOLUMNW column{};
    column.mask = LVCF_WIDTH;
    column.cx = 100;
    ListView_InsertColumn(list_, 0, &column);
    // Focus rectangles would show up as dotted boxes on the dark rows.
    SendMessageW(list_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    SetWindowSubclass(list_, &PlaylistPanel::ListProc, 1, reinterpret_cast<DWORD_PTR>(this));

    // The buttons are only icons, so their names come up on hover.
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window_, nullptr, instance, nullptr);
    for (UINT i = 0; i < 4; ++i) {
        TTTOOLINFOW tool{sizeof(tool)};
        tool.uFlags = TTF_SUBCLASS;
        tool.hwnd = window_;
        tool.uId = i + 1;
        tool.lpszText = const_cast<wchar_t*>(i < 3 ? kOptionNames[i] : L"닫기");
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    UpdateMetrics();

    dropTarget_ = new DropTarget(*this);
    RegisterDragDrop(window_, dropTarget_);
}

PlaylistPanel::~PlaylistPanel() {
    RevokeDragDrop(window_);
    if (dropTarget_) {
        dropTarget_->Release();
    }
    DestroyWindow(window_);
    for (HFONT font : {font_, boldFont_, titleFont_, iconFont_}) {
        if (font) {
            DeleteObject(font);
        }
    }
    if (rowHeight_) {
        ImageList_Destroy(rowHeight_);
    }
}

void PlaylistPanel::Place(const RECT& bounds) {
    SetWindowPos(window_, HWND_TOP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, SWP_NOACTIVATE);
}

void PlaylistPanel::Show(bool visible) {
    if (!visible && HasKeyboard()) {
        SetFocus(parent_);
    }
    ShowWindow(window_, visible ? SW_SHOWNA : SW_HIDE);
}

bool PlaylistPanel::HasKeyboard() const {
    return GetFocus() == list_;
}

void PlaylistPanel::SetItems(const std::vector<std::wstring>& files, std::optional<size_t> current) {
    names_.clear();
    for (size_t i = 0; i < files.size(); ++i) {
        names_.push_back(std::to_wstring(i + 1) + L". " + std::filesystem::path(files[i]).filename().wstring());
    }
    paths_ = files;
    current_ = current;
    ListView_SetItemCountEx(list_, static_cast<int>(names_.size()), LVSICF_NOSCROLL);
    InvalidateRect(list_, nullptr, FALSE);
    Layout();  // A scroll bar may have come or gone.
    const RECT header = HeaderRect();
    InvalidateRect(window_, &header, FALSE);
}

void PlaylistPanel::SetOptions(bool autoAdvance, RepeatMode repeat) {
    autoAdvance_ = autoAdvance;
    repeat_ = repeat;
    const RECT footer = FooterRect();
    InvalidateRect(window_, &footer, FALSE);
}

void PlaylistPanel::Select(size_t first, size_t count) {
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (size_t i = first; i < first + count && i < names_.size(); ++i) {
        ListView_SetItemState(list_, static_cast<int>(i), LVIS_SELECTED, LVIS_SELECTED);
    }
    if (first < names_.size()) {
        ListView_SetItemState(list_, static_cast<int>(first), LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_SetSelectionMark(list_, static_cast<int>(first));
        ListView_EnsureVisible(list_, static_cast<int>(first + count - 1 < names_.size() ? first + count - 1 : first), FALSE);
        ListView_EnsureVisible(list_, static_cast<int>(first), FALSE);
    }
}

LRESULT CALLBACK PlaylistPanel::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<PlaylistPanel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self || !self->window_) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    return self->HandleMessage(message, wParam, lParam);
}

LRESULT CALLBACK PlaylistPanel::ListProc(HWND, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    return reinterpret_cast<PlaylistPanel*>(data)->HandleListMessage(message, wParam, lParam);
}

LRESULT PlaylistPanel::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window_, &paint);
        RECT client{};
        GetClientRect(window_, &client);
        // Drawn off screen first, so the buttons don't flicker as they change.
        const HDC buffer = CreateCompatibleDC(dc);
        const HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
        const HGDIOBJ old = SelectObject(buffer, bitmap);
        Paint(buffer);
        BitBlt(dc, 0, 0, client.right, client.bottom, buffer, 0, 0, SRCCOPY);
        SelectObject(buffer, old);
        DeleteObject(bitmap);
        DeleteDC(buffer);
        EndPaint(window_, &paint);
        return 0;
    }
    case WM_SIZE:
        Layout();
        return 0;
    case WM_DPICHANGED_AFTERPARENT:
        UpdateMetrics();
        Layout();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_SETCURSOR:
        if (reinterpret_cast<HWND>(wParam) == window_ && LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window_, &point);
            SetCursor(LoadCursorW(nullptr, resizing_ || HitTest(point) == Hot::Edge ? IDC_SIZEWE : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (resizing_) {
            POINT screen = point;
            ClientToScreen(window_, &screen);
            // Dragging the left edge to the left widens the panel.
            const int width = resizeStartWidth_ + resizeStartX_ - screen.x;
            if (callbacks_.resize) {
                callbacks_.resize(MulDiv(width, 96, static_cast<int>(dpi_)));
            }
            return 0;
        }
        if (draggingItems_) {
            ShowInsertMark(InsertIndexAt(point));
            return 0;
        }
        if (!trackingLeave_) {
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
            trackingLeave_ = TrackMouseEvent(&track) != FALSE;
        }
        SetHot(HitTest(point));
        return 0;
    }
    case WM_MOUSELEAVE:
        trackingLeave_ = false;
        SetHot(Hot::None);
        return 0;
    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        switch (HitTest(point)) {
        case Hot::Edge: {
            RECT bounds{};
            GetWindowRect(window_, &bounds);
            POINT screen = point;
            ClientToScreen(window_, &screen);
            resizing_ = true;
            resizeStartX_ = screen.x;
            resizeStartWidth_ = bounds.right - bounds.left;
            SetCapture(window_);
            break;
        }
        case Hot::Close:
            if (callbacks_.close) {
                callbacks_.close();
            }
            break;
        case Hot::AutoAdvance:
        case Hot::RepeatOne:
        case Hot::RepeatAll:
            if (callbacks_.toggle) {
                const Hot hot = HitTest(point);
                callbacks_.toggle(hot == Hot::AutoAdvance ? Option::AutoAdvance : hot == Hot::RepeatOne ? Option::RepeatOne : Option::RepeatAll);
            }
            break;
        case Hot::None:
            break;
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (resizing_) {
            resizing_ = false;
            ReleaseCapture();
        } else if (draggingItems_) {
            EndItemDrag(true);
        }
        return 0;
    case WM_CAPTURECHANGED:
        resizing_ = false;
        if (draggingItems_) {
            EndItemDrag(false);
        }
        return 0;
    case WM_TIMER:
        if (wParam == kScrollTimer) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window_, &point);
            ScrollNearEdge(point);
            if (draggingItems_ || insertMark_) {
                ShowInsertMark(InsertIndexAt(point));
            }
        }
        return 0;
    case WM_NOTIFY:
        return HandleNotify(reinterpret_cast<NMHDR*>(lParam));
    // Neither goes on to the player: the wheel would change the volume, and the panel has no menu.
    case WM_MOUSEWHEEL:
    case WM_CONTEXTMENU:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

LRESULT PlaylistPanel::HandleListMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_KEYDOWN:
        if (draggingItems_ && wParam == VK_ESCAPE) {
            EndItemDrag(false);
            return 0;
        }
        switch (wParam) {
        case VK_UP:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
            break;  // The list's own: moving through it, with Shift or Ctrl for choosing several.
        case VK_RETURN:
            if (const std::optional<size_t> focused = FocusedIndex(); focused && callbacks_.play) {
                callbacks_.play(*focused);
            }
            return 0;
        case VK_DELETE:
            if (const std::vector<size_t> selected = SelectedIndices(); !selected.empty() && callbacks_.remove) {
                callbacks_.remove(selected);
            }
            return 0;
        default:
            // Every other key is the player's, as if the list didn't have the keyboard.
            return SendMessageW(parent_, message, wParam, lParam);
        }
        break;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        return SendMessageW(parent_, message, wParam, lParam);
    case WM_CHAR:
        return 0;  // No jumping to an item by typing its name: letters are shortcuts.
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(list_, nullptr, FALSE);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(list_, &PlaylistPanel::ListProc, 1);
        break;
    default:
        break;
    }
    return DefSubclassProc(list_, message, wParam, lParam);
}

LRESULT PlaylistPanel::HandleNotify(NMHDR* header) {
    if (header->hwndFrom != list_) {
        return 0;
    }
    switch (header->code) {
    case LVN_GETDISPINFOW: {
        auto* info = reinterpret_cast<NMLVDISPINFOW*>(header);
        const auto index = static_cast<size_t>(info->item.iItem);
        if ((info->item.mask & LVIF_TEXT) && index < names_.size()) {
            CopyText(names_[index], info->item.pszText, info->item.cchTextMax);
        }
        return 0;
    }
    case LVN_GETINFOTIPW: {
        auto* tip = reinterpret_cast<NMLVGETINFOTIPW*>(header);
        const auto index = static_cast<size_t>(tip->iItem);
        if (index < paths_.size()) {
            CopyText(paths_[index], tip->pszText, tip->cchTextMax);
        }
        return 0;
    }
    case LVN_ODFINDITEMW:
        return -1;
    case NM_DBLCLK: {
        const auto* activate = reinterpret_cast<NMITEMACTIVATE*>(header);
        if (activate->iItem >= 0 && static_cast<size_t>(activate->iItem) < names_.size() && callbacks_.play) {
            callbacks_.play(static_cast<size_t>(activate->iItem));
        }
        return 0;
    }
    case LVN_BEGINDRAG:
        BeginItemDrag();
        return 0;
    case NM_CUSTOMDRAW:
        return HandleCustomDraw(reinterpret_cast<NMLVCUSTOMDRAW*>(header));
    default:
        return 0;
    }
}

LRESULT PlaylistPanel::HandleCustomDraw(NMLVCUSTOMDRAW* draw) {
    switch (draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPREPAINT: {
        const bool current = current_ && draw->nmcd.dwItemSpec == *current_;
        draw->clrText = current ? kCurrentText : kRowText;
        draw->clrTextBk = kBackground;
        if (current) {
            SelectObject(draw->nmcd.hdc, boldFont_);
            return CDRF_NEWFONT;
        }
        return CDRF_DODEFAULT;
    }
    case CDDS_POSTPAINT:
        if (insertMark_) {
            RECT row{};
            int y = Scale(1);
            if (!names_.empty()) {
                const size_t at = std::min(*insertMark_, names_.size() - 1);
                ListView_GetItemRect(list_, static_cast<int>(at), &row, LVIR_BOUNDS);
                y = *insertMark_ < names_.size() ? row.top : row.bottom;
            }
            RECT client{};
            GetClientRect(list_, &client);
            const int thickness = std::max(2, Scale(2));
            FillColor(draw->nmcd.hdc, RECT{client.left, y - thickness / 2, client.right, y - thickness / 2 + thickness}, kInsertMark);
        }
        return CDRF_DODEFAULT;
    default:
        return CDRF_DODEFAULT;
    }
}

int PlaylistPanel::Scale(int dips) const {
    return MulDiv(dips, static_cast<int>(dpi_), 96);
}

void PlaylistPanel::UpdateMetrics() {
    dpi_ = GetDpiForWindow(window_);
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi_);
    LOGFONTW logFont = metrics.lfMessageFont;
    for (HFONT font : {font_, boldFont_, titleFont_, iconFont_}) {
        if (font) {
            DeleteObject(font);
        }
    }
    font_ = CreateFontIndirectW(&logFont);
    logFont.lfWeight = FW_BOLD;
    boldFont_ = CreateFontIndirectW(&logFont);
    logFont.lfWeight = FW_SEMIBOLD;
    logFont.lfHeight = logFont.lfHeight * 11 / 10;
    titleFont_ = CreateFontIndirectW(&logFont);
    LOGFONTW iconFont{};
    iconFont.lfHeight = -Scale(kIconSize);
    iconFont.lfCharSet = DEFAULT_CHARSET;
    iconFont.lfQuality = CLEARTYPE_QUALITY;
    for (const wchar_t* face : kIconFonts) {
        if (FontExists(face)) {
            wcsncpy_s(iconFont.lfFaceName, face, _TRUNCATE);
            break;
        }
    }
    iconFont_ = CreateFontIndirectW(&iconFont);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    // A list's rows are as tall as its icons, and this one has none to show.
    if (rowHeight_) {
        ImageList_Destroy(rowHeight_);
    }
    rowHeight_ = ImageList_Create(1, Scale(kRowHeight), ILC_COLOR32, 1, 0);
    ListView_SetImageList(list_, rowHeight_, LVSIL_SMALL);
}

RECT PlaylistPanel::HeaderRect() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {client.left + Scale(kEdgeWidth), client.top, client.right, client.top + Scale(kHeaderHeight)};
}

RECT PlaylistPanel::FooterRect() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {client.left + Scale(kEdgeWidth), std::max(client.top + Scale(kHeaderHeight), client.bottom - Scale(kFooterHeight)), client.right,
            client.bottom};
}

RECT PlaylistPanel::CloseRect() const {
    const RECT header = HeaderRect();
    const int size = Scale(kCloseSize);
    const int top = header.top + (header.bottom - header.top - size) / 2;
    return {header.right - Scale(kPadding / 2) - size, top, header.right - Scale(kPadding / 2), top + size};
}

RECT PlaylistPanel::OptionRect(int which) const {
    const RECT footer = FooterRect();
    const int x = footer.left + Scale(kPadding) + which * (Scale(kButtonWidth) + Scale(kButtonGap));
    const int top = footer.top + (footer.bottom - footer.top - Scale(kButtonHeight)) / 2;
    return {x, top, x + Scale(kButtonWidth), top + Scale(kButtonHeight)};
}

PlaylistPanel::Hot PlaylistPanel::HitTest(POINT point) const {
    RECT client{};
    GetClientRect(window_, &client);
    if (point.x >= client.left && point.x < client.left + Scale(kEdgeWidth) && point.y >= client.top && point.y < client.bottom) {
        return Hot::Edge;
    }
    const RECT close = CloseRect();
    if (PtInRect(&close, point)) {
        return Hot::Close;
    }
    for (int i = 0; i < 3; ++i) {
        const RECT option = OptionRect(i);
        if (PtInRect(&option, point)) {
            return i == 0 ? Hot::AutoAdvance : i == 1 ? Hot::RepeatOne : Hot::RepeatAll;
        }
    }
    return Hot::None;
}

void PlaylistPanel::SetHot(Hot hot) {
    if (hot != hot_) {
        hot_ = hot;
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void PlaylistPanel::Layout() {
    RECT client{};
    GetClientRect(window_, &client);
    const RECT header = HeaderRect();
    const RECT footer = FooterRect();
    MoveWindow(list_, header.left, header.bottom, header.right - header.left, std::max(0L, footer.top - header.bottom), TRUE);
    RECT listClient{};
    GetClientRect(list_, &listClient);
    ListView_SetColumnWidth(list_, 0, listClient.right);
    for (UINT i = 0; i < 4; ++i) {
        TTTOOLINFOW tool{sizeof(tool)};
        tool.hwnd = window_;
        tool.uId = i + 1;
        tool.rect = i < 3 ? OptionRect(static_cast<int>(i)) : CloseRect();
        SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void PlaylistPanel::Paint(HDC dc) {
    RECT client{};
    GetClientRect(window_, &client);
    FillColor(dc, client, kBackground);
    FillColor(dc, RECT{client.left, client.top, client.left + std::max(1, Scale(1)), client.bottom}, hot_ == Hot::Edge || resizing_ ? kEdgeHot : kEdge);
    SetBkMode(dc, TRANSPARENT);

    const RECT header = HeaderRect();
    const RECT close = CloseRect();
    const std::wstring position = (current_ ? std::to_wstring(*current_ + 1) : std::wstring(L"-")) + L" / " + std::to_wstring(names_.size());
    RECT positionRect{header.left + Scale(kPadding), header.top, close.left - Scale(kPadding / 2), header.bottom};
    SelectObject(dc, font_);
    SetTextColor(dc, kDimText);
    DrawTextW(dc, position.c_str(), -1, &positionRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    SIZE positionSize{};
    GetTextExtentPoint32W(dc, position.c_str(), static_cast<int>(position.size()), &positionSize);
    RECT titleRect{header.left + Scale(kPadding), header.top, positionRect.right - positionSize.cx - Scale(kPadding), header.bottom};
    SelectObject(dc, titleFont_);
    SetTextColor(dc, kTitleText);
    DrawTextW(dc, L"재생 목록", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (hot_ == Hot::Close) {
        DrawRoundedFill(dc, close, kHotFill, Scale(6));
    }
    // A cross, drawn rather than typed, so it's centered whatever the font.
    {
        const int arm = Scale(5);
        const int cx = (close.left + close.right) / 2;
        const int cy = (close.top + close.bottom) / 2;
        const HPEN pen = CreatePen(PS_SOLID, std::max(1, Scale(1)), hot_ == Hot::Close ? kTitleText : kDimText);
        const HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, cx - arm, cy - arm, nullptr);
        LineTo(dc, cx + arm + 1, cy + arm + 1);
        MoveToEx(dc, cx + arm, cy - arm, nullptr);
        LineTo(dc, cx - arm - 1, cy + arm + 1);
        SelectObject(dc, old);
        DeleteObject(pen);
    }

    const RECT footer = FooterRect();
    FillColor(dc, RECT{footer.left, footer.top, footer.right, footer.top + std::max(1, Scale(1))}, kEdge);
    const bool on[3] = {autoAdvance_, repeat_ == RepeatMode::One, repeat_ == RepeatMode::All};
    const Hot hots[3] = {Hot::AutoAdvance, Hot::RepeatOne, Hot::RepeatAll};
    SelectObject(dc, iconFont_);
    for (int i = 0; i < 3; ++i) {
        RECT option = OptionRect(i);
        const bool hot = hot_ == hots[i];
        // On is a filled button. Off is just the icon, dimmed, which gets a fill on hover.
        if (on[i] || hot) {
            DrawRoundedFill(dc, option, on[i] ? (hot ? kButtonOnHot : kButtonOn) : kHotFill, Scale(6));
        }
        SetTextColor(dc, on[i] || hot ? kTitleText : kDimText);
        DrawTextW(dc, kOptionIcons[i], -1, &option, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

std::vector<size_t> PlaylistPanel::SelectedIndices() const {
    std::vector<size_t> selected;
    for (int i = ListView_GetNextItem(list_, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list_, i, LVNI_SELECTED)) {
        selected.push_back(static_cast<size_t>(i));
    }
    return selected;
}

std::optional<size_t> PlaylistPanel::FocusedIndex() const {
    const int focused = ListView_GetNextItem(list_, -1, LVNI_FOCUSED);
    return focused >= 0 && static_cast<size_t>(focused) < names_.size() ? std::optional<size_t>(static_cast<size_t>(focused)) : std::nullopt;
}

size_t PlaylistPanel::InsertIndexAt(POINT panelPoint) const {
    if (names_.empty()) {
        return 0;
    }
    POINT point = panelPoint;
    MapWindowPoints(window_, list_, &point, 1);
    const int top = ListView_GetTopIndex(list_);
    RECT row{};
    ListView_GetItemRect(list_, top, &row, LVIR_BOUNDS);
    const int height = std::max(1L, row.bottom - row.top);
    // The gap between two rows nearest the point.
    const double rows = std::floor(static_cast<double>(point.y - row.top) / height + 0.5);
    return static_cast<size_t>(std::clamp(static_cast<double>(top) + rows, 0.0, static_cast<double>(names_.size())));
}

void PlaylistPanel::ShowInsertMark(std::optional<size_t> before) {
    if (before != insertMark_) {
        insertMark_ = before;
        InvalidateRect(list_, nullptr, FALSE);
    }
}

void PlaylistPanel::ScrollNearEdge(POINT panelPoint) {
    POINT point = panelPoint;
    MapWindowPoints(window_, list_, &point, 1);
    RECT client{};
    GetClientRect(list_, &client);
    const int band = Scale(kRowHeight);
    if (point.y < client.top + band / 2) {
        ListView_Scroll(list_, 0, -band);
    } else if (point.y > client.bottom - band / 2) {
        ListView_Scroll(list_, 0, band);
    }
}

void PlaylistPanel::BeginItemDrag() {
    if (SelectedIndices().empty()) {
        return;
    }
    draggingItems_ = true;
    SetCapture(window_);
    SetTimer(window_, kScrollTimer, kScrollIntervalMs, nullptr);
}

void PlaylistPanel::EndItemDrag(bool drop) {
    draggingItems_ = false;  // First, so losing the capture below doesn't come back here.
    KillTimer(window_, kScrollTimer);
    if (GetCapture() == window_) {
        ReleaseCapture();
    }
    const std::optional<size_t> before = insertMark_;
    ShowInsertMark(std::nullopt);
    if (drop && before && callbacks_.move) {
        callbacks_.move(SelectedIndices(), *before);
    }
}

bool PlaylistPanel::HandleDragOver(POINT screenPoint) {
    POINT point = screenPoint;
    ScreenToClient(window_, &point);
    if (!insertMark_) {
        SetTimer(window_, kScrollTimer, kScrollIntervalMs, nullptr);
    }
    ShowInsertMark(InsertIndexAt(point));
    return true;
}

void PlaylistPanel::HandleDragLeave() {
    KillTimer(window_, kScrollTimer);
    ShowInsertMark(std::nullopt);
}

void PlaylistPanel::HandleDrop(POINT screenPoint, const std::vector<std::wstring>& paths) {
    POINT point = screenPoint;
    ScreenToClient(window_, &point);
    const size_t before = InsertIndexAt(point);
    HandleDragLeave();
    if (!paths.empty() && callbacks_.insert) {
        callbacks_.insert(paths, before);
    }
}
