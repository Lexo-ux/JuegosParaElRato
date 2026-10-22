// SPDX-License-Identifier: GPL-3.0-or-later
// Borderless Win32 window rendered with Dear ImGui on Direct3D 11.
// Used by the panel (inside the game process, on its own thread) and by the launcher.
#pragma once
#include <windows.h>

#include <functional>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

class DxWindow {
public:
    struct Options {
        const wchar_t* className = L"Grounded2PanelES";
        const wchar_t* title = L"Grounded 2 · Panel";
        HINSTANCE instance = nullptr;   // module that owns the icon and font resources
        int iconResource = 1;
        int width = 1180, height = 760;
        int minWidth = 860, minHeight = 560;
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        bool topmost = false;
        bool startHidden = false;
    };

    bool Create(const Options& options);
    void Destroy();

    // Processes pending window messages. Returns false after WM_QUIT.
    bool Pump();
    // Starts an ImGui frame; returns false when nothing should be drawn (hidden/minimised).
    bool BeginFrame();
    void EndFrame();

    HWND Hwnd() const { return hwnd_; }
    void Attach(HWND hwnd) { hwnd_ = hwnd; }
    float Scale() const { return scale_; }
    bool ScaleChanged() { bool changed = scaleChanged_; scaleChanged_ = false; return changed; }
    void InvalidateFonts();   // call after rebuilding the font atlas

    void SetVisible(bool visible);
    bool Visible() const;
    void SetTopmost(bool topmost);
    void SetOpacity(float opacity);   // 0.5 .. 1
    void Minimize();
    void ToggleMaximize();
    bool Maximized() const;

    // Title bar support: the frame code tells the window whether the point under the mouse in
    // the header can drag the window (i.e. it is not over a button).
    void SetHeaderHeight(float pixels) { headerHeight_ = pixels; }
    void SetCaptionDraggable(bool draggable) { captionDraggable_ = draggable; }

    // Set when the user clicks the close button or presses Alt+F4.
    bool TakeCloseRequest() { bool requested = closeRequested_; closeRequested_ = false; return requested; }

    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);

private:
    bool CreateDevice();
    void CleanupDevice();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void UpdateScale();

    HWND hwnd_ = nullptr;
    Options options_{};
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* target_ = nullptr;
    UINT resizeWidth_ = 0, resizeHeight_ = 0;
    float scale_ = 1.0f;
    bool scaleChanged_ = false;
    float headerHeight_ = 56;
    bool captionDraggable_ = false;
    bool closeRequested_ = false;
    bool imguiReady_ = false;
    float opacity_ = 1.0f;
};
