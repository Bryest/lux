#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <chrono>
#include <cmath>
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include "d3dx12.h"
#include "scene.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

constexpr UINT kBackBufferCount = 2;
constexpr UINT kWidth           = 1280;
constexpr UINT kHeight          = 720;
constexpr UINT kShadowMapSize   = 4096;
constexpr UINT kShadowSrvSlot   = 255; // fixed slot in g_srvHeap

struct SceneConstants {
    glm::mat4 mvp;        // 64
    glm::mat4 model;      // 64
    glm::mat4 lightVP;    // 64  light view-projection for shadow mapping
    glm::vec4 lightDir;   // 16  xyz = dir toward light
    glm::vec4 cameraPos;  // 16  xyz = camera world pos
    glm::vec4 lightColor; // 16  xyz = color * intensity
    float     _pad[4];    // 16  pad to 256
};
static_assert(sizeof(SceneConstants) == 256);

struct Camera {
    glm::vec3 pos   = { 0.0f, 2.0f,  0.0f };
    float     yaw   = 0.0f;
    float     pitch = -0.1f;

    glm::vec3 forward() const {
        return { cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw) };
    }
    glm::vec3 right() const {
        return glm::normalize(glm::cross(forward(), glm::vec3(0, 1, 0)));
    }
    glm::mat4 view() const {
        return glm::lookAtLH(pos, pos + forward(), { 0, 1, 0 });
    }
};

// --- D3D12 state ---
ComPtr<ID3D12Device>              g_device;
ComPtr<ID3D12CommandQueue>        g_commandQueue;
ComPtr<IDXGISwapChain3>           g_swapChain;
ComPtr<ID3D12DescriptorHeap>      g_rtvHeap;
ComPtr<ID3D12Resource>            g_backBuffers[kBackBufferCount];
ComPtr<ID3D12CommandAllocator>    g_commandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12Fence>               g_fence;
ComPtr<ID3D12RootSignature>       g_rootSignature;
ComPtr<ID3D12PipelineState>       g_pipelineState;
ComPtr<ID3D12DescriptorHeap>      g_dsvHeap;
ComPtr<ID3D12DescriptorHeap>      g_srvHeap;
ComPtr<ID3D12Resource>            g_depthBuffer;
ComPtr<ID3D12Resource>            g_constantBuffer;
UINT8*                            g_cbMapped      = nullptr;
D3D12_VIEWPORT                    g_viewport      = {};
D3D12_RECT                        g_scissor       = {};
UINT64  g_fenceValue        = 0;
HANDLE  g_fenceEvent        = nullptr;
UINT    g_rtvDescriptorSize = 0;
UINT    g_frameIndex        = 0;
UINT    g_windowWidth       = kWidth;
UINT    g_windowHeight      = kHeight;
HWND    g_hwnd              = nullptr;

// --- Shadow state ---
ComPtr<ID3D12Resource>       g_shadowMap;
ComPtr<ID3D12DescriptorHeap> g_shadowDsvHeap;
ComPtr<ID3D12RootSignature>  g_shadowRootSignature;
ComPtr<ID3D12PipelineState>  g_shadowPipelineState;
ComPtr<ID3D12Resource>       g_shadowConstantBuffer;
UINT8*                       g_shadowCbMapped = nullptr;

// --- Scene state ---
Camera    g_camera;
Scene     g_scene;
UINT      g_srvDescSize = 0;
float     g_moveSpeed   = 10.0f;
glm::vec3 g_lightDir    = glm::vec3(0.0f, 2.0f, -0.45f); // editable via ImGui

// --- ImGui ---
ComPtr<ID3D12DescriptorHeap> g_imguiSrvHeap;
bool   g_mouseLook  = false;
int    g_lastMouseX = 0;
int    g_lastMouseY = 0;

void WaitForGPU() {
    ++g_fenceValue;
    g_commandQueue->Signal(g_fence.Get(), g_fenceValue);
    if (g_fence->GetCompletedValue() < g_fenceValue) {
        g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
        WaitForSingleObject(g_fenceEvent, INFINITE);
    }
}

void CreateDepthBuffer(UINT w, UINT h) {
    D3D12_RESOURCE_DESC dd = {};
    dd.Dimension            = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dd.Width                = w;
    dd.Height               = h;
    dd.DepthOrArraySize     = 1;
    dd.MipLevels            = 1;
    dd.Format               = DXGI_FORMAT_D32_FLOAT;
    dd.SampleDesc.Count     = 1;
    dd.Flags                = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE cv    = {};
    cv.Format               = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth   = 1.0f;

    auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE,
        &dd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, IID_PPV_ARGS(&g_depthBuffer));

    g_device->CreateDepthStencilView(g_depthBuffer.Get(), nullptr,
        g_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void OnResize(UINT w, UINT h) {
    if (w == 0 || h == 0) return;
    WaitForGPU();
    for (UINT i = 0; i < kBackBufferCount; ++i) g_backBuffers[i].Reset();
    g_depthBuffer.Reset();

    g_swapChain->ResizeBuffers(kBackBufferCount, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i]));
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, g_rtvDescriptorSize);
    }

    CreateDepthBuffer(w, h);

    g_viewport.Width    = (float)w;
    g_viewport.Height   = (float)h;
    g_scissor.right     = (LONG)w;
    g_scissor.bottom    = (LONG)h;
    g_windowWidth       = w;
    g_windowHeight      = h;
}

void InitD3D12(HWND hwnd) {
    UINT factoryFlags = 0;
#ifdef _DEBUG
    {
        ComPtr<ID3D12Debug> dbg;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) {
            dbg->EnableDebugLayer();
            factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    ComPtr<IDXGIFactory6> factory;
    CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory));

    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&g_device));

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    g_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&g_commandQueue));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount       = kBackBufferCount;
    scd.Width             = kWidth;
    scd.Height            = kHeight;
    scd.Format            = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect        = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count  = 1;

    ComPtr<IDXGISwapChain1> sc1;
    factory->CreateSwapChainForHwnd(g_commandQueue.Get(), hwnd, &scd, nullptr, nullptr, &sc1);
    sc1.As(&g_swapChain);
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rhd = {};
    rhd.NumDescriptors = kBackBufferCount;
    rhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    g_device->CreateDescriptorHeap(&rhd, IID_PPV_ARGS(&g_rtvHeap));
    g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < kBackBufferCount; ++i) {
        g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_backBuffers[i]));
        g_device->CreateRenderTargetView(g_backBuffers[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, g_rtvDescriptorSize);
    }

    D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
    dhd.NumDescriptors = 1;
    dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    g_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&g_dsvHeap));

    CreateDepthBuffer(kWidth, kHeight);

    g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_commandAllocator));
    g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_commandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_commandList));
    g_commandList->Close();

    g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    g_viewport.Width    = (float)kWidth;
    g_viewport.Height   = (float)kHeight;
    g_viewport.MaxDepth = 1.0f;
    g_scissor.right     = kWidth;
    g_scissor.bottom    = kHeight;
}

void InitShadows() {
    // Shadow map (R32_TYPELESS so it can be both DSV and SRV)
    D3D12_RESOURCE_DESC sd = {};
    sd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    sd.Width            = kShadowMapSize;
    sd.Height           = kShadowMapSize;
    sd.DepthOrArraySize = 1;
    sd.MipLevels        = 1;
    sd.Format           = DXGI_FORMAT_R32_TYPELESS;
    sd.SampleDesc.Count = 1;
    sd.Flags            = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE cv = {};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;

    auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &sd,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&g_shadowMap));

    // DSV
    D3D12_DESCRIPTOR_HEAP_DESC dhd = {};
    dhd.NumDescriptors = 1;
    dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    g_device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&g_shadowDsvHeap));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    g_device->CreateDepthStencilView(g_shadowMap.Get(), &dsvDesc,
        g_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart());

    // Shadow constant buffer (just holds lightVP mat4, padded to 256)
    auto chp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto cbd = CD3DX12_RESOURCE_DESC::Buffer(256);
    g_device->CreateCommittedResource(&chp, D3D12_HEAP_FLAG_NONE, &cbd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_shadowConstantBuffer));
    CD3DX12_RANGE r(0, 0);
    g_shadowConstantBuffer->Map(0, &r, (void**)&g_shadowCbMapped);

    // Shadow root signature: just CBV at b0
    CD3DX12_ROOT_PARAMETER srp[1];
    srp[0].InitAsConstantBufferView(0);
    CD3DX12_ROOT_SIGNATURE_DESC srsd;
    srsd.Init(1, srp, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> srsBlob, srsErr;
    D3D12SerializeRootSignature(&srsd, D3D_ROOT_SIGNATURE_VERSION_1, &srsBlob, &srsErr);
    g_device->CreateRootSignature(0, srsBlob->GetBufferPointer(), srsBlob->GetBufferSize(),
        IID_PPV_ARGS(&g_shadowRootSignature));

    // Shadow PSO (depth-only, no pixel shader)
    ComPtr<ID3DBlob> shadowVs, errBlob;
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    D3DCompileFromFile(L"shadow.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &shadowVs, &errBlob);

    D3D12_INPUT_ELEMENT_DESC shadowLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC spd = {};
    spd.InputLayout           = { shadowLayout, 1 };
    spd.pRootSignature        = g_shadowRootSignature.Get();
    spd.VS                    = CD3DX12_SHADER_BYTECODE(shadowVs.Get());
    auto srs                  = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    srs.FrontCounterClockwise = TRUE;
    srs.DepthBias             = 5000;
    srs.SlopeScaledDepthBias  = 2.0f;
    spd.RasterizerState       = srs;
    spd.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    spd.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    spd.SampleMask            = UINT_MAX;
    spd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    spd.NumRenderTargets      = 0;
    spd.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    spd.SampleDesc.Count      = 1;
    g_device->CreateGraphicsPipelineState(&spd, IID_PPV_ARGS(&g_shadowPipelineState));
}

void InitScene() {
    CD3DX12_DESCRIPTOR_RANGE srvRange0, srvRange1, srvRange2, srvRange3;
    srvRange0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0 = albedo
    srvRange1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1 = normal map
    srvRange2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); // t2 = metallic-roughness
    srvRange3.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3); // t3 = shadow map

    CD3DX12_ROOT_PARAMETER rp[5];
    rp[0].InitAsConstantBufferView(0);
    rp[1].InitAsDescriptorTable(1, &srvRange0, D3D12_SHADER_VISIBILITY_PIXEL);
    rp[2].InitAsDescriptorTable(1, &srvRange1, D3D12_SHADER_VISIBILITY_PIXEL);
    rp[3].InitAsDescriptorTable(1, &srvRange2, D3D12_SHADER_VISIBILITY_PIXEL);
    rp[4].InitAsDescriptorTable(1, &srvRange3, D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    // s0: linear wrap — textures
    samplers[0].Filter           = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister   = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // s1: point clamp — shadow map
    samplers[1].Filter           = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[1].AddressU         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW         = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxLOD           = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister   = 1;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    CD3DX12_ROOT_SIGNATURE_DESC rsd;
    rsd.Init(5, rp, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> rsBlob, rsErr;
    D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    g_device->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature));

    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    UINT flags = 0;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", flags, 0, &vsBlob, &errBlob);
    D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", flags, 0, &psBlob, &errBlob);

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.InputLayout           = { layout, _countof(layout) };
    pd.pRootSignature        = g_rootSignature.Get();
    pd.VS                    = CD3DX12_SHADER_BYTECODE(vsBlob.Get());
    pd.PS                    = CD3DX12_SHADER_BYTECODE(psBlob.Get());
    auto rs = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rs.FrontCounterClockwise = TRUE; // OBJ from Blender uses CCW winding
    pd.RasterizerState       = rs;
    pd.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pd.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pd.SampleMask            = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat             = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc.Count      = 1;
    g_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&g_pipelineState));

    {
        auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bd = CD3DX12_RESOURCE_DESC::Buffer(sizeof(SceneConstants));
        g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_constantBuffer));
        CD3DX12_RANGE r(0, 0);
        g_constantBuffer->Map(0, &r, (void**)&g_cbMapped);
    }

    // --- SRV heap: 256 slots (0-254 = scene textures, 255 = shadow map) ---
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.NumDescriptors = 256;
        hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        g_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_srvHeap));
        g_srvDescSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    InitShadows();

    // Shadow map SRV at slot 255
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Format                  = DXGI_FORMAT_R32_FLOAT;
        sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels     = 1;
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(g_srvHeap->GetCPUDescriptorHandleForHeapStart());
        h.Offset(kShadowSrvSlot, g_srvDescSize);
        g_device->CreateShaderResourceView(g_shadowMap.Get(), &sd, h);
    }

    g_commandAllocator->Reset();
    g_commandList->Reset(g_commandAllocator.Get(), nullptr);

    bool sceneOk = g_scene.LoadGltf("models/sponza/glTF/Sponza.gltf",
                                     g_device.Get(), g_commandList.Get(),
                                     g_srvHeap.Get(), g_srvDescSize);

    g_commandList->Close();
    ID3D12CommandList* lists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGPU();

    if (!sceneOk) {
        MessageBoxA(nullptr,
            "Could not load models/sponza/glTF/Sponza.gltf\n\n"
            "Make sure the Sponza assets are in a 'models/sponza/glTF' folder next to lux.exe.",
            "lux — missing scene", MB_OK | MB_ICONERROR);
        PostQuitMessage(1);
    }
}

void Update(float dt) {
    glm::vec3 fwd = g_camera.forward();
    glm::vec3 rgt = g_camera.right();

    if (GetKeyState('W') & 0x8000) g_camera.pos += fwd * g_moveSpeed * dt;
    if (GetKeyState('S') & 0x8000) g_camera.pos -= fwd * g_moveSpeed * dt;
    if (GetKeyState('A') & 0x8000) g_camera.pos -= rgt * g_moveSpeed * dt;
    if (GetKeyState('D') & 0x8000) g_camera.pos += rgt * g_moveSpeed * dt;
    if (GetKeyState(VK_SPACE)   & 0x8000) g_camera.pos.y += g_moveSpeed * dt;
    if (GetKeyState(VK_CONTROL) & 0x8000) g_camera.pos.y -= g_moveSpeed * dt;
}

void Render(float t) {
    glm::vec3 lightDirV = glm::normalize(g_lightDir);
    glm::vec3 lightPos  = lightDirV * 40.0f;
    glm::mat4 lightView = glm::lookAtLH(lightPos, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0, 1, 0));
    glm::mat4 lightProj = glm::orthoLH_ZO(-22.0f, 22.0f, -12.0f, 12.0f, 0.1f, 80.0f);
    glm::mat4 lightVP   = lightProj * lightView;

    g_commandAllocator->Reset();
    g_commandList->Reset(g_commandAllocator.Get(), nullptr);

    // ===================== SHADOW PASS =====================
    auto toDepthWrite = CD3DX12_RESOURCE_BARRIER::Transition(
        g_shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_DEPTH_WRITE);
    g_commandList->ResourceBarrier(1, &toDepthWrite);

    auto shadowDsv = g_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
    g_commandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    g_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);

    D3D12_VIEWPORT shadowVP = { 0, 0, (float)kShadowMapSize, (float)kShadowMapSize, 0, 1 };
    D3D12_RECT     shadowSc = { 0, 0, (LONG)kShadowMapSize, (LONG)kShadowMapSize };
    g_commandList->RSSetViewports(1, &shadowVP);
    g_commandList->RSSetScissorRects(1, &shadowSc);

    memcpy(g_shadowCbMapped, &lightVP, sizeof(glm::mat4));

    g_commandList->SetGraphicsRootSignature(g_shadowRootSignature.Get());
    g_commandList->SetPipelineState(g_shadowPipelineState.Get());
    g_commandList->SetGraphicsRootConstantBufferView(0, g_shadowConstantBuffer->GetGPUVirtualAddress());
    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_scene.DrawDepth(g_commandList.Get());

    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        g_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    g_commandList->ResourceBarrier(1, &toSrv);

    // ===================== MAIN PASS =====================
    g_commandList->SetGraphicsRootSignature(g_rootSignature.Get());
    g_commandList->SetPipelineState(g_pipelineState.Get());
    g_commandList->RSSetViewports(1, &g_viewport);
    g_commandList->RSSetScissorRects(1, &g_scissor);

    auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(
        g_backBuffers[g_frameIndex].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    g_commandList->ResourceBarrier(1, &toRT);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(g_rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        g_frameIndex, g_rtvDescriptorSize);
    auto dsv = g_dsvHeap->GetCPUDescriptorHandleForHeapStart();

    g_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float clearColor[] = { 0.05f, 0.1f, 0.2f, 1.0f };
    g_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    g_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view  = g_camera.view();
    glm::mat4 proj  = glm::perspectiveLH_ZO(
        glm::radians(60.0f),
        (float)g_windowWidth / (float)g_windowHeight,
        0.1f, 500.0f);

    SceneConstants sc = {};
    sc.mvp       = proj * view * model;
    sc.model     = model;
    sc.lightVP    = lightVP;
    sc.lightDir   = glm::vec4(lightDirV, 0.0f);
    sc.cameraPos  = glm::vec4(g_camera.pos, 0.0f);
    sc.lightColor = glm::vec4(3.0f, 2.8f, 2.5f, 0.0f); // warm sunlight
    memcpy(g_cbMapped, &sc, sizeof(sc));

    ID3D12DescriptorHeap* heaps[] = { g_srvHeap.Get() };
    g_commandList->SetDescriptorHeaps(1, heaps);
    g_commandList->SetGraphicsRootConstantBufferView(0, g_constantBuffer->GetGPUVirtualAddress());
    // Bind shadow map SRV at root param 4 (t3) — same for all draw calls
    CD3DX12_GPU_DESCRIPTOR_HANDLE shadowSrv(g_srvHeap->GetGPUDescriptorHandleForHeapStart());
    shadowSrv.Offset(kShadowSrvSlot, g_srvDescSize);
    g_commandList->SetGraphicsRootDescriptorTable(4, shadowSrv);

    g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_scene.Draw(g_commandList.Get(), g_srvHeap.Get(), g_srvDescSize);

    // === IMGUI PASS ===
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("lux");
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("cam: %.1f %.1f %.1f", g_camera.pos.x, g_camera.pos.y, g_camera.pos.z);
    ImGui::Separator();
    ImGui::Text("Sun Direction");
    ImGui::SliderFloat("X##sun", &g_lightDir.x, -2.0f, 2.0f);
    ImGui::SliderFloat("Y##sun", &g_lightDir.y, 0.0f,  2.0f);
    ImGui::SliderFloat("Z##sun", &g_lightDir.z, -2.0f, 2.0f);
    ImGui::End();

    ImGui::Render();
    ID3D12DescriptorHeap* imguiHeaps[] = { g_imguiSrvHeap.Get() };
    g_commandList->SetDescriptorHeaps(1, imguiHeaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_commandList.Get());

    auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        g_backBuffers[g_frameIndex].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    g_commandList->ResourceBarrier(1, &toPresent);
    g_commandList->Close();

    ID3D12CommandList* lists[] = { g_commandList.Get() };
    g_commandQueue->ExecuteCommandLists(1, lists);
    g_swapChain->Present(1, 0);
    WaitForGPU();
    g_frameIndex = g_swapChain->GetCurrentBackBufferIndex();
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

void InitImGui(HWND hwnd) {
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = 1;
    hd.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    g_device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&g_imguiSrvHeap));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui::GetIO().IniFilename = nullptr; // don't save imgui.ini

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(g_device.Get(), kBackBufferCount,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        g_imguiSrvHeap.Get(),
        g_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        g_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart());
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (g_swapChain && wParam != SIZE_MINIMIZED) {
            UINT w = LOWORD(lParam), h = HIWORD(lParam);
            if (w > 0 && h > 0 && (w != g_windowWidth || h != g_windowHeight))
                OnResize(w, h);
        }
        return 0;
    case WM_RBUTTONDOWN: {
        g_mouseLook = true;
        SetCapture(hwnd);
        ShowCursor(FALSE);
        RECT rc; GetClientRect(hwnd, &rc);
        g_lastMouseX = (rc.right - rc.left) / 2;
        g_lastMouseY = (rc.bottom - rc.top) / 2;
        POINT center = { g_lastMouseX, g_lastMouseY };
        ClientToScreen(hwnd, &center);
        SetCursorPos(center.x, center.y);
        return 0;
    }
    case WM_RBUTTONUP:
        g_mouseLook = false;
        ReleaseCapture();
        ShowCursor(TRUE);
        return 0;
    case WM_MOUSEMOVE:
        if (g_mouseLook) {
            int x = (int)(short)LOWORD(lParam);
            int y = (int)(short)HIWORD(lParam);
            g_camera.yaw   += (x - g_lastMouseX) * 0.002f;
            g_camera.pitch -= (y - g_lastMouseY) * 0.002f;
            constexpr float kMaxPitch = 1.5607f;
            if (g_camera.pitch >  kMaxPitch) g_camera.pitch =  kMaxPitch;
            if (g_camera.pitch < -kMaxPitch) g_camera.pitch = -kMaxPitch;
            RECT rc; GetClientRect(hwnd, &rc);
            g_lastMouseX = (rc.right - rc.left) / 2;
            g_lastMouseY = (rc.bottom - rc.top) / 2;
            POINT center = { g_lastMouseX, g_lastMouseY };
            ClientToScreen(hwnd, &center);
            SetCursorPos(center.x, center.y);
        }
        return 0;
    case WM_MOUSEWHEEL:
        g_moveSpeed *= (GET_WHEEL_DELTA_WPARAM(wParam) > 0) ? 1.2f : (1.0f / 1.2f);
        if (g_moveSpeed < 0.5f)  g_moveSpeed = 0.5f;
        if (g_moveSpeed > 50.0f) g_moveSpeed = 50.0f;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    const wchar_t* cls = L"LuxWindowClass";
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = cls;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassEx(&wc);

    g_hwnd = CreateWindowEx(0, cls, L"lux", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, kWidth, kHeight,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_hwnd, nCmdShow);

    InitD3D12(g_hwnd);
    InitScene();
    InitImGui(g_hwnd);

    auto startTime = std::chrono::high_resolution_clock::now();
    auto prevTime  = startTime;

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            auto now = std::chrono::high_resolution_clock::now();
            float t  = std::chrono::duration<float>(now - startTime).count();
            float dt = std::chrono::duration<float>(now - prevTime).count();
            prevTime = now;
            Update(dt);
            Render(t);
        }
    }

    WaitForGPU();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CloseHandle(g_fenceEvent);
    return 0;
}
