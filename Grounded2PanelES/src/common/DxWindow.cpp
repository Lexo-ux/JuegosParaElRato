// SPDX-License-Identifier: GPL-3.0-or-later
#include "DxWindow.hpp"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        if (auto* created = static_cast<DxWindow*>(create->lpCreateParams)) created->Attach(hwnd);
    }
    auto* self = reinterpret_cast<DxWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->Hwnd()) return self->Handle(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

UINT DpiFor(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto getDpi = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (getDpi && hwnd) {
        UINT dpi = getDpi(hwnd);
        if (dpi) return dpi;
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
    ReleaseDC(nullptr, dc);
    return dpi ? dpi : 96;
}

} // namespace

bool DxWindow::Create(const Options& options) {
    options_ = options;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = options.instance;
    wc.hIcon = LoadIconW(options.instance, MAKEINTRESOURCEW(options.iconResource));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(14, 20, 17));
    wc.lpszClassName = options.className;
    RegisterClassExW(&wc);

    // Size in physical pixels for the primary monitor's DPI; corrected after creation.
    const float initialScale = static_cast<float>(DpiFor(nullptr)) / 96.0f;
    const int width = static_cast<int>(options.width * initialScale);
    const int height = static_cast<int>(options.height * initialScale);
    int x = options.x, y = options.y;
    if (x == CW_USEDEFAULT) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + std::max(0L, (work.right - work.left - width) / 2);
        y = work.top + std::max(0L, (work.bottom - work.top - height) / 2);
    }
    DWORD exStyle = WS_EX_APPWINDOW | (options.topmost ? WS_EX_TOPMOST : 0);
    DWORD style = WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CLIPCHILDREN;
    hwnd_ = CreateWindowExW(exStyle, options.className, options.title, style, x, y, width, height, nullptr, nullptr,
                            options.instance, this);
    if (!hwnd_) return false;

    // Dark, rounded frame with the system shadow on Windows 10/11.
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    int corners = 2 /* DWMWCP_ROUND */;
    DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));
    COLORREF border = RGB(36, 51, 41);
    DwmSetWindowAttribute(hwnd_, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!CreateDevice()) {
        CleanupDevice();
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    UpdateScale();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);
    imguiReady_ = true;

    ShowWindow(hwnd_, options.startHidden ? SW_HIDE : SW_SHOWNORMAL);
    if (!options.startHidden) UpdateWindow(hwnd_);
    return true;
}

void DxWindow::Destroy() {
    if (imguiReady_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    CleanupDevice();
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    UnregisterClassW(options_.className, options_.instance);
}

bool DxWindow::CreateDevice() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    // Blt model: compatible with layered windows (transparency) and with older systems.
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &swapChain_, &device_, &obtained, &context_);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &swapChain_, &device_, &obtained, &context_);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void DxWindow::CleanupDevice() {
    CleanupRenderTarget();
    if (swapChain_) { swapChain_->Release(); swapChain_ = nullptr; }
    if (context_) { context_->Release(); context_ = nullptr; }
    if (device_) { device_->Release(); device_ = nullptr; }
}

void DxWindow::CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) && back) {
        device_->CreateRenderTargetView(back, nullptr, &target_);
        back->Release();
    }
}

void DxWindow::CleanupRenderTarget() {
    if (target_) { target_->Release(); target_ = nullptr; }
}

void DxWindow::UpdateScale() {
    float scale = static_cast<float>(DpiFor(hwnd_)) / 96.0f;
    scale = std::clamp(scale, 1.0f, 3.0f);
    if (scale != scale_) {
        scale_ = scale;
        scaleChanged_ = true;
    }
}

bool DxWindow::Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

bool DxWindow::BeginFrame() {
    if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(hwnd_)) return false;
    if (resizeWidth_ && resizeHeight_) {
        CleanupRenderTarget();
        swapChain_->ResizeBuffers(0, resizeWidth_, resizeHeight_, DXGI_FORMAT_UNKNOWN, 0);
        resizeWidth_ = resizeHeight_ = 0;
        CreateRenderTarget();
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void DxWindow::EndFrame() {
    ImGui::Render();
    const float clear[4] = {14 / 255.0f, 20 / 255.0f, 17 / 255.0f, 1.0f};
    context_->OMSetRenderTargets(1, &target_, nullptr);
    context_->ClearRenderTargetView(target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    HRESULT hr = swapChain_->Present(1, 0);
    if (hr == DXGI_STATUS_OCCLUDED) Sleep(16);
}

void DxWindow::SetVisible(bool visible) {
    if (!hwnd_) return;
    if (visible) {
        ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(hwnd_);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool DxWindow::Visible() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(hwnd_); }

void DxWindow::SetTopmost(bool topmost) {
    if (hwnd_) SetWindowPos(hwnd_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DxWindow::SetOpacity(float opacity) {
    if (!hwnd_) return;
    opacity = std::clamp(opacity, 0.5f, 1.0f);
    if (opacity == opacity_) return;
    opacity_ = opacity;
    LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (opacity >= 0.999f) {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(opacity * 255), LWA_ALPHA);
    }
}

void DxWindow::InvalidateFonts() {
    if (imguiReady_) ImGui_ImplDX11_InvalidateDeviceObjects();
}

void DxWindow::Minimize() { if (hwnd_) ShowWindow(hwnd_, SW_MINIMIZE); }
void DxWindow::ToggleMaximize() { if (hwnd_) ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE); }
bool DxWindow::Maximized() const { return hwnd_ && IsZoomed(hwnd_); }

LRESULT DxWindow::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    if (imguiReady_ && ImGui_ImplWin32_WndProcHandler(hwnd_, message, wParam, lParam)) return TRUE;
    switch (message) {
    case WM_NCCALCSIZE:
        if (wParam == TRUE) {
            if (IsZoomed(hwnd_)) {
                // Keep a maximised borderless window inside the work area.
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                const int frame = GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                params->rgrc[0].left += frame;
                params->rgrc[0].right -= frame;
                params->rgrc[0].top += frame;
                params->rgrc[0].bottom -= frame;
            }
            return 0;
        }
        break;
    case WM_NCACTIVATE:
        return DefWindowProcW(hwnd_, message, wParam, -1);
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(hwnd_, &point);
        RECT rect;
        GetClientRect(hwnd_, &rect);
        const int border = static_cast<int>(6 * scale_);
        if (!IsZoomed(hwnd_)) {
            const bool left = point.x < border, right = point.x >= rect.right - border;
            const bool top = point.y < border, bottom = point.y >= rect.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        if (point.y < headerHeight_ && captionDraggable_) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            resizeWidth_ = LOWORD(lParam);
            resizeHeight_ = HIWORD(lParam);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize.x = static_cast<LONG>(options_.minWidth * scale_);
        info->ptMinTrackSize.y = static_cast<LONG>(options_.minHeight * scale_);
        return 0;
    }
    case WM_DPICHANGED: {
        UpdateScale();
        auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                     suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;  // no Alt menu beep
        break;
    case WM_CLOSE:
        closeRequested_ = true;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
