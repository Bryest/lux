#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include "texture.h"
#include "d3dx12.h"
#include <vector>

static void Upload(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                   ComPtr<ID3D12Resource>& tex, ComPtr<ID3D12Resource>& upload,
                   UINT w, UINT h, const void* rgba)
{
    D3D12_RESOURCE_DESC td = {};
    td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width            = w;
    td.Height           = h;
    td.DepthOrArraySize = 1;
    td.MipLevels        = 1;
    td.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;

    auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));

    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    auto uhp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto ubd = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
    device->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &ubd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));

    D3D12_SUBRESOURCE_DATA sd = {};
    sd.pData      = rgba;
    sd.RowPitch   = w * 4;
    sd.SlicePitch = w * h * 4;
    UpdateSubresources<1>(cmdList, tex.Get(), upload.Get(), 0, 0, 1, &sd);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

bool Texture::LoadFile(const char* path, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    int w, h, ch;
    stbi_uc* pixels = stbi_load(path, &w, &h, &ch, 4);
    if (!pixels) return false;
    Upload(device, cmdList, resource, upload, (UINT)w, (UINT)h, pixels);
    stbi_image_free(pixels);
    return true;
}

void Texture::CreateChecker(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    constexpr UINT N = 16;
    std::vector<uint32_t> data(N * N);
    for (UINT y = 0; y < N; ++y)
        for (UINT x = 0; x < N; ++x)
            data[y * N + x] = ((x / 2 + y / 2) & 1) ? 0xFF606060 : 0xFFFFFFFF;
    Upload(device, cmdList, resource, upload, N, N, data.data());
}
