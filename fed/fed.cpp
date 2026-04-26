#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <ole2.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "Scintilla.h"
#include "resource.h"

namespace {

constexpr wchar_t kAppName[] = L"fed";
constexpr wchar_t kWindowClassName[] = L"fed.MainWindow";
constexpr wchar_t kSearchWindowClassName[] = L"fed.SearchWindow";
constexpr int kDefaultEditorFontSize = 11;
constexpr int kMinLineNumberDigits = 3;
constexpr int kStatusHeight = 24;
constexpr int kLineNumberSpacerWidth = 6;
constexpr int kSearchIndicator = 8;
constexpr int kSearchDialogWidth = 475;
constexpr int kFindDialogHeight = 190;
constexpr int kReplaceDialogHeight = 230;
constexpr int kSearchTextId = 5001;
constexpr int kReplaceTextId = 5002;
constexpr int kMatchCaseId = 5003;
constexpr int kWholeWordId = 5004;
constexpr int kFindNextId = 5005;
constexpr int kFindPreviousId = 5006;
constexpr int kReplaceButtonId = 5007;
constexpr int kReplaceAllButtonId = 5008;
constexpr int kCloseSearchId = 5009;
constexpr int kOccurrenceLabelId = 5010;
constexpr ULONGLONG kMaxDroppedFileBytes = 64ull * 1024ull * 1024ull;
constexpr COLORREF kDarkEditorBack = RGB(40, 44, 52);
constexpr COLORREF kDarkEditorFore = RGB(220, 223, 228);
constexpr COLORREF kDarkGutterBack = RGB(46, 50, 58);
constexpr COLORREF kDarkGutterFore = RGB(150, 156, 166);
constexpr COLORREF kDarkSelectionBack = RGB(86, 97, 118);
constexpr COLORREF kDarkCurrentLineBack = RGB(48, 53, 63);
constexpr COLORREF kDarkCaretFore = RGB(220, 223, 228);
constexpr COLORREF kLightEditorBack = RGB(255, 255, 255);
constexpr COLORREF kLightEditorFore = RGB(0, 0, 0);
constexpr COLORREF kLightGutterBack = RGB(236, 236, 236);
constexpr COLORREF kLightGutterFore = RGB(120, 120, 120);
constexpr COLORREF kLightSelectionBack = RGB(201, 221, 245);
constexpr COLORREF kLightCurrentLineBack = RGB(248, 248, 248);
constexpr COLORREF kLightCaretFore = RGB(0, 0, 0);
constexpr COLORREF kSearchHighlight = RGB(255, 210, 80);
constexpr COLORREF kDarkDialogBack = RGB(32, 36, 42);
constexpr COLORREF kDarkControlBack = RGB(43, 47, 54);
constexpr COLORREF kChromeBlue = RGB(86, 129, 168);
constexpr COLORREF kLightStatusBarBack = RGB(180, 205, 230);
constexpr COLORREF kChromeText = RGB(25, 33, 41);
constexpr wchar_t kDarkThemeClassName[] = L"DarkMode_Explorer";
constexpr wchar_t kLightThemeClassName[] = L"Explorer";

enum class StyleMode {
    System,
    Light,
    Dark,
};

enum class SearchDialogMode {
    Find,
    Replace,
};

struct EditorPalette {
    COLORREF back;
    COLORREF fore;
    COLORREF gutterBack;
    COLORREF gutterFore;
    COLORREF selectionBack;
    COLORREF currentLineBack;
    COLORREF caretFore;
};

EditorPalette PaletteForDarkMode(bool darkModeEnabled) {
    if (darkModeEnabled) {
        return {
            kDarkEditorBack,
            kDarkEditorFore,
            kDarkGutterBack,
            kDarkGutterFore,
            kDarkSelectionBack,
            kDarkCurrentLineBack,
            kDarkCaretFore,
        };
    }

    return {
        kLightEditorBack,
        kLightEditorFore,
        kLightGutterBack,
        kLightGutterFore,
        kLightSelectionBack,
        kLightCurrentLineBack,
        kLightCaretFore,
    };
}

void ApplyImmersiveDarkMode(HWND window, bool darkModeEnabled) {
    if (window == nullptr) {
        return;
    }

    const BOOL useDarkMode = darkModeEnabled ? TRUE : FALSE;
    if (FAILED(::DwmSetWindowAttribute(
            window,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &useDarkMode,
            sizeof(useDarkMode)))) {
        constexpr DWORD legacyDarkModeAttribute = 19;
        ::DwmSetWindowAttribute(window, legacyDarkModeAttribute, &useDarkMode, sizeof(useDarkMode));
    }
}

std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring wide(required, L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

std::string WideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string utf8(required, '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), required, nullptr, nullptr);
    return utf8;
}

std::wstring BytesToWide(UINT codePage, DWORD flags, std::string_view bytes) {
    if (bytes.empty()) {
        return {};
    }

    const int required = ::MultiByteToWideChar(
        codePage, flags, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring wide(required, L'\0');
    ::MultiByteToWideChar(
        codePage, flags, bytes.data(), static_cast<int>(bytes.size()), wide.data(), required);
    return wide;
}

std::wstring SystemMessageWide(DWORD error) {
    wchar_t *buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = ::FormatMessageW(
        flags, nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, length);
        while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }
    if (message.empty()) {
        return L"Unknown error";
    }
    return message;
}

void SetSystemErrorMessage(DWORD error, std::wstring *errorMessage) {
    *errorMessage = SystemMessageWide(error);
}

class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    ~UniqueHandle() {
        if (IsValid()) {
            ::CloseHandle(handle_);
        }
    }

    bool IsValid() const {
        return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
    }

    HANDLE Get() const {
        return handle_;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

std::wstring DirectoryFromPath(const std::wstring &path) {
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return {};
    }
    return path.substr(0, separator);
}

std::wstring FileNameFromPath(const std::wstring &path) {
    const size_t separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) {
        return path;
    }
    return path.substr(separator + 1);
}

bool ReadRegistryDword(HKEY root, const wchar_t *subKey, const wchar_t *valueName, DWORD *value) {
    DWORD type = 0;
    DWORD size = sizeof(*value);
    return ::RegGetValueW(root, subKey, valueName, RRF_RT_REG_DWORD, &type, value, &size) == ERROR_SUCCESS;
}

bool IsHighContrastEnabled() {
    HIGHCONTRASTW highContrast = {};
    highContrast.cbSize = sizeof(highContrast);
    if (!::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast), &highContrast, 0)) {
        return false;
    }
    return (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

bool IsDarkModeEnabledByOs() {
    if (IsHighContrastEnabled()) {
        return false;
    }

    constexpr wchar_t personalizeKey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    DWORD appsUseLightTheme = 1;
    if (ReadRegistryDword(HKEY_CURRENT_USER, personalizeKey, L"AppsUseLightTheme", &appsUseLightTheme)) {
        return appsUseLightTheme == 0;
    }

    DWORD systemUsesLightTheme = 1;
    if (ReadRegistryDword(HKEY_CURRENT_USER, personalizeKey, L"SystemUsesLightTheme", &systemUsesLightTheme)) {
        return systemUsesLightTheme == 0;
    }

    return false;
}

bool TryGetFileSize(const std::wstring &path, ULONGLONG *sizeBytes) {
    WIN32_FILE_ATTRIBUTE_DATA attributes = {};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return false;
    }
    if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return false;
    }

    ULARGE_INTEGER size = {};
    size.HighPart = attributes.nFileSizeHigh;
    size.LowPart = attributes.nFileSizeLow;
    *sizeBytes = size.QuadPart;
    return true;
}

bool ReadAllBytes(const std::wstring &path, std::string *contents, std::wstring *errorMessage) {
    UniqueHandle file(::CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid()) {
        SetSystemErrorMessage(::GetLastError(), errorMessage);
        return false;
    }

    LARGE_INTEGER fileSize = {};
    if (!::GetFileSizeEx(file.Get(), &fileSize)) {
        SetSystemErrorMessage(::GetLastError(), errorMessage);
        return false;
    }

    if (fileSize.QuadPart < 0) {
        *errorMessage = L"File is too large to open.";
        return false;
    }

    const ULONGLONG byteCount = static_cast<ULONGLONG>(fileSize.QuadPart);
    if (byteCount > static_cast<ULONGLONG>((std::numeric_limits<size_t>::max)())) {
        *errorMessage = L"File is too large to open.";
        return false;
    }

    contents->assign(static_cast<size_t>(byteCount), '\0');
    size_t totalRead = 0;
    BOOL ok = TRUE;
    while (ok && totalRead < contents->size()) {
        const DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(contents->size() - totalRead, 1u << 20));
        DWORD read = 0;
        ok = ::ReadFile(file.Get(), contents->data() + totalRead, chunkSize, &read, nullptr);
        totalRead += read;
        if (read == 0) {
            break;
        }
    }
    if (!ok) {
        SetSystemErrorMessage(::GetLastError(), errorMessage);
        return false;
    }
    contents->resize(totalRead);
    return true;
}

bool WriteAllBytes(const std::wstring &path, const std::string &contents, std::wstring *errorMessage) {
    UniqueHandle file(::CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.IsValid()) {
        SetSystemErrorMessage(::GetLastError(), errorMessage);
        return false;
    }

    size_t totalWritten = 0;
    BOOL ok = TRUE;
    while (ok && totalWritten < contents.size()) {
        const DWORD chunkSize = static_cast<DWORD>(std::min<size_t>(contents.size() - totalWritten, 1u << 20));
        DWORD written = 0;
        ok = ::WriteFile(file.Get(), contents.data() + totalWritten, chunkSize, &written, nullptr);
        totalWritten += written;
        if (written == 0) {
            break;
        }
    }
    if (!ok || totalWritten != contents.size()) {
        if (!ok) {
            SetSystemErrorMessage(::GetLastError(), errorMessage);
        } else {
            *errorMessage = L"Unable to write the complete file.";
        }
        return false;
    }

    return true;
}

int DetectEolMode(std::string_view text) {
    size_t crlf = 0;
    size_t lf = 0;
    size_t cr = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++crlf;
                ++i;
            } else {
                ++cr;
            }
        } else if (ch == '\n') {
            ++lf;
        }
    }

    if (crlf == 0 && lf == 0 && cr == 0) {
        return SC_EOL_CRLF;
    }
    if (crlf >= lf && crlf >= cr) {
        return SC_EOL_CRLF;
    }
    if (lf >= cr) {
        return SC_EOL_LF;
    }
    return SC_EOL_CR;
}

bool IsFontInstalled(const wchar_t *faceName) {
    HDC dc = ::GetDC(nullptr);
    if (dc == nullptr) {
        return false;
    }

    LOGFONTW logFont = {};
    logFont.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(logFont.lfFaceName, faceName, _TRUNCATE);

    bool found = false;
    ::EnumFontFamiliesExW(
        dc,
        &logFont,
        [](const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM parameter) -> int {
            *reinterpret_cast<bool *>(parameter) = true;
            return 0;
        },
        reinterpret_cast<LPARAM>(&found),
        0);
    ::ReleaseDC(nullptr, dc);
    return found;
}

std::wstring SelectEditorFont() {
    constexpr std::array<const wchar_t *, 4> candidates = {
        L"Cascadia Mono",
        L"Consolas",
        L"Lucida Console",
        L"Courier New",
    };

    for (const wchar_t *candidate : candidates) {
        if (IsFontInstalled(candidate)) {
            return candidate;
        }
    }
    return L"Consolas";
}

HFONT CreateUiFont(const std::wstring &faceName, int pointSize, int weight) {
    const int dpi = ::GetDpiForSystem();
    HFONT font = ::CreateFontW(
        -::MulDiv(pointSize, dpi, 72),
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        faceName.c_str());
    return font;
}

HFONT CreateMessageFont() {
    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        return ::CreateFontIndirectW(&metrics.lfMessageFont);
    }
    return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
}

HICON LoadAppIcon(HINSTANCE instance, bool useSmallIcon) {
    const int width = ::GetSystemMetrics(useSmallIcon ? SM_CXSMICON : SM_CXICON);
    const int height = ::GetSystemMetrics(useSmallIcon ? SM_CYSMICON : SM_CYICON);
    HICON icon = static_cast<HICON>(::LoadImageW(
        instance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR));
    if (icon != nullptr) {
        return icon;
    }
    return ::LoadIconW(nullptr, IDI_APPLICATION);
}

LRESULT CALLBACK StatusBarSubclassProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR) {
    switch (message) {
    case WM_LBUTTONDOWN:
        ::ReleaseCapture();
        ::SendMessageW(::GetParent(window), WM_NCLBUTTONDOWN, HTCAPTION, 0);
        return 0;
    case WM_NCDESTROY:
        ::RemoveWindowSubclass(window, StatusBarSubclassProc, 0);
        break;
    default:
        break;
    }

    return ::DefSubclassProc(window, message, wParam, lParam);
}

struct LoadedDocument {
    std::string utf8Text;
    int eolMode = SC_EOL_CRLF;
    bool hadUtf8Bom = false;
};

struct MatchRange {
    sptr_t start = 0;
    sptr_t end = 0;
};

class FedWindow {
public:
    explicit FedWindow(HINSTANCE instance) : instance_(instance) {}

    int Run(int showCommand) {
        INITCOMMONCONTROLSEX controls = {};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
        ::InitCommonControlsEx(&controls);
        const HRESULT oleResult = ::OleInitialize(nullptr);
        const bool oleInitialized = SUCCEEDED(oleResult);

        editorFontFace_ = SelectEditorFont();
        statusFont_ = CreateUiFont(editorFontFace_, 11, FW_NORMAL);
        searchFont_ = CreateMessageFont();
        darkModeEnabled_ = ResolveDarkModeEnabled();
        const EditorPalette palette = PaletteForDarkMode(darkModeEnabled_);
        frameBrush_ = ::CreateSolidBrush(palette.back);
        statusBrush_ = ::CreateSolidBrush(CurrentStatusBarBackColor());
        UpdateSearchBrushes();
        if (!::Scintilla_RegisterClasses(instance_)) {
            ::MessageBoxW(nullptr, L"Failed to register Scintilla window classes.", kAppName, MB_ICONERROR | MB_OK);
            DestroyUiResources();
            if (oleInitialized) {
                ::OleUninitialize();
            }
            return 1;
        }

        if (!RegisterMainClass() || !CreateMainWindow(showCommand)) {
            ::Scintilla_ReleaseResources();
            DestroyUiResources();
            if (oleInitialized) {
                ::OleUninitialize();
            }
            return 1;
        }

        accelerator_ = ::LoadAcceleratorsW(instance_, MAKEINTRESOURCEW(IDR_ACCELERATORS));

        MSG message = {};
        while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (ProcessModelessMessage(&message)) {
                continue;
            }
            if (accelerator_ == nullptr || !::TranslateAcceleratorW(window_, accelerator_, &message)) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
        }

        ::Scintilla_ReleaseResources();
        if (oleInitialized) {
            ::OleUninitialize();
        }
        return static_cast<int>(message.wParam);
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND editor_ = nullptr;
    HWND statusBar_ = nullptr;
    HACCEL accelerator_ = nullptr;
    HBRUSH statusBrush_ = nullptr;
    HBRUSH frameBrush_ = nullptr;
    HBRUSH searchBrush_ = nullptr;
    HBRUSH searchEditBrush_ = nullptr;
    HFONT statusFont_ = nullptr;
    HFONT searchFont_ = nullptr;
    std::wstring currentPath_;
    std::wstring editorFontFace_;
    HWND findWindow_ = nullptr;
    HWND replaceWindow_ = nullptr;
    std::wstring searchText_;
    std::wstring replaceText_;
    std::vector<MatchRange> searchMatches_;
    int currentMatchIndex_ = -1;
    bool searchMatchCase_ = false;
    bool searchWholeWord_ = false;
    bool searchIndicatorConfigured_ = false;
    bool updatingSearchControls_ = false;
    bool suppressSearchRefresh_ = false;
    bool switchingSearchWindow_ = false;
    bool writeUtf8Bom_ = false;
    StyleMode styleMode_ = StyleMode::System;
    bool darkModeEnabled_ = false;
    bool applyingDarkMode_ = false;
    bool showLineNumbers_ = true;
    bool showStatusBar_ = true;
    bool wordWrapEnabled_ = false;

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        FedWindow *self = nullptr;
        if (message == WM_NCCREATE) {
            auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            self = static_cast<FedWindow *>(create->lpCreateParams);
            self->window_ = window;
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<FedWindow *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (self != nullptr) {
            return self->HandleMessage(message, wParam, lParam);
        }
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    bool RegisterMainClass() {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadAppIcon(instance_, false);
        windowClass.hIconSm = LoadAppIcon(instance_, true);
        windowClass.hbrBackground = frameBrush_;
        windowClass.lpszClassName = kWindowClassName;
        return ::RegisterClassExW(&windowClass) != 0;
    }

    bool RegisterSearchClass() {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = SearchWindowProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hIcon = LoadAppIcon(instance_, false);
        windowClass.hIconSm = LoadAppIcon(instance_, true);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kSearchWindowClassName;

        if (::RegisterClassExW(&windowClass) != 0) {
            return true;
        }
        return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    bool CreateMainWindow(int showCommand) {
        const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        const DWORD exStyle = WS_EX_ACCEPTFILES;
        HWND window = ::CreateWindowExW(
            exStyle,
            kWindowClassName,
            kAppName,
            style,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1080,
            760,
            nullptr,
            ::LoadMenuW(instance_, MAKEINTRESOURCEW(IDR_MAINMENU)),
            instance_,
            this);

        if (window == nullptr) {
            ::MessageBoxW(nullptr, L"Failed to create the main window.", kAppName, MB_ICONERROR | MB_OK);
            return false;
        }

        ::ShowWindow(window, showCommand);
        ::UpdateWindow(window);
        return true;
    }

    void DestroyUiResources() {
        DestroySearchWindow(findWindow_);
        DestroySearchWindow(replaceWindow_);
        if (searchFont_ != nullptr && searchFont_ != ::GetStockObject(DEFAULT_GUI_FONT)) {
            ::DeleteObject(searchFont_);
            searchFont_ = nullptr;
        }
        if (statusFont_ != nullptr) {
            ::DeleteObject(statusFont_);
            statusFont_ = nullptr;
        }
        if (searchEditBrush_ != nullptr) {
            ::DeleteObject(searchEditBrush_);
            searchEditBrush_ = nullptr;
        }
        if (searchBrush_ != nullptr) {
            ::DeleteObject(searchBrush_);
            searchBrush_ = nullptr;
        }
        if (statusBrush_ != nullptr) {
            ::DeleteObject(statusBrush_);
            statusBrush_ = nullptr;
        }
        if (frameBrush_ != nullptr) {
            ::DeleteObject(frameBrush_);
            frameBrush_ = nullptr;
        }
    }

    bool ProcessModelessMessage(MSG *message) {
        if (message->message == WM_KEYDOWN) {
            if (message->wParam == VK_ESCAPE && IsSearchWindow(message->hwnd)) {
                ::DestroyWindow(::GetAncestor(message->hwnd, GA_ROOT));
                return true;
            }
            if (message->wParam == VK_RETURN && IsSearchWindow(message->hwnd)) {
                HWND restoreFocus = message->hwnd;
                SelectSearchMatch(true);
                if (restoreFocus != nullptr && ::IsWindow(restoreFocus)) {
                    ::SetFocus(restoreFocus);
                }
                return true;
            }
            if (message->wParam == VK_F3) {
                const bool previous = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
                SelectSearchMatch(!previous);
                return true;
            }
        }

        if (findWindow_ != nullptr && ::IsWindow(findWindow_) && ::IsDialogMessageW(findWindow_, message)) {
            return true;
        }
        if (replaceWindow_ != nullptr && ::IsWindow(replaceWindow_) && ::IsDialogMessageW(replaceWindow_, message)) {
            return true;
        }
        return false;
    }

    bool IsSearchWindow(HWND window) const {
        return (findWindow_ != nullptr && (::IsChild(findWindow_, window) || findWindow_ == window)) ||
            (replaceWindow_ != nullptr && (::IsChild(replaceWindow_, window) || replaceWindow_ == window));
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            return OnCreate() ? 0 : -1;
        case WM_SETFOCUS:
            RestoreEditorFocus();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE) {
                RestoreEditorFocus();
            }
            return 0;
        case WM_SIZE:
            LayoutChildren();
            return 0;
        case WM_DROPFILES:
            OnDropFiles(reinterpret_cast<HDROP>(wParam));
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam));
            return 0;
        case WM_NOTIFY:
            return OnNotify(reinterpret_cast<const NMHDR *>(lParam));
        case WM_INITMENUPOPUP:
            OnInitMenuPopup(reinterpret_cast<HMENU>(wParam));
            return 0;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == statusBar_) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, kChromeText);
                ::SetBkColor(dc, CurrentStatusBarBackColor());
                ::SetBkMode(dc, OPAQUE);
                return reinterpret_cast<LRESULT>(statusBrush_);
            }
            break;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            RefreshDarkMode();
            return 0;
        case WM_CLOSE:
            if (ConfirmDiscardChanges()) {
                ::DestroyWindow(window_);
            }
            return 0;
        case WM_DESTROY:
            DestroyUiResources();
            ::PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return ::DefWindowProcW(window_, message, wParam, lParam);
    }

    static LRESULT CALLBACK SearchWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        FedWindow *self = nullptr;
        if (message == WM_NCCREATE) {
            auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            self = static_cast<FedWindow *>(create->lpCreateParams);
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<FedWindow *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (self != nullptr) {
            return self->HandleSearchWindowMessage(window, message, wParam, lParam);
        }
        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT HandleSearchWindowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_COMMAND:
            HandleSearchCommand(window, LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_ERASEBKGND: {
            RECT client = {};
            ::GetClientRect(window, &client);
            ::FillRect(reinterpret_cast<HDC>(wParam), &client, searchBrush_);
            return 1;
        }
        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(searchBrush_);
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            ::SetTextColor(dc, CurrentDialogTextColor());
            ::SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(searchBrush_);
        }
        case WM_CTLCOLOREDIT:
            if (darkModeEnabled_) {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, kDarkEditorFore);
                ::SetBkColor(dc, kDarkControlBack);
                return reinterpret_cast<LRESULT>(searchEditBrush_);
            }
            break;
        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            OnSearchWindowDestroyed(window);
            return 0;
        default:
            break;
        }

        return ::DefWindowProcW(window, message, wParam, lParam);
    }

    bool OnCreate() {
        editor_ = ::CreateWindowExW(
            0,
            L"Scintilla",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | WS_VSCROLL | WS_HSCROLL,
            0,
            0,
            100,
            100,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_EDITOR)),
            instance_,
            nullptr);
        if (editor_ == nullptr) {
            ::MessageBoxW(window_, L"Failed to create the editor control.", kAppName, MB_ICONERROR | MB_OK);
            return false;
        }

        statusBar_ = ::CreateWindowExW(
            0,
            WC_STATICW,
            L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOTIFY,
            0,
            0,
            100,
            kStatusHeight,
            window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUSBAR)),
            instance_,
            nullptr);
        if (statusBar_ == nullptr) {
            ::MessageBoxW(window_, L"Failed to create the status bar.", kAppName, MB_ICONERROR | MB_OK);
            return false;
        }
        ::SendMessageW(statusBar_, WM_SETFONT, reinterpret_cast<WPARAM>(statusFont_), TRUE);
        ::SetWindowSubclass(statusBar_, StatusBarSubclassProc, 0, 0);

        ApplyDarkMode();
        ConfigureEditor();
        ResetToNewDocument();
        LayoutChildren();
        ::SetFocus(editor_);
        return true;
    }

    void RestoreEditorFocus() {
        if (editor_ != nullptr && ::GetFocus() != editor_) {
            ::SetFocus(editor_);
        }
    }

    void OnCommand(int commandId) {
        switch (commandId) {
        case ID_FILE_NEW:
            if (ConfirmDiscardChanges()) {
                ResetToNewDocument();
            }
            break;
        case ID_FILE_OPEN:
            if (ConfirmDiscardChanges()) {
                const std::wstring path = PromptForPath(true);
                if (!path.empty()) {
                    OpenDocument(path);
                }
            }
            break;
        case ID_FILE_SAVE:
            SaveDocument(false);
            break;
        case ID_FILE_SAVE_AS:
            SaveDocument(true);
            break;
        case ID_FILE_EXIT:
            ::SendMessageW(window_, WM_CLOSE, 0, 0);
            break;
        case ID_EDIT_UNDO:
            EditorSend(SCI_UNDO);
            break;
        case ID_EDIT_CUT:
            EditorSend(SCI_CUT);
            break;
        case ID_EDIT_COPY:
            EditorSend(SCI_COPY);
            break;
        case ID_EDIT_PASTE:
            EditorSend(SCI_PASTE);
            break;
        case ID_EDIT_DELETE:
            if (EditorSend(SCI_GETSELECTIONEMPTY) == 0) {
                EditorSend(SCI_CLEAR);
            } else {
                const sptr_t currentPosition = EditorSend(SCI_GETCURRENTPOS);
                const sptr_t documentLength = EditorSend(SCI_GETLENGTH);
                if (currentPosition < documentLength) {
                    EditorSend(SCI_DELETERANGE, currentPosition, 1);
                }
            }
            break;
        case ID_EDIT_FIND:
            ShowSearchWindow(SearchDialogMode::Find);
            break;
        case ID_EDIT_REPLACE:
            ShowSearchWindow(SearchDialogMode::Replace);
            break;
        case ID_EDIT_FIND_NEXT:
            SelectSearchMatch(true);
            break;
        case ID_EDIT_FIND_PREVIOUS:
            SelectSearchMatch(false);
            break;
        case ID_EDIT_SELECT_ALL:
            EditorSend(SCI_SELECTALL);
            break;
        case ID_VIEW_LINE_NUMBERS:
            showLineNumbers_ = !showLineNumbers_;
            UpdateLineNumberMargin();
            UpdateMenuChecks();
            break;
        case ID_VIEW_STATUS_BAR:
            showStatusBar_ = !showStatusBar_;
            ::ShowWindow(statusBar_, showStatusBar_ ? SW_SHOW : SW_HIDE);
            LayoutChildren();
            UpdateMenuChecks();
            break;
        case ID_VIEW_WORD_WRAP:
            wordWrapEnabled_ = !wordWrapEnabled_;
            ApplyWordWrap();
            UpdateMenuChecks();
            break;
        case ID_VIEW_STYLE_SYSTEM:
            ApplyStyleMode(StyleMode::System);
            break;
        case ID_VIEW_STYLE_LIGHT:
            ApplyStyleMode(StyleMode::Light);
            break;
        case ID_VIEW_STYLE_DARK:
            ApplyStyleMode(StyleMode::Dark);
            break;
        case ID_VIEW_ZOOM_IN:
            EditorSend(SCI_ZOOMIN);
            break;
        case ID_VIEW_ZOOM_OUT:
            EditorSend(SCI_ZOOMOUT);
            break;
        case ID_VIEW_ZOOM_RESET:
            EditorSend(SCI_SETZOOM, 0);
            break;
        case ID_HELP_ABOUT:
            ShowAboutDialog();
            break;
        default:
            break;
        }
    }

    void OnDropFiles(HDROP dropHandle) {
        if (dropHandle == nullptr) {
            return;
        }

        const UINT fileCount = ::DragQueryFileW(dropHandle, 0xFFFFFFFF, nullptr, 0);
        if (fileCount != 1) {
            ::DragFinish(dropHandle);
            ::MessageBoxW(window_, L"Please drop exactly one file.", kAppName, MB_ICONWARNING | MB_OK);
            return;
        }

        const UINT pathLength = ::DragQueryFileW(dropHandle, 0, nullptr, 0);
        std::vector<wchar_t> pathBuffer(pathLength + 1, L'\0');
        if (::DragQueryFileW(dropHandle, 0, pathBuffer.data(), pathLength + 1) == 0) {
            ::DragFinish(dropHandle);
            ::MessageBoxW(window_, L"Unable to read the dropped file path.", kAppName, MB_ICONERROR | MB_OK);
            return;
        }
        ::DragFinish(dropHandle);

        const std::wstring path(pathBuffer.data());
        ULONGLONG sizeBytes = 0;
        if (!TryGetFileSize(path, &sizeBytes)) {
            std::wstring message = L"Unable to inspect dropped file:\n\n" + path;
            ::MessageBoxW(window_, message.c_str(), kAppName, MB_ICONERROR | MB_OK);
            return;
        }
        if (sizeBytes > kMaxDroppedFileBytes) {
            std::wstring message =
                L"Dropped files larger than 64 MiB are not supported:\n\n" + path;
            ::MessageBoxW(window_, message.c_str(), kAppName, MB_ICONWARNING | MB_OK);
            return;
        }

        if (ConfirmDiscardChanges()) {
            OpenDocument(path);
        }
    }

    LRESULT OnNotify(const NMHDR *header) {
        if (header == nullptr) {
            return 0;
        }

        if (header->hwndFrom != editor_) {
            return 0;
        }

        if (header->code == SCN_SAVEPOINTLEFT || header->code == SCN_SAVEPOINTREACHED) {
            UpdateWindowTitle();
            UpdateStatusBar();
            return 0;
        }

        if (header->code == SCN_UPDATEUI) {
            UpdateStatusBar();
            return 0;
        }

        if (header->code == SCN_ZOOM) {
            UpdateLineNumberMargin();
            UpdateStatusBar();
            return 0;
        }

        if (header->code == SCN_MODIFIED) {
            UpdateLineNumberMargin();
            if (!suppressSearchRefresh_) {
                RefreshSearchMatches();
            }
            UpdateStatusBar();
            return 0;
        }

        return 0;
    }

    void OnInitMenuPopup(HMENU, bool = false) {
        if (editor_ == nullptr) {
            return;
        }

        const bool hasSelection = EditorSend(SCI_GETSELECTIONEMPTY) == 0;
        const bool canUndo = EditorSend(SCI_CANUNDO) != 0;
        const bool canPaste = EditorSend(SCI_CANPASTE) != 0;
        const bool canDelete = hasSelection || EditorSend(SCI_GETCURRENTPOS) < EditorSend(SCI_GETLENGTH);

        auto setState = [&](UINT id, bool enabled) {
            ::EnableMenuItem(::GetMenu(window_), id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
        };

        setState(ID_EDIT_UNDO, canUndo);
        setState(ID_EDIT_CUT, hasSelection);
        setState(ID_EDIT_COPY, hasSelection);
        setState(ID_EDIT_DELETE, canDelete);
        setState(ID_EDIT_PASTE, canPaste);
        setState(ID_EDIT_FIND, true);
        setState(ID_EDIT_REPLACE, true);
        UpdateMenuChecks();
    }

    void ConfigureEditor() {
        EditorSend(SCI_SETCODEPAGE, SC_CP_UTF8);
        EditorSend(SCI_SETUNDOCOLLECTION, 1);
        EditorSend(SCI_SETUSETABS, 1);
        EditorSend(SCI_SETTABWIDTH, 4);
        EditorSend(SCI_SETVSCROLLBAR, 1);
        EditorSend(SCI_SETMARGINS, 2);
        EditorSend(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
        EditorSend(SCI_SETMARGINTYPEN, 1, SC_MARGIN_BACK);
        EditorSend(SCI_SETMARGINMASKN, 0, 0);
        EditorSend(SCI_SETMARGINMASKN, 1, 0);
        EditorSend(SCI_SETMARGINSENSITIVEN, 0, 0);
        EditorSend(SCI_SETMARGINSENSITIVEN, 1, 0);
        EditorSend(SCI_SETSCROLLWIDTHTRACKING, 1);
        EditorSend(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT);
        ConfigureSearchIndicator();
        ApplyEditorAppearance();
        ApplyWordWrap();
        UpdateLineNumberMargin();
    }

    bool ResolveDarkModeEnabled() const {
        switch (styleMode_) {
        case StyleMode::Light:
            return false;
        case StyleMode::Dark:
            return true;
        case StyleMode::System:
        default:
            return IsDarkModeEnabledByOs();
        }
    }

    COLORREF CurrentStatusBarBackColor() const {
        return darkModeEnabled_ ? kChromeBlue : kLightStatusBarBack;
    }

    COLORREF CurrentDialogBackColor() const {
        return darkModeEnabled_ ? kDarkDialogBack : ::GetSysColor(COLOR_WINDOW);
    }

    COLORREF CurrentDialogTextColor() const {
        return darkModeEnabled_ ? kDarkEditorFore : ::GetSysColor(COLOR_WINDOWTEXT);
    }

    void UpdateStatusBrush() {
        HBRUSH nextBrush = ::CreateSolidBrush(CurrentStatusBarBackColor());
        HBRUSH oldBrush = statusBrush_;
        statusBrush_ = nextBrush;
        if (statusBar_ != nullptr) {
            ::InvalidateRect(statusBar_, nullptr, TRUE);
        }
        if (oldBrush != nullptr) {
            ::DeleteObject(oldBrush);
        }
    }

    void UpdateSearchBrushes() {
        HBRUSH nextBrush = ::CreateSolidBrush(CurrentDialogBackColor());
        HBRUSH nextEditBrush = ::CreateSolidBrush(darkModeEnabled_ ? kDarkControlBack : ::GetSysColor(COLOR_WINDOW));
        HBRUSH oldBrush = searchBrush_;
        HBRUSH oldEditBrush = searchEditBrush_;
        searchBrush_ = nextBrush;
        searchEditBrush_ = nextEditBrush;
        if (findWindow_ != nullptr) {
            ::InvalidateRect(findWindow_, nullptr, TRUE);
        }
        if (replaceWindow_ != nullptr) {
            ::InvalidateRect(replaceWindow_, nullptr, TRUE);
        }
        if (oldBrush != nullptr) {
            ::DeleteObject(oldBrush);
        }
        if (oldEditBrush != nullptr) {
            ::DeleteObject(oldEditBrush);
        }
    }

    void ApplySearchWindowTheme(HWND window) {
        if (window == nullptr || !::IsWindow(window)) {
            return;
        }

        const wchar_t *themeClass = darkModeEnabled_ ? kDarkThemeClassName : kLightThemeClassName;
        ApplyImmersiveDarkMode(window, darkModeEnabled_);
        ::SetWindowTheme(window, themeClass, nullptr);
        for (HWND child = ::GetWindow(window, GW_CHILD); child != nullptr; child = ::GetWindow(child, GW_HWNDNEXT)) {
            ::SetWindowTheme(child, themeClass, nullptr);
            ::InvalidateRect(child, nullptr, TRUE);
        }
        ::InvalidateRect(window, nullptr, TRUE);
    }

    void UpdateFrameBrush() {
        const EditorPalette palette = PaletteForDarkMode(darkModeEnabled_);
        HBRUSH nextBrush = ::CreateSolidBrush(palette.back);
        HBRUSH oldBrush = frameBrush_;
        frameBrush_ = nextBrush;
        if (window_ != nullptr) {
            ::SetClassLongPtrW(window_, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(frameBrush_));
            ::InvalidateRect(window_, nullptr, TRUE);
        }
        if (oldBrush != nullptr) {
            ::DeleteObject(oldBrush);
        }
    }

    void ApplyEditorAppearance() {
        if (editor_ == nullptr) {
            return;
        }

        const EditorPalette palette = PaletteForDarkMode(darkModeEnabled_);
        const std::string fontName = WideToUtf8(editorFontFace_);

        EditorSend(SCI_STYLESETFORE, STYLE_DEFAULT, palette.fore);
        EditorSend(SCI_STYLESETBACK, STYLE_DEFAULT, palette.back);
        EditorSend(SCI_STYLESETSIZE, STYLE_DEFAULT, kDefaultEditorFontSize);
        EditorSend(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(fontName.c_str()));
        EditorSend(SCI_STYLECLEARALL);

        EditorSend(SCI_STYLESETFORE, STYLE_LINENUMBER, palette.gutterFore);
        EditorSend(SCI_STYLESETBACK, STYLE_LINENUMBER, palette.gutterBack);
        EditorSend(SCI_SETMARGINBACKN, 1, palette.back);

        EditorSend(SCI_SETSELFORE, 1, palette.fore);
        EditorSend(SCI_SETSELBACK, 1, palette.selectionBack);
        EditorSend(SCI_SETCARETFORE, palette.caretFore);
        EditorSend(SCI_SETCARETLINEVISIBLE, 1);
        EditorSend(SCI_SETCARETLINEBACK, palette.currentLineBack);
        EditorSend(SCI_SETCARETLINEBACKALPHA, SC_ALPHA_NOALPHA);
        EditorSend(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST, palette.back);
        EditorSend(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_BACK, palette.back);
        EditorSend(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED, palette.selectionBack);
        EditorSend(SCI_SETELEMENTCOLOUR, SC_ELEMENT_LIST_SELECTED_BACK, palette.selectionBack);
        EditorSend(SCI_SETELEMENTCOLOUR, SC_ELEMENT_CARET_LINE_BACK, palette.currentLineBack);
        ::InvalidateRect(editor_, nullptr, TRUE);
    }

    void ApplyStyleMode(StyleMode styleMode) {
        styleMode_ = styleMode;
        darkModeEnabled_ = ResolveDarkModeEnabled();
        UpdateStatusBrush();
        UpdateFrameBrush();
        UpdateSearchBrushes();
        ApplyDarkMode();
        ApplyEditorAppearance();
        UpdateStatusBar();
        UpdateMenuChecks();
    }

    void ApplyDarkMode() {
        if (applyingDarkMode_) {
            return;
        }

        applyingDarkMode_ = true;
        ApplyImmersiveDarkMode(window_, darkModeEnabled_);
        ApplyImmersiveDarkMode(editor_, darkModeEnabled_);

        const wchar_t *themeClass = darkModeEnabled_ ? kDarkThemeClassName : kLightThemeClassName;
        if (window_ != nullptr) {
            ::SetWindowTheme(window_, themeClass, nullptr);
            ::DrawMenuBar(window_);
            ::SetWindowPos(
                window_,
                nullptr,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (editor_ != nullptr) {
            ::SetWindowTheme(editor_, themeClass, nullptr);
            ::RedrawWindow(editor_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        }
        if (statusBar_ != nullptr) {
            ::SetWindowTheme(statusBar_, L"", L"");
            ::InvalidateRect(statusBar_, nullptr, TRUE);
        }
        ApplySearchWindowTheme(findWindow_);
        ApplySearchWindowTheme(replaceWindow_);
        applyingDarkMode_ = false;
    }

    void RefreshDarkMode() {
        if (applyingDarkMode_) {
            return;
        }

        const bool nextDarkModeEnabled = ResolveDarkModeEnabled();
        if (nextDarkModeEnabled == darkModeEnabled_ && styleMode_ != StyleMode::System) {
            return;
        }

        darkModeEnabled_ = nextDarkModeEnabled;
        UpdateStatusBrush();
        UpdateFrameBrush();
        UpdateSearchBrushes();
        ApplyDarkMode();
        ApplyEditorAppearance();
        UpdateStatusBar();
        UpdateMenuChecks();
    }

    void ApplyWordWrap() {
        if (editor_ == nullptr) {
            return;
        }

        EditorSend(SCI_SETWRAPMODE, wordWrapEnabled_ ? SC_WRAP_WORD : SC_WRAP_NONE);
        EditorSend(SCI_SETHSCROLLBAR, wordWrapEnabled_ ? 0 : 1);
        ::ShowScrollBar(editor_, SB_HORZ, wordWrapEnabled_ ? FALSE : TRUE);
        EditorSend(SCI_SCROLLCARET);
    }

    void LayoutChildren() {
        if (window_ == nullptr || editor_ == nullptr || statusBar_ == nullptr) {
            return;
        }

        RECT client = {};
        ::GetClientRect(window_, &client);

        int editorBottom = client.bottom;
        if (showStatusBar_) {
            editorBottom -= kStatusHeight;
            ::MoveWindow(statusBar_, 0, editorBottom, client.right, kStatusHeight, TRUE);
            ::ShowWindow(statusBar_, SW_SHOW);
        } else {
            ::ShowWindow(statusBar_, SW_HIDE);
        }

        ::MoveWindow(editor_, 0, 0, client.right, editorBottom, TRUE);
        UpdateStatusBar();
    }

    void FocusEditorAtStart() {
        if (editor_ == nullptr) {
            return;
        }

        EditorSend(SCI_SETSEL, 0, 0);
        EditorSend(SCI_GOTOPOS, 0);
        EditorSend(SCI_SCROLLCARET);
        ::SetFocus(editor_);
    }

    void ResetToNewDocument() {
        currentPath_.clear();
        writeUtf8Bom_ = false;

        EditorSend(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(""));
        EditorSend(SCI_SETEOLMODE, SC_EOL_CRLF);
        EditorSend(SCI_EMPTYUNDOBUFFER);
        EditorSend(SCI_SETSAVEPOINT);
        EditorSend(SCI_GOTOPOS, 0);
        EditorSend(SCI_SCROLLCARET);

        UpdateLineNumberMargin();
        UpdateStatusBar();
        UpdateWindowTitle();
        FocusEditorAtStart();
    }

    bool OpenDocument(const std::wstring &path) {
        LoadedDocument document;
        std::wstring errorMessage;
        if (!LoadDocument(path, &document, &errorMessage)) {
            std::wstring message = L"Unable to open file:\n\n" + path + L"\n\n" + errorMessage;
            ::MessageBoxW(window_, message.c_str(), kAppName, MB_ICONERROR | MB_OK);
            return false;
        }

        EditorSend(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(document.utf8Text.c_str()));
        writeUtf8Bom_ = document.hadUtf8Bom;
        currentPath_ = path;

        EditorSend(SCI_SETEOLMODE, document.eolMode);
        EditorSend(SCI_EMPTYUNDOBUFFER);
        EditorSend(SCI_SETSAVEPOINT);
        EditorSend(SCI_GOTOPOS, 0);
        EditorSend(SCI_SCROLLCARET);

        UpdateLineNumberMargin();
        UpdateStatusBar();
        UpdateWindowTitle();
        ::SetFocus(editor_);
        return true;
    }

    bool SaveDocument(bool forceSaveAs) {
        std::wstring targetPath = currentPath_;
        if (forceSaveAs || targetPath.empty()) {
            targetPath = PromptForPath(false);
            if (targetPath.empty()) {
                return false;
            }
        }

        std::string text;
        if (!ExtractEditorText(&text)) {
            ::MessageBoxW(window_, L"Unable to read editor contents.", kAppName, MB_ICONERROR | MB_OK);
            return false;
        }

        std::string bytes;
        if (writeUtf8Bom_) {
            bytes.assign("\xEF\xBB\xBF", 3);
        }
        bytes.append(text);

        std::wstring errorMessage;
        if (!WriteAllBytes(targetPath, bytes, &errorMessage)) {
            std::wstring message = L"Unable to save file:\n\n" + targetPath + L"\n\n" + errorMessage;
            ::MessageBoxW(window_, message.c_str(), kAppName, MB_ICONERROR | MB_OK);
            return false;
        }

        currentPath_ = targetPath;
        EditorSend(SCI_SETSAVEPOINT);
        UpdateStatusBar();
        UpdateWindowTitle();
        return true;
    }

    bool ConfirmDiscardChanges() {
        if (!DocumentIsDirty()) {
            return true;
        }

        const std::wstring name = currentPath_.empty() ? L"Untitled" : FileNameFromPath(currentPath_);
        const std::wstring message =
            L"Do you want to save changes to " + name + L"?";
        const int result = ::MessageBoxW(
            window_,
            message.c_str(),
            kAppName,
            MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1);

        if (result == IDCANCEL) {
            return false;
        }
        if (result == IDYES) {
            return SaveDocument(false);
        }
        return true;
    }

    void ShowAboutDialog() {
        std::wstring message =
            std::wstring(L"fed ") + FED_VERSION_STRING_W + L"\n\n"
            L"A minimalist Win32 editor built on Scintilla.\n";
        ::MessageBoxW(window_, message.c_str(), L"About fed", MB_OK | MB_ICONINFORMATION);
    }

    std::wstring PromptForPath(bool open) {
        IFileDialog *dialog = nullptr;
        const CLSID dialogClass = open ? CLSID_FileOpenDialog : CLSID_FileSaveDialog;
        HRESULT result = ::CoCreateInstance(
            dialogClass,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog));
        if (FAILED(result)) {
            return {};
        }

        constexpr COMDLG_FILTERSPEC filters[] = {
            {L"Text Files (*.txt)", L"*.txt"},
            {L"All Files (*.*)", L"*.*"},
        };
        dialog->SetFileTypes(ARRAYSIZE(filters), filters);
        dialog->SetFileTypeIndex(1);
        dialog->SetDefaultExtension(L"txt");

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
            options |= open ? FOS_FILEMUSTEXIST : FOS_OVERWRITEPROMPT;
            dialog->SetOptions(options);
        }

        if (!currentPath_.empty()) {
            const std::wstring directory = DirectoryFromPath(currentPath_);
            if (!directory.empty()) {
                IShellItem *folder = nullptr;
                if (SUCCEEDED(::SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }
            dialog->SetFileName(FileNameFromPath(currentPath_).c_str());
        }

        std::wstring path;
        result = dialog->Show(window_);
        if (SUCCEEDED(result)) {
            IShellItem *item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR filePath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &filePath))) {
                    path = filePath;
                    ::CoTaskMemFree(filePath);
                }
                item->Release();
            }
        }

        dialog->Release();
        return path;
    }

    HWND CreateSearchChild(
        HWND parent,
        const wchar_t *className,
        const wchar_t *text,
        DWORD style,
        int id,
        int x,
        int y,
        int width,
        int height) {
        HWND child = ::CreateWindowExW(
            0,
            className,
            text,
            WS_CHILD | WS_VISIBLE | style,
            x,
            y,
            width,
            height,
            parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            instance_,
            nullptr);
        if (child != nullptr) {
            ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(searchFont_), TRUE);
            ::SetWindowTheme(child, darkModeEnabled_ ? kDarkThemeClassName : kLightThemeClassName, nullptr);
        }
        return child;
    }

    void ShowSearchWindow(SearchDialogMode mode) {
        switchingSearchWindow_ = true;
        if (mode == SearchDialogMode::Find) {
            DestroySearchWindow(replaceWindow_);
        } else {
            DestroySearchWindow(findWindow_);
        }
        switchingSearchWindow_ = false;

        PrefillSearchFromSelection();

        HWND &dialogWindow = mode == SearchDialogMode::Find ? findWindow_ : replaceWindow_;
        if (dialogWindow != nullptr && ::IsWindow(dialogWindow)) {
            ::ShowWindow(dialogWindow, SW_SHOWNORMAL);
            ::SetForegroundWindow(dialogWindow);
            HWND searchField = ::GetDlgItem(dialogWindow, kSearchTextId);
            if (searchField != nullptr) {
                ::SetFocus(searchField);
                ::SendMessageW(searchField, EM_SETSEL, 0, -1);
            }
            return;
        }

        if (!RegisterSearchClass()) {
            ::MessageBoxW(window_, L"Failed to register the search window class.", kAppName, MB_ICONERROR | MB_OK);
            return;
        }

        const bool replaceMode = mode == SearchDialogMode::Replace;
        const wchar_t *title = replaceMode ? L"Replace" : L"Find";
        const int height = replaceMode ? kReplaceDialogHeight : kFindDialogHeight;
        dialogWindow = ::CreateWindowExW(
            WS_EX_TOOLWINDOW,
            kSearchWindowClassName,
            title,
            WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            kSearchDialogWidth,
            height,
            window_,
            nullptr,
            instance_,
            this);
        if (dialogWindow == nullptr) {
            ::MessageBoxW(window_, L"Failed to create the search window.", kAppName, MB_ICONERROR | MB_OK);
            return;
        }

        BuildSearchWindow(dialogWindow, replaceMode);
        ApplySearchWindowTheme(dialogWindow);
        ::ShowWindow(dialogWindow, SW_SHOWNORMAL);
        ::UpdateWindow(dialogWindow);
        RefreshSearchMatches();

        HWND searchField = ::GetDlgItem(dialogWindow, kSearchTextId);
        if (searchField != nullptr) {
            ::SetFocus(searchField);
            ::SendMessageW(searchField, EM_SETSEL, 0, -1);
        }
    }

    void BuildSearchWindow(HWND dialogWindow, bool replaceMode) {
        updatingSearchControls_ = true;

        CreateSearchChild(dialogWindow, WC_STATICW, L"Search string:", 0, -1, 16, 18, 105, 22);
        CreateSearchChild(
            dialogWindow,
            WC_EDITW,
            searchText_.c_str(),
            WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
            kSearchTextId,
            126,
            14,
            320,
            24);

        int nextY = 50;
        if (replaceMode) {
            CreateSearchChild(dialogWindow, WC_STATICW, L"Replace with:", 0, -1, 16, 54, 105, 22);
            CreateSearchChild(
                dialogWindow,
                WC_EDITW,
                replaceText_.c_str(),
                WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER,
                kReplaceTextId,
                126,
                50,
                320,
                24);
            nextY = 86;
        }

        HWND matchCase = CreateSearchChild(
            dialogWindow,
            WC_BUTTONW,
            L"Match case",
            WS_TABSTOP | BS_AUTOCHECKBOX,
            kMatchCaseId,
            126,
            nextY,
            135,
            22);
        HWND wholeWord = CreateSearchChild(
            dialogWindow,
            WC_BUTTONW,
            L"Match whole word only",
            WS_TABSTOP | BS_AUTOCHECKBOX,
            kWholeWordId,
            270,
            nextY,
            176,
            22);
        ::SendMessageW(matchCase, BM_SETCHECK, searchMatchCase_ ? BST_CHECKED : BST_UNCHECKED, 0);
        ::SendMessageW(wholeWord, BM_SETCHECK, searchWholeWord_ ? BST_CHECKED : BST_UNCHECKED, 0);

        CreateSearchChild(dialogWindow, WC_STATICW, L"Occ: 0/0", 0, kOccurrenceLabelId, 16, nextY + 38, 128, 22);
        CreateSearchChild(
            dialogWindow,
            WC_BUTTONW,
            L"Find Previous",
            WS_TABSTOP | BS_PUSHBUTTON,
            kFindPreviousId,
            154,
            nextY + 34,
            112,
            30);
        CreateSearchChild(
            dialogWindow,
            WC_BUTTONW,
            L"Find Next",
            WS_TABSTOP | BS_DEFPUSHBUTTON,
            kFindNextId,
            274,
            nextY + 34,
            88,
            30);
        CreateSearchChild(
            dialogWindow,
            WC_BUTTONW,
            L"Close",
            WS_TABSTOP | BS_PUSHBUTTON,
            kCloseSearchId,
            370,
            nextY + 34,
            76,
            30);

        if (replaceMode) {
            CreateSearchChild(
                dialogWindow,
                WC_BUTTONW,
                L"Replace",
                WS_TABSTOP | BS_PUSHBUTTON,
                kReplaceButtonId,
                154,
                nextY + 72,
                88,
                30);
            CreateSearchChild(
                dialogWindow,
                WC_BUTTONW,
                L"Replace All",
                WS_TABSTOP | BS_PUSHBUTTON,
                kReplaceAllButtonId,
                250,
                nextY + 72,
                112,
                30);
        }

        updatingSearchControls_ = false;
        UpdateOccurrenceLabels();
    }

    void HandleSearchCommand(HWND dialogWindow, int controlId, int notificationCode, HWND) {
        if (updatingSearchControls_) {
            return;
        }

        switch (controlId) {
        case kSearchTextId:
        case kReplaceTextId:
            if (notificationCode == EN_CHANGE) {
                SyncSearchStateFromWindow(dialogWindow);
                RefreshSearchMatches();
            }
            break;
        case kMatchCaseId:
        case kWholeWordId:
            if (notificationCode == BN_CLICKED) {
                SyncSearchStateFromWindow(dialogWindow);
                RefreshSearchMatches();
            }
            break;
        case kFindNextId:
            SyncSearchStateFromWindow(dialogWindow);
            SelectSearchMatch(true);
            break;
        case kFindPreviousId:
            SyncSearchStateFromWindow(dialogWindow);
            SelectSearchMatch(false);
            break;
        case kReplaceButtonId:
            SyncSearchStateFromWindow(dialogWindow);
            ReplaceCurrentMatch();
            break;
        case kReplaceAllButtonId:
            SyncSearchStateFromWindow(dialogWindow);
            ReplaceAllMatches();
            break;
        case kCloseSearchId:
            ::DestroyWindow(dialogWindow);
            break;
        default:
            break;
        }
    }

    void SyncSearchStateFromWindow(HWND dialogWindow) {
        searchText_ = GetControlText(::GetDlgItem(dialogWindow, kSearchTextId));
        if (::GetDlgItem(dialogWindow, kReplaceTextId) != nullptr) {
            replaceText_ = GetControlText(::GetDlgItem(dialogWindow, kReplaceTextId));
        }
        searchMatchCase_ = ::SendMessageW(::GetDlgItem(dialogWindow, kMatchCaseId), BM_GETCHECK, 0, 0) == BST_CHECKED;
        searchWholeWord_ = ::SendMessageW(::GetDlgItem(dialogWindow, kWholeWordId), BM_GETCHECK, 0, 0) == BST_CHECKED;
    }

    std::wstring GetControlText(HWND control) const {
        if (control == nullptr) {
            return {};
        }

        const int length = ::GetWindowTextLengthW(control);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        if (length > 0) {
            ::GetWindowTextW(control, text.data(), length + 1);
        }
        text.resize(static_cast<size_t>(length));
        return text;
    }

    void PrefillSearchFromSelection() {
        if (editor_ == nullptr || EditorSend(SCI_GETSELECTIONEMPTY) != 0) {
            return;
        }

        const sptr_t start = EditorSend(SCI_GETSELECTIONSTART);
        const sptr_t end = EditorSend(SCI_GETSELECTIONEND);
        if (end <= start) {
            return;
        }

        std::string selected(static_cast<size_t>(end - start) + 1, '\0');
        Sci_TextRangeFull range = {};
        range.chrg.cpMin = start;
        range.chrg.cpMax = end;
        range.lpstrText = selected.data();
        EditorSend(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range));
        selected.resize(static_cast<size_t>(end - start));
        if (selected.find_first_of("\r\n") != std::string::npos) {
            return;
        }

        searchText_ = Utf8ToWide(selected);
        UpdateSearchFields();
    }

    int SearchFlags() const {
        int flags = SCFIND_NONE;
        if (searchMatchCase_) {
            flags |= SCFIND_MATCHCASE;
        }
        if (searchWholeWord_) {
            flags |= SCFIND_WHOLEWORD;
        }
        return flags;
    }

    void ConfigureSearchIndicator() {
        if (editor_ == nullptr || searchIndicatorConfigured_) {
            return;
        }

        EditorSend(SCI_INDICSETSTYLE, kSearchIndicator, INDIC_ROUNDBOX);
        EditorSend(SCI_INDICSETFORE, kSearchIndicator, kSearchHighlight);
        EditorSend(SCI_INDICSETALPHA, kSearchIndicator, 80);
        EditorSend(SCI_INDICSETOUTLINEALPHA, kSearchIndicator, 140);
        searchIndicatorConfigured_ = true;
    }

    void ClearSearchHighlights() {
        if (editor_ == nullptr) {
            return;
        }

        EditorSend(SCI_SETINDICATORCURRENT, kSearchIndicator);
        EditorSend(SCI_INDICATORCLEARRANGE, 0, EditorSend(SCI_GETLENGTH));
    }

    void RefreshSearchMatches() {
        if (editor_ == nullptr) {
            return;
        }

        ConfigureSearchIndicator();
        ClearSearchHighlights();
        searchMatches_.clear();
        currentMatchIndex_ = -1;

        const std::string searchUtf8 = WideToUtf8(searchText_);
        if (!searchUtf8.empty()) {
            const sptr_t documentLength = EditorSend(SCI_GETLENGTH);
            sptr_t searchStart = 0;
            while (searchStart < documentLength) {
                Sci_TextToFindFull find = {};
                find.chrg.cpMin = searchStart;
                find.chrg.cpMax = documentLength;
                find.lpstrText = searchUtf8.c_str();
                const sptr_t found = EditorSend(SCI_FINDTEXTFULL, SearchFlags(), reinterpret_cast<sptr_t>(&find));
                if (found < 0 || find.chrgText.cpMax <= find.chrgText.cpMin) {
                    break;
                }

                searchMatches_.push_back({find.chrgText.cpMin, find.chrgText.cpMax});
                EditorSend(SCI_SETINDICATORCURRENT, kSearchIndicator);
                EditorSend(SCI_INDICATORFILLRANGE, find.chrgText.cpMin, find.chrgText.cpMax - find.chrgText.cpMin);
                searchStart = find.chrgText.cpMax;
            }

            currentMatchIndex_ = MatchIndexForSelection();
        }

        UpdateOccurrenceLabels();
    }

    int MatchIndexForSelection() const {
        if (editor_ == nullptr) {
            return -1;
        }

        const sptr_t start = EditorSend(SCI_GETSELECTIONSTART);
        const sptr_t end = EditorSend(SCI_GETSELECTIONEND);
        for (size_t index = 0; index < searchMatches_.size(); ++index) {
            if (searchMatches_[index].start == start && searchMatches_[index].end == end) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }

    int NextMatchIndexFromPosition(sptr_t position) const {
        if (searchMatches_.empty()) {
            return -1;
        }
        for (size_t index = 0; index < searchMatches_.size(); ++index) {
            if (searchMatches_[index].start >= position) {
                return static_cast<int>(index);
            }
        }
        return 0;
    }

    int PreviousMatchIndexFromPosition(sptr_t position) const {
        if (searchMatches_.empty()) {
            return -1;
        }
        for (size_t index = searchMatches_.size(); index > 0; --index) {
            if (searchMatches_[index - 1].start < position) {
                return static_cast<int>(index - 1);
            }
        }
        return static_cast<int>(searchMatches_.size() - 1);
    }

    void SelectSearchMatch(bool forward) {
        RefreshSearchMatches();
        if (searchMatches_.empty()) {
            return;
        }

        const sptr_t position = forward ? EditorSend(SCI_GETSELECTIONEND) : EditorSend(SCI_GETSELECTIONSTART);
        const int selectionIndex = MatchIndexForSelection();
        int nextIndex = -1;
        if (forward) {
            nextIndex = selectionIndex >= 0 ?
                (selectionIndex + 1) % static_cast<int>(searchMatches_.size()) :
                NextMatchIndexFromPosition(position);
        } else {
            nextIndex = selectionIndex >= 0 ?
                (selectionIndex + static_cast<int>(searchMatches_.size()) - 1) % static_cast<int>(searchMatches_.size()) :
                PreviousMatchIndexFromPosition(position);
        }

        SelectSearchMatchByIndex(nextIndex);
    }

    void SelectSearchMatchByIndex(int index) {
        if (index < 0 || index >= static_cast<int>(searchMatches_.size())) {
            currentMatchIndex_ = -1;
            UpdateOccurrenceLabels();
            return;
        }

        const MatchRange &match = searchMatches_[static_cast<size_t>(index)];
        EditorSend(SCI_SETSEL, match.start, match.end);
        EditorSend(SCI_SCROLLCARET);
        currentMatchIndex_ = index;
        UpdateOccurrenceLabels();
        ::SetFocus(editor_);
    }

    void SelectSearchMatchFromPosition(sptr_t position) {
        RefreshSearchMatches();
        SelectSearchMatchByIndex(NextMatchIndexFromPosition(position));
    }

    void ReplaceCurrentMatch() {
        RefreshSearchMatches();
        const int matchIndex = MatchIndexForSelection();
        if (matchIndex < 0) {
            SelectSearchMatch(true);
            return;
        }

        const MatchRange match = searchMatches_[static_cast<size_t>(matchIndex)];
        const std::string replacement = WideToUtf8(replaceText_);
        EditorSend(SCI_TARGETFROMSELECTION);
        const sptr_t replacementLength = EditorSend(
            SCI_REPLACETARGET,
            static_cast<uptr_t>(replacement.size()),
            reinterpret_cast<sptr_t>(replacement.c_str()));
        SelectSearchMatchFromPosition(match.start + std::max<sptr_t>(replacementLength, 0));
    }

    void ReplaceAllMatches() {
        const std::string searchUtf8 = WideToUtf8(searchText_);
        if (editor_ == nullptr || searchUtf8.empty()) {
            RefreshSearchMatches();
            return;
        }

        const std::string replacement = WideToUtf8(replaceText_);
        sptr_t searchStart = 0;
        sptr_t finalPosition = EditorSend(SCI_GETCURRENTPOS);
        suppressSearchRefresh_ = true;
        EditorSend(SCI_BEGINUNDOACTION);
        while (searchStart <= EditorSend(SCI_GETLENGTH)) {
            Sci_TextToFindFull find = {};
            find.chrg.cpMin = searchStart;
            find.chrg.cpMax = EditorSend(SCI_GETLENGTH);
            find.lpstrText = searchUtf8.c_str();
            const sptr_t found = EditorSend(SCI_FINDTEXTFULL, SearchFlags(), reinterpret_cast<sptr_t>(&find));
            if (found < 0 || find.chrgText.cpMax <= find.chrgText.cpMin) {
                break;
            }

            EditorSend(SCI_SETTARGETRANGE, find.chrgText.cpMin, find.chrgText.cpMax);
            const sptr_t replacementLength = EditorSend(
                SCI_REPLACETARGET,
                static_cast<uptr_t>(replacement.size()),
                reinterpret_cast<sptr_t>(replacement.c_str()));
            finalPosition = find.chrgText.cpMin + std::max<sptr_t>(replacementLength, 0);
            searchStart = finalPosition;
        }
        EditorSend(SCI_ENDUNDOACTION);
        suppressSearchRefresh_ = false;

        EditorSend(SCI_SETSEL, finalPosition, finalPosition);
        RefreshSearchMatches();
        UpdateStatusBar();
        ::SetFocus(editor_);
    }

    void UpdateSearchFields() {
        updatingSearchControls_ = true;
        if (findWindow_ != nullptr && ::IsWindow(findWindow_)) {
            ::SetWindowTextW(::GetDlgItem(findWindow_, kSearchTextId), searchText_.c_str());
        }
        if (replaceWindow_ != nullptr && ::IsWindow(replaceWindow_)) {
            ::SetWindowTextW(::GetDlgItem(replaceWindow_, kSearchTextId), searchText_.c_str());
        }
        updatingSearchControls_ = false;
    }

    void UpdateOccurrenceLabels() {
        wchar_t buffer[64] = {};
        const int total = static_cast<int>(searchMatches_.size());
        const int current = currentMatchIndex_ >= 0 ? currentMatchIndex_ + 1 : 0;
        swprintf_s(buffer, L"Occ: %d/%d", current, total);

        if (findWindow_ != nullptr && ::IsWindow(findWindow_)) {
            ::SetWindowTextW(::GetDlgItem(findWindow_, kOccurrenceLabelId), buffer);
        }
        if (replaceWindow_ != nullptr && ::IsWindow(replaceWindow_)) {
            ::SetWindowTextW(::GetDlgItem(replaceWindow_, kOccurrenceLabelId), buffer);
        }
    }

    void DestroySearchWindow(HWND window) {
        if (window != nullptr && ::IsWindow(window)) {
            ::DestroyWindow(window);
        }
    }

    void OnSearchWindowDestroyed(HWND window) {
        if (window == findWindow_) {
            findWindow_ = nullptr;
        }
        if (window == replaceWindow_) {
            replaceWindow_ = nullptr;
            replaceText_.clear();
        }
        if (!switchingSearchWindow_ && findWindow_ == nullptr && replaceWindow_ == nullptr) {
            searchText_.clear();
            searchMatches_.clear();
            currentMatchIndex_ = -1;
            ClearSearchHighlights();
        }
    }

    bool LoadDocument(const std::wstring &path, LoadedDocument *document, std::wstring *errorMessage) {
        std::string bytes;
        if (!ReadAllBytes(path, &bytes, errorMessage)) {
            return false;
        }

        std::string_view text(bytes);
        document->hadUtf8Bom = text.size() >= 3 &&
            static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB &&
            static_cast<unsigned char>(text[2]) == 0xBF;
        if (document->hadUtf8Bom) {
            text.remove_prefix(3);
        }

        document->eolMode = DetectEolMode(text);

        std::wstring wide = BytesToWide(CP_UTF8, MB_ERR_INVALID_CHARS, text);
        if (wide.empty() && !text.empty()) {
            wide = BytesToWide(CP_ACP, 0, text);
        }
        if (wide.empty() && !text.empty()) {
            *errorMessage = L"The file could not be decoded as text.";
            return false;
        }

        document->utf8Text = text.empty() ? std::string() : WideToUtf8(wide);
        return true;
    }

    bool ExtractEditorText(std::string *text) const {
        const sptr_t length = EditorSend(SCI_GETLENGTH);
        text->assign(static_cast<size_t>(length) + 1, '\0');
        if (length > 0) {
            EditorSend(SCI_GETTEXT, length + 1, reinterpret_cast<sptr_t>(text->data()));
        } else {
            (*text)[0] = '\0';
        }
        text->resize(static_cast<size_t>(length));
        return true;
    }

    int CurrentEditorPointSize() const {
        const int zoomLevel = editor_ != nullptr ? static_cast<int>(EditorSend(SCI_GETZOOM)) : 0;
        return std::max(1, kDefaultEditorFontSize + zoomLevel);
    }

    void UpdateLineNumberMargin() {
        if (editor_ == nullptr) {
            return;
        }

        if (!showLineNumbers_) {
            EditorSend(SCI_SETMARGINWIDTHN, 0, 0);
            EditorSend(SCI_SETMARGINWIDTHN, 1, 0);
            return;
        }

        const sptr_t lineCount = std::max<sptr_t>(1, EditorSend(SCI_GETLINECOUNT));
        int digits = 1;
        for (sptr_t value = lineCount; value >= 10; value /= 10) {
            ++digits;
        }
        digits = std::max(digits, kMinLineNumberDigits);

        std::string sample(static_cast<size_t>(digits), '9');
        const sptr_t width = EditorSend(
            SCI_TEXTWIDTH,
            STYLE_LINENUMBER,
            reinterpret_cast<sptr_t>(sample.c_str()));
        EditorSend(SCI_SETMARGINWIDTHN, 0, width);
        EditorSend(SCI_SETMARGINWIDTHN, 1, kLineNumberSpacerWidth);
    }

    void UpdateStatusBar() {
        if (statusBar_ == nullptr) {
            return;
        }

        const sptr_t position = EditorSend(SCI_GETCURRENTPOS);
        const sptr_t line = EditorSend(SCI_LINEFROMPOSITION, position);
        const sptr_t column = EditorSend(SCI_GETCOLUMN, position);
        const sptr_t lineCount = std::max<sptr_t>(1, EditorSend(SCI_GETLINECOUNT));
        const wchar_t *eol = L"CRLF";
        switch (EditorSend(SCI_GETEOLMODE)) {
        case SC_EOL_LF:
            eol = L"LF";
            break;
        case SC_EOL_CR:
            eol = L"CR";
            break;
        default:
            break;
        }

        const wchar_t *encoding = writeUtf8Bom_ ? L"UTF-8 BOM" : L"UTF-8";
        wchar_t buffer[256] = {};
        swprintf_s(
            buffer,
            L"  Ln %lld/%lld   Col %lld   %s   %s   %s %d",
            static_cast<long long>(line + 1),
            static_cast<long long>(lineCount),
            static_cast<long long>(column + 1),
            eol,
            encoding,
            editorFontFace_.c_str(),
            CurrentEditorPointSize());
        ::SetWindowTextW(statusBar_, buffer);
    }

    void UpdateWindowTitle() {
        const std::wstring name = currentPath_.empty() ? L"Untitled" : FileNameFromPath(currentPath_);
        const wchar_t *dirty = DocumentIsDirty() ? L"*" : L"";
        const std::wstring title = std::wstring(dirty) + name + L" - " + kAppName;
        ::SetWindowTextW(window_, title.c_str());
    }

    void UpdateMenuChecks() {
        HMENU menu = ::GetMenu(window_);
        if (menu == nullptr) {
            return;
        }

        ::CheckMenuItem(menu, ID_VIEW_LINE_NUMBERS, MF_BYCOMMAND | (showLineNumbers_ ? MF_CHECKED : MF_UNCHECKED));
        ::CheckMenuItem(menu, ID_VIEW_STATUS_BAR, MF_BYCOMMAND | (showStatusBar_ ? MF_CHECKED : MF_UNCHECKED));
        ::CheckMenuItem(menu, ID_VIEW_WORD_WRAP, MF_BYCOMMAND | (wordWrapEnabled_ ? MF_CHECKED : MF_UNCHECKED));
        UINT styleCommandId = ID_VIEW_STYLE_SYSTEM;
        switch (styleMode_) {
        case StyleMode::Light:
            styleCommandId = ID_VIEW_STYLE_LIGHT;
            break;
        case StyleMode::Dark:
            styleCommandId = ID_VIEW_STYLE_DARK;
            break;
        case StyleMode::System:
        default:
            break;
        }
        ::CheckMenuRadioItem(
            menu,
            ID_VIEW_STYLE_SYSTEM,
            ID_VIEW_STYLE_DARK,
            styleCommandId,
            MF_BYCOMMAND);
    }

    bool DocumentIsDirty() const {
        return editor_ != nullptr && EditorSend(SCI_GETMODIFY) != 0;
    }

    sptr_t EditorSend(UINT message, uptr_t wParam = 0, sptr_t lParam = 0) const {
        return ::SendMessageW(editor_, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
    }
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    FedWindow app(instance);
    return app.Run(showCommand);
}
