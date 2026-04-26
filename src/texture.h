#pragma once
#include <d3d12.h>
#include <wrl.h>
using Microsoft::WRL::ComPtr;

struct Texture {
    ComPtr<ID3D12Resource> resource;
    ComPtr<ID3D12Resource> upload; // kept alive until GPU finishes copy

    // Load PNG/JPG/BMP from disk. Returns false if file not found.
    bool LoadFile(const char* path, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

    // 16x16 grey/white checker — used as fallback when no texture file exists.
    void CreateChecker(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
};
