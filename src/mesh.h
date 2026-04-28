#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <string>
using Microsoft::WRL::ComPtr;

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    float tangent[4]; // xyz = tangent direction, w = bitangent sign (±1)
};

struct Mesh {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    D3D12_INDEX_BUFFER_VIEW  ibv = {};
    UINT indexCount = 0;

    bool Load(const std::string& path, ID3D12Device* device);
    void Draw(ID3D12GraphicsCommandList* cmdList) const;
};
