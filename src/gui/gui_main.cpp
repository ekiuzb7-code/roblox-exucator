#include "editor_tabs.hpp"
#include "imgui/imgui_impl.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <chrono>
#include <thread>

// Dear ImGui Win32 + DX11 backend (simplified inline)
namespace ImGui_ImplWin32 {
    extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    bool Init(HWND hWnd);
    void Shutdown();
    void NewFrame();
}

namespace ImGui_ImplDX11 {
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    void NewFrame();
    void RenderDrawData(ImDrawData* draw_data);
}

// Forward declarations
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Global state
static gui::TabEditor* g_editor = nullptr;
static HWND g_hWnd = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                featureLevelArray, 2, D3D11_SDK_VERSION,
                                                &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                            featureLevelArray, 2, D3D11_SDK_VERSION,
                                            &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    }
    if (FAILED(res)) return false;

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void ResizeSwapChain() {
    CleanupRenderTarget();
    g_pSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // Create application window
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"RobloxExecutorWindow", nullptr };
    RegisterClassExW(&wc);

    // Window size
    int width = 1280, height = 800;
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    g_hWnd = CreateWindowW(wc.lpszClassName, L"Roblox Executor", WS_OVERLAPPEDWINDOW,
                           100, 100, rect.right - rect.left, rect.bottom - rect.top,
                           nullptr, nullptr, hInstance, nullptr);

    // Initialize D3D
    if (!CreateDeviceD3D(g_hWnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    // Show window
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    // Initialize ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "imgui.ini";

    // Load default font with larger size
    io.Fonts->AddFontDefault();
    io.FontDefault = io.Fonts->Fonts.back();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32::Init(g_hWnd);
    ImGui_ImplDX11::Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Create editor
    g_editor = new gui::TabEditor();
    g_editor->set_font_size(14.0f);

    // Try to auto-connect to executor
    g_editor->connect_to_executor(L"\\\\.\\pipe\\roblox_executor");

    // Main loop
    MSG msg = {};
    bool done = false;

    while (!done) {
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Handle window resize
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            ResizeSwapChain();
            g_ResizeWidth = g_ResizeHeight = 0;
        }

        // Start frame
        ImGui_ImplDX11::NewFrame();
        ImGui_ImplWin32::NewFrame();
        ImGui::NewFrame();

        // Render editor
        if (g_editor->render()) {
            done = true;
        }

        // Rendering
        ImGui::Render();
        const float clear_color[4] = { 0.1f, 0.1f, 0.12f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11::RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync
    }

    // Cleanup
    delete g_editor;
    g_editor = nullptr;

    ImGui_ImplDX11::Shutdown();
    ImGui_ImplWin32::Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hWnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    return 0;
}

// Win32 message handler
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_SIZE:
            if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
                g_ResizeWidth = (UINT)LOWORD(lParam);
                g_ResizeHeight = (UINT)HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_DPICHANGED:
            if (ImGui_ImplWin32::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
                return true;
            break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Global resize variables
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;