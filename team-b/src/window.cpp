#include "sr3render/window.h"

#include <windows.h>

namespace sr3render {

namespace {

constexpr const wchar_t* kClassName = L"SR3RenderWindow";

struct WindowState {
    uint32_t width = 0;
    uint32_t height = 0;
    bool shouldClose = false;
};

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    WindowState* state =
        reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            if (state != nullptr) state->shouldClose = true;
            if (msg == WM_DESTROY) PostQuitMessage(0);
            return 0;
        case WM_KEYDOWN:
            // Escape quits. The only input this window handles on purpose.
            if (wparam == VK_ESCAPE && state != nullptr) state->shouldClose = true;
            return 0;
        case WM_SIZE:
            if (state != nullptr) {
                state->width = static_cast<uint32_t>(LOWORD(lparam));
                state->height = static_cast<uint32_t>(HIWORD(lparam));
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

std::string lastErrorString(const char* what) {
    DWORD code = GetLastError();
    char buf[128];
    snprintf(buf, sizeof(buf), "%s failed (GetLastError=%lu)", what,
             static_cast<unsigned long>(code));
    return buf;
}

} // namespace

Window::~Window() {
    if (hwnd_ != nullptr) {
        HWND hwnd = static_cast<HWND>(hwnd_);
        WindowState* state =
            reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        DestroyWindow(hwnd);
        hwnd_ = nullptr;
    }
}

bool Window::create(const std::string& title, uint32_t width, uint32_t height,
                    std::string& error) {
    if (width == 0 || height == 0) {
        error = "window dimensions must be non-zero";
        return false;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    // MAKEINTRESOURCEW explicitly: the bare IDC_ARROW macro expands to the
    // ANSI form unless UNICODE is defined, which does not match LoadCursorW.
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
    wc.lpszClassName = kClassName;

    // Re-registering the same class in one process is not an error; only a
    // genuinely different failure matters.
    if (RegisterClassExW(&wc) == 0) {
        DWORD code = GetLastError();
        if (code != ERROR_CLASS_ALREADY_EXISTS) {
            error = lastErrorString("RegisterClassExW");
            return false;
        }
    }

    // Size the window so the CLIENT area is exactly what was asked for -
    // otherwise the swap chain and the visible area disagree by the border
    // and title-bar size, and everything renders subtly scaled.
    RECT rect{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    const DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rect, style, FALSE);

    std::wstring wideTitle(title.begin(), title.end());
    HWND hwnd = CreateWindowExW(0, kClassName, wideTitle.c_str(), style, CW_USEDEFAULT,
                                CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) {
        error = lastErrorString("CreateWindowExW") +
                " - note this is expected in a non-interactive or session-0 context, where "
                "there is no desktop to put a window on; use the headless render path instead";
        return false;
    }

    WindowState* state = new WindowState{width, height, false};
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    hwnd_ = hwnd;
    width_ = width;
    height_ = height;
    shouldClose_ = false;
    return true;
}

bool Window::pumpMessages() {
    if (hwnd_ == nullptr) return false;
    HWND hwnd = static_cast<HWND>(hwnd_);

    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            shouldClose_ = true;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    WindowState* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (state != nullptr) {
        width_ = state->width;
        height_ = state->height;
        if (state->shouldClose) shouldClose_ = true;
    }
    return !shouldClose_;
}

} // namespace sr3render
