#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include "mesh.h"
#include "d3dx12.h"
#include <vector>

bool Mesh::Load(const std::string& path, ID3D12Device* device) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str()))
        return false;

    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;

    for (auto& shape : shapes) {
        for (auto& idx : shape.mesh.indices) {
            Vertex v = {};
            v.position[0] =  attrib.vertices[3 * idx.vertex_index + 0];
            v.position[1] =  attrib.vertices[3 * idx.vertex_index + 1];
            v.position[2] = -attrib.vertices[3 * idx.vertex_index + 2]; // RH→LH

            if (idx.normal_index >= 0) {
                v.normal[0] =  attrib.normals[3 * idx.normal_index + 0];
                v.normal[1] =  attrib.normals[3 * idx.normal_index + 1];
                v.normal[2] = -attrib.normals[3 * idx.normal_index + 2]; // RH→LH
            }

            if (idx.texcoord_index >= 0) {
                v.uv[0] =        attrib.texcoords[2 * idx.texcoord_index + 0];
                v.uv[1] = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]; // flip V for DX
            }

            indices.push_back((uint32_t)vertices.size());
            vertices.push_back(v);
        }
    }

    indexCount = (UINT)indices.size();

    {
        const UINT sz = (UINT)(vertices.size() * sizeof(Vertex));
        auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bd = CD3DX12_RESOURCE_DESC::Buffer(sz);
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vertexBuffer));
        UINT8* p; CD3DX12_RANGE r(0, 0);
        vertexBuffer->Map(0, &r, (void**)&p);
        memcpy(p, vertices.data(), sz);
        vertexBuffer->Unmap(0, nullptr);
        vbv = { vertexBuffer->GetGPUVirtualAddress(), sz, sizeof(Vertex) };
    }

    {
        const UINT sz = (UINT)(indices.size() * sizeof(uint32_t));
        auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto bd = CD3DX12_RESOURCE_DESC::Buffer(sz);
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&indexBuffer));
        UINT8* p; CD3DX12_RANGE r(0, 0);
        indexBuffer->Map(0, &r, (void**)&p);
        memcpy(p, indices.data(), sz);
        indexBuffer->Unmap(0, nullptr);
        ibv = { indexBuffer->GetGPUVirtualAddress(), sz, DXGI_FORMAT_R32_UINT };
    }

    return true;
}

void Mesh::Draw(ID3D12GraphicsCommandList* cmdList) const {
    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);
    cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}
