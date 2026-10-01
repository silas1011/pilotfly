#include "capture/GdiFrameSource.h"

#include <cstddef>
#include <utility>

#include <windows.h>

namespace pilotfly {

namespace {

std::wstring toWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = static_cast<int>(text.size());
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    if (length <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), length);
    return wide;
}

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = static_cast<int>(text.size());
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, narrow.data(), length, nullptr, nullptr);
    return narrow;
}

void lowerInPlace(std::wstring& text) {
    if (!text.empty()) {
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    }
}

std::wstring windowTitle(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(window, title.data(), length + 1);
    if (copied <= 0) {
        return {};
    }
    title.resize(static_cast<std::size_t>(copied));
    return title;
}

struct WindowSearch {
    std::wstring needle;
    DWORD ownProcess = 0;
    HWND found = nullptr;
};

BOOL CALLBACK matchWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    if (!IsWindowVisible(window)) {
        return TRUE;
    }
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == search->ownProcess) {
        return TRUE;
    }
    std::wstring title = windowTitle(window);
    if (title.empty()) {
        return TRUE;
    }
    lowerInPlace(title);
    if (title.find(search->needle) == std::wstring::npos) {
        return TRUE;
    }
    search->found = window;
    return FALSE;
}

class ScreenDc {
public:
    ScreenDc() : dc_(GetDC(nullptr)) {}
    ~ScreenDc() {
        if (dc_) {
            ReleaseDC(nullptr, dc_);
        }
    }
    ScreenDc(const ScreenDc&) = delete;
    ScreenDc& operator=(const ScreenDc&) = delete;
    HDC get() const { return dc_; }

private:
    HDC dc_;
};

class MemoryDc {
public:
    explicit MemoryDc(HDC compatibleWith) : dc_(CreateCompatibleDC(compatibleWith)) {}
    ~MemoryDc() {
        if (dc_) {
            DeleteDC(dc_);
        }
    }
    MemoryDc(const MemoryDc&) = delete;
    MemoryDc& operator=(const MemoryDc&) = delete;
    HDC get() const { return dc_; }

private:
    HDC dc_;
};

class Bitmap {
public:
    Bitmap(HDC compatibleWith, int width, int height)
        : bitmap_(CreateCompatibleBitmap(compatibleWith, width, height)) {}
    ~Bitmap() {
        if (bitmap_) {
            DeleteObject(bitmap_);
        }
    }
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    HBITMAP get() const { return bitmap_; }

private:
    HBITMAP bitmap_;
};

class SelectedBitmap {
public:
    SelectedBitmap(HDC dc, HBITMAP bitmap) : dc_(dc), previous_(SelectObject(dc, bitmap)) {}
    ~SelectedBitmap() {
        if (ok()) {
            SelectObject(dc_, previous_);
        }
    }
    SelectedBitmap(const SelectedBitmap&) = delete;
    SelectedBitmap& operator=(const SelectedBitmap&) = delete;
    bool ok() const { return previous_ != nullptr && previous_ != HGDI_ERROR; }

private:
    HDC dc_;
    HGDIOBJ previous_;
};

}

GdiFrameSource::GdiFrameSource(std::string windowTitle) : windowTitle_(std::move(windowTitle)) {}

std::string GdiFrameSource::name() const {
    return "Screen copy of '" + windowTitle_ + "'";
}

bool GdiFrameSource::findWindow() {
    window_ = nullptr;
    WindowSearch search;
    search.needle = toWide(windowTitle_);
    lowerInPlace(search.needle);
    if (search.needle.empty()) {
        return false;
    }
    search.ownProcess = GetCurrentProcessId();
    EnumWindows(matchWindow, reinterpret_cast<LPARAM>(&search));
    window_ = search.found;
    return window_ != nullptr;
}

FrameStatus GdiFrameSource::open() {
    if (!findWindow()) {
        return {false, "Uncrashed window not found. Start the game in windowed or borderless mode, then click 'Check again'."};
    }
    const HWND window = static_cast<HWND>(window_);
    if (IsIconic(window)) {
        return {false, "The Uncrashed window is minimized. Bring it back, then click 'Check again'."};
    }
    return {true, "copying the picture of '" + toUtf8(windowTitle(window)) +
                      "' from the screen (keep it visible, exclusive fullscreen cannot be captured)"};
}

void GdiFrameSource::close() {
    window_ = nullptr;
}

bool GdiFrameSource::grab(std::vector<float>& gray, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if ((window_ == nullptr || !IsWindow(static_cast<HWND>(window_))) && !findWindow()) {
        return false;
    }
    const HWND window = static_cast<HWND>(window_);
    if (IsIconic(window)) {
        return false;
    }

    RECT client{};
    if (!GetClientRect(window, &client)) {
        return false;
    }
    POINT origin{0, 0};
    if (!ClientToScreen(window, &origin)) {
        return false;
    }
    const int sourceWidth = client.right - client.left;
    const int sourceHeight = client.bottom - client.top;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }

    const ScreenDc screen;
    if (!screen.get()) {
        return false;
    }
    const MemoryDc memory(screen.get());
    if (!memory.get()) {
        return false;
    }
    const Bitmap bitmap(screen.get(), width, height);
    if (!bitmap.get()) {
        return false;
    }

    {
        const SelectedBitmap selection(memory.get(), bitmap.get());
        if (!selection.ok()) {
            return false;
        }
        SetStretchBltMode(memory.get(), HALFTONE);
        SetBrushOrgEx(memory.get(), 0, 0, nullptr);
        if (!StretchBlt(memory.get(), 0, 0, width, height,
                        screen.get(), origin.x, origin.y, sourceWidth, sourceHeight, SRCCOPY)) {
            return false;
        }
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    pixels_.resize(pixelCount * 4);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const int lines = GetDIBits(screen.get(), bitmap.get(), 0, static_cast<UINT>(height),
                                pixels_.data(), &info, DIB_RGB_COLORS);
    if (lines != height) {
        return false;
    }

    gray.resize(pixelCount);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const float blue = static_cast<float>(pixels_[i * 4]);
        const float green = static_cast<float>(pixels_[i * 4 + 1]);
        const float red = static_cast<float>(pixels_[i * 4 + 2]);
        gray[i] = (0.299f * red + 0.587f * green + 0.114f * blue) / 255.0f;
    }
    return true;
}

}
