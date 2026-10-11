// Dear ImGui Win32 + DX11 backend implementation
// This is a minimal implementation - for production use the official imgui_impl_win32.cpp and imgui_impl_dx11.cpp

#include "imgui_impl.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

// Win32 backend
namespace ImGui_ImplWin32 {

static HWND g_hWnd = nullptr;
static INT64 g_Time = 0;
static INT64 g_TicksPerSecond = 0;
static ImGuiMouseCursor g_LastMouseCursor = ImGuiMouseArrow;
static bool g_WantUpdateMonitors = true;

bool Init(HWND hWnd) {
    g_hWnd = hWnd;
    ::QueryPerformanceFrequency((LARGE_INTEGER*)&g_TicksPerSecond);
    ::QueryPerformanceCounter((LARGE_INTEGER*)&g_Time);
    return true;
}

void Shutdown() {
    g_hWnd = nullptr;
}

void NewFrame() {
    if (!g_hWnd) return;

    // Setup time step
    INT64 current_time;
    ::QueryPerformanceCounter((LARGE_INTEGER*)&current_time);
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = (float)(current_time - g_Time) / g_TicksPerSecond;
    g_Time = current_time;

    // Setup display size
    RECT rect;
    ::GetClientRect(g_hWnd, &rect);
    io.DisplaySize = ImVec2((float)(rect.right - rect.left), (float)(rect.bottom - rect.top));

    // Keyboard
    io.AddKeyEvent(ImGuiKey_Tab, (::GetKeyState(VK_TAB) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_LeftArrow, (::GetKeyState(VK_LEFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_RightArrow, (::GetKeyState(VK_RIGHT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_UpArrow, (::GetKeyState(VK_UP) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_DownArrow, (::GetKeyState(VK_DOWN) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_PageUp, (::GetKeyState(VK_PRIOR) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_PageDown, (::GetKeyState(VK_NEXT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Home, (::GetKeyState(VK_HOME) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_End, (::GetKeyState(VK_END) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Insert, (::GetKeyState(VK_INSERT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Delete, (::GetKeyState(VK_DELETE) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Backspace, (::GetKeyState(VK_BACK) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Space, (::GetKeyState(VK_SPACE) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Enter, (::GetKeyState(VK_RETURN) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Escape, (::GetKeyState(VK_ESCAPE) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_LeftCtrl, (::GetKeyState(VK_CONTROL) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_LeftShift, (::GetKeyState(VK_SHIFT) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_LeftAlt, (::GetKeyState(VK_MENU) & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_A, (::GetKeyState('A') & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_C, (::GetKeyState('C') & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_V, (::GetKeyState('V') & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_X, (::GetKeyState('X') & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Y, (::GetKeyState('Y') & 0x8000) != 0);
    io.AddKeyEvent(ImGuiKey_Z, (::GetKeyState('Z') & 0x8000) != 0);

    // Mouse
    POINT mouse_pos;
    ::GetCursorPos(&mouse_pos);
    ::ScreenToClient(g_hWnd, &mouse_pos);
    io.AddMousePosEvent((float)mouse_pos.x, (float)mouse_pos.y);
    io.AddMouseButtonEvent(0, (::GetKeyState(VK_LBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(1, (::GetKeyState(VK_RBUTTON) & 0x8000) != 0);
    io.AddMouseButtonEvent(2, (::GetKeyState(VK_MBUTTON) & 0x8000) != 0);
}

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ImGuiIO& io = ImGui::GetIO();
    switch (msg) {
        case WM_MOUSEWHEEL:
            io.AddMouseWheelEvent(0.0f, (float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA);
            return true;
        case WM_MOUSEHWHEEL:
            io.AddMouseWheelEvent((float)GET_WHEEL_DELTA_WPARAM(wParam) / (float)WHEEL_DELTA, 0.0f);
            return true;
        case WM_CHAR:
            if (wParam > 0 && wParam < 0x10000)
                io.AddInputCharacter((unsigned int)wParam);
            return true;
        case WM_SETFOCUS:
            io.AddFocusEvent(true);
            return true;
        case WM_KILLFOCUS:
            io.AddFocusEvent(false);
            return true;
    }
    return false;
}

} // namespace ImGui_ImplWin32

// DX11 backend
namespace ImGui_ImplDX11 {

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static ID3D11VertexShader* g_pVertexShader = nullptr;
static ID3D11InputLayout* g_pInputLayout = nullptr;
static ID3D11Buffer* g_pVertexConstantBuffer = nullptr;
static ID3D11PixelShader* g_pPixelShader = nullptr;
static ID3D11SamplerState* g_pFontSampler = nullptr;
static ID3D11ShaderResourceView* g_pFontTextureView = nullptr;
static ID3D11RasterizerState* g_pRasterizerState = nullptr;
static ID3D11BlendState* g_pBlendState = nullptr;
static ID3D11DepthStencilState* g_pDepthStencilState = nullptr;
static ID3D11Buffer* g_pVertexBuffer = nullptr;
static ID3D11Buffer* g_pIndexBuffer = nullptr;
static int g_VertexBufferSize = 5000, g_IndexBufferSize = 10000;

struct VERTEX_CONSTANT_BUFFER {
    float mvp[4][4];
};

bool Init(ID3D11Device* device, ID3D11DeviceContext* context) {
    g_pd3dDevice = device;
    g_pd3dDeviceContext = context;

    // Create vertex shader
    const char* vertexShaderCode = R"(
        cbuffer vertexBuffer : register(b0) {
            float4x4 ProjectionMatrix;
        };
        struct VS_INPUT {
            float2 pos : POSITION;
            float4 col : COLOR0;
            float2 uv  : TEXCOORD0;
        };
        struct PS_INPUT {
            float4 pos : SV_POSITION;
            float4 col : COLOR0;
            float2 uv  : TEXCOORD0;
        };
        PS_INPUT main(VS_INPUT input) {
            PS_INPUT output;
            output.pos = mul(ProjectionMatrix, float4(input.pos.xy, 0.0f, 1.0f));
            output.col = input.col;
            output.uv = input.uv;
            return output;
        }
    )";

    ID3DBlob* vertexShaderBlob = nullptr;
    D3DCompile(vertexShaderCode, strlen(vertexShaderCode), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vertexShaderBlob, nullptr);
    g_pd3dDevice->CreateVertexShader(vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), nullptr, &g_pVertexShader);

    // Create input layout
    D3D11_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ImDrawVert, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ImDrawVert, col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    g_pd3dDevice->CreateInputLayout(inputLayout, 3, vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize(), &g_pInputLayout);
    vertexShaderBlob->Release();

    // Create constant buffer
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(VERTEX_CONSTANT_BUFFER);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    g_pd3dDevice->CreateBuffer(&cbDesc, nullptr, &g_pVertexConstantBuffer);

    // Create pixel shader
    const char* pixelShaderCode = R"(
        struct PS_INPUT {
            float4 pos : SV_POSITION;
            float4 col : COLOR0;
            float2 uv  : TEXCOORD0;
        };
        Texture2D Texture0 : register(t0);
        SamplerState Sampler0 : register(s0);
        float4 main(PS_INPUT input) : SV_TARGET {
            return input.col * Texture0.Sample(Sampler0, input.uv);
        }
    )";

    ID3DBlob* pixelShaderBlob = nullptr;
    D3DCompile(pixelShaderCode, strlen(pixelShaderCode), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &pixelShaderBlob, nullptr);
    g_pd3dDevice->CreatePixelShader(pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize(), nullptr, &g_pPixelShader);
    pixelShaderBlob->Release();

    // Create font sampler
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
    g_pd3dDevice->CreateSamplerState(&samplerDesc, &g_pFontSampler);

    // Create rasterizer state
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthBias = 0;
    rasterDesc.DepthBiasClamp = 0.0f;
    rasterDesc.SlopeScaledDepthBias = 0.0f;
    rasterDesc.DepthClipEnable = TRUE;
    rasterDesc.ScissorEnable = TRUE;
    rasterDesc.MultisampleEnable = FALSE;
    rasterDesc.AntialiasedLineEnable = FALSE;
    g_pd3dDevice->CreateRasterizerState(&rasterDesc, &g_pRasterizerState);

    // Create blend state
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_pd3dDevice->CreateBlendState(&blendDesc, &g_pBlendState);

    // Create depth stencil state
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
    depthDesc.StencilEnable = FALSE;
    g_pd3dDevice->CreateDepthStencilState(&depthDesc, &g_pDepthStencilState);

    return true;
}

void Shutdown() {
    if (g_pVertexBuffer) { g_pVertexBuffer->Release(); g_pVertexBuffer = nullptr; }
    if (g_pIndexBuffer) { g_pIndexBuffer->Release(); g_pIndexBuffer = nullptr; }
    if (g_pVertexConstantBuffer) { g_pVertexConstantBuffer->Release(); g_pVertexConstantBuffer = nullptr; }
    if (g_pInputLayout) { g_pInputLayout->Release(); g_pInputLayout = nullptr; }
    if (g_pVertexShader) { g_pVertexShader->Release(); g_pVertexShader = nullptr; }
    if (g_pPixelShader) { g_pPixelShader->Release(); g_pPixelShader = nullptr; }
    if (g_pFontSampler) { g_pFontSampler->Release(); g_pFontSampler = nullptr; }
    if (g_pFontTextureView) { g_pFontTextureView->Release(); g_pFontTextureView = nullptr; }
    if (g_pRasterizerState) { g_pRasterizerState->Release(); g_pRasterizerState = nullptr; }
    if (g_pBlendState) { g_pBlendState->Release(); g_pBlendState = nullptr; }
    if (g_pDepthStencilState) { g_pDepthStencilState->Release(); g_pDepthStencilState = nullptr; }
    g_pd3dDevice = nullptr;
    g_pd3dDeviceContext = nullptr;
}

void NewFrame() {
    if (!g_pFontTextureView) {
        // Create font texture
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subResource = {};
        subResource.pSysMem = pixels;
        subResource.SysMemPitch = width * 4;

        ID3D11Texture2D* texture = nullptr;
        g_pd3dDevice->CreateTexture2D(&texDesc, &subResource, &texture);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.MostDetailedMip = 0;
        g_pd3dDevice->CreateShaderResourceView(texture, &srvDesc, &g_pFontTextureView);
        texture->Release();

        io.Fonts->SetTexID(g_pFontTextureView);
    }
}

void RenderDrawData(ImDrawData* draw_data) {
    if (!draw_data || draw_data->CmdListsCount == 0) return;

    // Update buffers
    size_t vertex_count = 0, index_count = 0;
    for (int i = 0; i < draw_data->CmdListsCount; ++i) {
        vertex_count += draw_data->CmdLists[i]->VtxBuffer.Size;
        index_count += draw_data->CmdLists[i]->IdxBuffer.Size;
    }

    if (vertex_count > (size_t)g_VertexBufferSize) {
        if (g_pVertexBuffer) { g_pVertexBuffer->Release(); g_pVertexBuffer = nullptr; }
        g_VertexBufferSize = (int)(vertex_count * 1.5f);
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = g_VertexBufferSize * sizeof(ImDrawVert);
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_pd3dDevice->CreateBuffer(&vbDesc, nullptr, &g_pVertexBuffer);
    }
    if (index_count > (size_t)g_IndexBufferSize) {
        if (g_pIndexBuffer) { g_pIndexBuffer->Release(); g_pIndexBuffer = nullptr; }
        g_IndexBufferSize = (int)(index_count * 1.5f);
        D3D11_BUFFER_DESC ibDesc = {};
        ibDesc.ByteWidth = g_IndexBufferSize * sizeof(ImDrawIdx);
        ibDesc.Usage = D3D11_USAGE_DYNAMIC;
        ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        ibDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        g_pd3dDevice->CreateBuffer(&ibDesc, nullptr, &g_pIndexBuffer);
    }

    // Map and copy vertex/index data
    D3D11_MAPPED_SUBRESOURCE vtxResource, idxResource;
    g_pd3dDeviceContext->Map(g_pVertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtxResource);
    g_pd3dDeviceContext->Map(g_pIndexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &idxResource);

    ImDrawVert* vtxDst = (ImDrawVert*)vtxResource.pData;
    ImDrawIdx* idxDst = (ImDrawIdx*)idxResource.pData;

    for (int i = 0; i < draw_data->CmdListsCount; ++i) {
        const ImDrawList* cmd_list = draw_data->CmdLists[i];
        memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtxDst += cmd_list->VtxBuffer.Size;
        idxDst += cmd_list->IdxBuffer.Size;
    }

    g_pd3dDeviceContext->Unmap(g_pVertexBuffer, 0);
    g_pd3dDeviceContext->Unmap(g_pIndexBuffer, 0);

    // Setup render state
    g_pd3dDeviceContext->IASetInputLayout(g_pInputLayout);
    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pd3dDeviceContext->VSSetShader(g_pVertexShader, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(g_pPixelShader, nullptr, 0);
    g_pd3dDeviceContext->PSSetSamplers(0, 1, &g_pFontSampler);
    g_pd3dDeviceContext->RSSetState(g_pRasterizerState);
    g_pd3dDeviceContext->OMSetBlendState(g_pBlendState, nullptr, 0xFFFFFFFF);
    g_pd3dDeviceContext->OMSetDepthStencilState(g_pDepthStencilState, 0);

    // Update constant buffer
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        g_pd3dDeviceContext->Map(g_pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        VERTEX_CONSTANT_BUFFER* cb = (VERTEX_CONSTANT_BUFFER*)mappedResource.pData;

        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] = {
            { 2.0f/(R-L), 0.0f, 0.0f, 0.0f },
            { 0.0f, 2.0f/(T-B), 0.0f, 0.0f },
            { 0.0f, 0.0f, 0.5f, 0.0f },
            { (R+L)/(L-R), (T+B)/(B-T), 0.5f, 1.0f }
        };
        memcpy(cb->mvp, mvp, sizeof(mvp));
        g_pd3dDeviceContext->Unmap(g_pVertexConstantBuffer, 0);
    }
    g_pd3dDeviceContext->VSSetConstantBuffers(0, 1, &g_pVertexConstantBuffer);

    // Render command lists
    int vtxOffset = 0, idxOffset = 0;
    for (int i = 0; i < draw_data->CmdListsCount; ++i) {
        const ImDrawList* cmd_list = draw_data->CmdLists[i];
        for (int j = 0; j < cmd_list->CmdBuffer.Size; ++j) {
            const ImDrawCmd* cmd = &cmd_list->CmdBuffer[j];
            if (cmd->UserCallback) {
                cmd->UserCallback(cmd_list, cmd);
            } else {
                ID3D11ShaderResourceView* srv = (ID3D11ShaderResourceView*)cmd->TextureId;
                g_pd3dDeviceContext->PSSetShaderResources(0, 1, &srv);

                D3D11_RECT rect = {};
                rect.left = (LONG)(cmd->ClipRect.x);
                rect.top = (LONG)(cmd->ClipRect.y);
                rect.right = (LONG)(cmd->ClipRect.z);
                rect.bottom = (LONG)(cmd->ClipRect.w);
                g_pd3dDeviceContext->RSSetScissorRects(1, &rect);

                g_pd3dDeviceContext->DrawIndexed((UINT)cmd->ElemCount, idxOffset, vtxOffset);
            }
            idxOffset += (int)cmd->ElemCount;
        }
        vtxOffset += cmd_list->VtxBuffer.Size;
    }
}

} // namespace ImGui_ImplDX11