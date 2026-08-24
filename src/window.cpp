#include "window.hpp"
#include <windows.h>
#include <cstring>
#include <string>

static Input* g_input = nullptr;
static Window* g_window = nullptr;

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
            PostQuitMessage(0);
            return 0;
        case WM_MOUSEWHEEL: {
            if (g_input) {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                if (delta > 0) { g_input->wheelUp = true; g_input->scrollAccum += 1.0f; }
                else { g_input->wheelDown = true; g_input->scrollAccum -= 1.0f; }
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (g_input && wParam < 256) {
                // lParam bit 30 is set on auto-repeat. Suppress repeats so text
                // entry advances one character per physical key press.
                bool autorepeat = (lParam & (1L << 30)) != 0;
                if (!autorepeat) g_input->pressed[wParam] = true;
                g_input->keys[wParam] = true;
            }
            return 0;
        case WM_KEYUP:
            if (g_input && wParam < 256) g_input->keys[wParam] = false;
            return 0;
        case WM_LBUTTONDOWN: if (g_input) g_input->mouse[0] = true; return 0;
        case WM_LBUTTONUP:   if (g_input) g_input->mouse[0] = false; return 0;
        case WM_RBUTTONDOWN: if (g_input) g_input->mouse[1] = true; return 0;
        case WM_RBUTTONUP:   if (g_input) g_input->mouse[1] = false; return 0;
        case WM_MBUTTONDOWN: if (g_input) g_input->mouse[2] = true; return 0;
        case WM_MBUTTONUP:   if (g_input) g_input->mouse[2] = false; return 0;
        default: break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool Window::init(int w, int h, const char* title) {
    w_ = w;
    h_ = h;
    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSW wc;
    std::memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorA(nullptr, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = L"VoxMineWnd";
    if (!RegisterClassW(&wc)) {
        // already registered
    }
    g_input = &in_;
    g_window = this;

    RECT rc = {0, 0, w, h};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    // Convert UTF-8 title to wide for proper Unicode display
    int wlen = MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0);
    std::wstring wtitle(wlen > 0 ? wlen - 1 : 0, 0);
    if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, title, -1, &wtitle[0], wlen);
    HWND hwnd = CreateWindowExW(0, L"VoxMineWnd", wtitle.c_str(), WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, nullptr, inst, nullptr);
    if (!hwnd) return false;
    hwnd_ = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    return true;
}

void Window::shutdown() {
    if (hwnd_) DestroyWindow((HWND)hwnd_);
    hwnd_ = nullptr;
    g_input = nullptr;
    g_window = nullptr;
}

bool Window::pump() {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return true;
}

void Window::endFrame() { in_.clear(); }

void Window::pollMouse(float& dx, float& dy) {
    dx = 0.0f;
    dy = 0.0f;
    if (!capture_ || !hwnd_) return;
    RECT rc;
    GetClientRect((HWND)hwnd_, &rc);
    float cx = (rc.right - rc.left) * 0.5f;
    float cy = (rc.bottom - rc.top) * 0.5f;
    POINT p;
    GetCursorPos(&p);
    ScreenToClient((HWND)hwnd_, &p);
    if (haveMouse_) {
        dx = p.x - cx;
        dy = p.y - cy;
    }
    haveMouse_ = true;
    POINT c;
    c.x = (LONG)(cx + rc.left);
    c.y = (LONG)(cy + rc.top);
    ClientToScreen((HWND)hwnd_, &c);
    SetCursorPos(c.x, c.y);
}

void Window::cursorPos(float& x, float& y) {
    x = 0;
    y = 0;
    if (!hwnd_) return;
    POINT p;
    GetCursorPos(&p);
    ScreenToClient((HWND)hwnd_, &p);
    x = (float)p.x;
    y = (float)p.y;
}
