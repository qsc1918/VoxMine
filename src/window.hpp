#pragma once
#include <cstdint>

// Per-frame input snapshot. Key indices are virtual-key codes (see window.cpp).
struct Input {
    bool keys[256] = {false};    // held (level) state, set by WM_KEYDOWN/UP
    bool pressed[256] = {false}; // rising edge: true only on the frame a key went down
    bool mouse[3] = {false};     // 0 lmb, 1 rmb, 2 mmb
    bool wheelUp = false;
    bool wheelDown = false;
    float scrollAccum = 0.0f;
    void clear() {
        wheelUp = wheelDown = false;
        scrollAccum = 0.0f;
        for (int i = 0; i < 256; i++) pressed[i] = false;
    }
};

class Window {
public:
    bool init(int w, int h, const char* title);
    void shutdown();

    // Returns false if the window requested to close.
    bool pump();

    // Call once per frame to clear transient input state.
    void endFrame();

    void* hwnd() const { return hwnd_; }
    int width() const { return w_; }
    int height() const { return h_; }

    Input& input() { return in_; }

    void setCapture(bool on) { capture_ = on; }
    bool captured() const { return capture_; }
    // Returns mouse dx,dy since last call (only meaningful while captured).
    void pollMouse(float& dx, float& dy);
    // Current cursor position in client coordinates (top-left origin).
    void cursorPos(float& x, float& y);

private:
    void* hwnd_ = nullptr;
    int w_ = 0, h_ = 0;
    Input in_;
    bool capture_ = false;
    float lastMX_ = 0, lastMY_ = 0;
    bool haveMouse_ = false;
    int winID_ = 0;
};
