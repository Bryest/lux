#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include "mesh.h"
using Microsoft::WRL::ComPtr;

struct DrawCall {
    UINT indexCount;
    UINT startIndex;
    INT  baseVertex;
    UINT albedoIdx;
    UINT normalIdx;
    UINT mrIdx; // metallic-roughness: G=roughness, B=metallic
};

struct Scene {
    ComPtr<ID3D12Resource> vertexBuffer, indexBuffer;
    ComPtr<ID3D12Resource> vbUpload, ibUpload;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW  ibv = {};
    std::vector<DrawCall>               drawCalls;
    std::vector<ComPtr<ID3D12Resource>> textures;
    std::vector<ComPtr<ID3D12Resource>> uploads;
    UINT textureCount = 0;

    bool LoadGltf(const char* path, ID3D12Device* device,
                  ID3D12GraphicsCommandList* cmdList,
                  ID3D12DescriptorHeap* srvHeap, UINT srvDescSize);

    void Draw(ID3D12GraphicsCommandList* cmdList,
              ID3D12DescriptorHeap* srvHeap, UINT srvDescSize) const;
};
