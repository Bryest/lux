#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>
#include "scene.h"
#include "d3dx12.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

static std::string DirOf(const char* path) {
    std::string s(path);
    auto p = s.find_last_of("/\\");
    return p == std::string::npos ? "" : s.substr(0, p + 1);
}

static void UploadTex(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList,
                      ComPtr<ID3D12Resource>& tex, ComPtr<ID3D12Resource>& upload,
                      UINT w, UINT h, const void* rgba)
{
    D3D12_RESOURCE_DESC td = {};
    td.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width            = w; td.Height = h;
    td.DepthOrArraySize = 1; td.MipLevels = 1;
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
    sd.pData = rgba; sd.RowPitch = w * 4; sd.SlicePitch = w * h * 4;
    UpdateSubresources<1>(cmdList, tex.Get(), upload.Get(), 0, 0, 1, &sd);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

static void MakeSrv(ID3D12Device* device, ID3D12Resource* res,
                    ID3D12DescriptorHeap* heap, UINT idx, UINT descSize)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension           = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels     = 1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE h(heap->GetCPUDescriptorHandleForHeapStart());
    h.Offset(idx, descSize);
    device->CreateShaderResourceView(res, &sd, h);
}

bool Scene::LoadGltf(const char* path, ID3D12Device* device,
                     ID3D12GraphicsCommandList* cmdList,
                     ID3D12DescriptorHeap* srvHeap, UINT srvDescSize)
{
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) return false;
    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        cgltf_free(data); return false;
    }

    // Slot 0: grey/white checker — albedo fallback
    {
        constexpr UINT N = 16;
        uint32_t checker[N * N];
        for (UINT y = 0; y < N; ++y)
            for (UINT x = 0; x < N; ++x)
                checker[y*N+x] = ((x/2+y/2)&1) ? 0xFF606060 : 0xFFFFFFFF;
        ComPtr<ID3D12Resource> tex, up;
        UploadTex(device, cmdList, tex, up, N, N, checker);
        MakeSrv(device, tex.Get(), srvHeap, 0, srvDescSize);
        textures.push_back(std::move(tex));
        uploads.push_back(std::move(up));
    }

    // Slot 1: flat normal map (128,128,255) = tangent-space (0,0,1) — normal fallback
    {
        constexpr UINT N = 4;
        uint32_t flat[N * N];
        for (UINT i = 0; i < N*N; ++i)
            flat[i] = 0xFFFF8080; // R=128 G=128 B=255 A=255
        ComPtr<ID3D12Resource> tex, up;
        UploadTex(device, cmdList, tex, up, N, N, flat);
        MakeSrv(device, tex.Get(), srvHeap, 1, srvDescSize);
        textures.push_back(std::move(tex));
        uploads.push_back(std::move(up));
    }

    // Slot 2: default metallic-roughness (roughness=0.5, metallic=0) — MR fallback
    {
        constexpr UINT N = 4;
        uint32_t mr[N * N];
        // RGBA: R=255, G=128(roughness=0.5), B=0(metallic=0), A=255
        for (UINT i = 0; i < N*N; ++i)
            mr[i] = 0xFF0080FF;
        ComPtr<ID3D12Resource> tex, up;
        UploadTex(device, cmdList, tex, up, N, N, mr);
        MakeSrv(device, tex.Get(), srvHeap, 2, srvDescSize);
        textures.push_back(std::move(tex));
        uploads.push_back(std::move(up));
    }

    textureCount = 3; // slots 0-2 reserved for fallbacks

    std::string dir = DirOf(path);
    std::unordered_map<cgltf_image*, UINT> texCache;

    auto loadTex = [&](cgltf_image* img, UINT fallback) -> UINT {
        if (!img || !img->uri) return fallback;
        auto it = texCache.find(img);
        if (it != texCache.end()) return it->second;

        std::string fullPath = dir + img->uri;
        int w, h, ch;
        stbi_uc* pixels = stbi_load(fullPath.c_str(), &w, &h, &ch, 4);
        if (!pixels) { texCache[img] = fallback; return fallback; }

        ComPtr<ID3D12Resource> tex, up;
        UploadTex(device, cmdList, tex, up, (UINT)w, (UINT)h, pixels);
        stbi_image_free(pixels);

        UINT idx = textureCount++;
        MakeSrv(device, tex.Get(), srvHeap, idx, srvDescSize);
        textures.push_back(std::move(tex));
        uploads.push_back(std::move(up));
        texCache[img] = idx;
        return idx;
    };

    std::vector<Vertex> verts;
    std::vector<UINT32> inds;

    std::function<void(cgltf_node*)> processNode = [&](cgltf_node* node) {
        if (node->mesh) {
            float mat[16];
            cgltf_node_transform_world(node, mat);
            glm::mat4 world   = glm::make_mat4(mat);
            glm::mat3 normMat = glm::mat3(world);

            for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
                cgltf_primitive& prim = node->mesh->primitives[pi];
                if (prim.type != cgltf_primitive_type_triangles || !prim.indices) continue;

                cgltf_accessor* posAcc = nullptr, *nrmAcc = nullptr,
                              * uvAcc  = nullptr, *tanAcc  = nullptr;
                for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                    auto& a = prim.attributes[ai];
                    if      (a.type == cgltf_attribute_type_position)                 posAcc = a.data;
                    else if (a.type == cgltf_attribute_type_normal)                   nrmAcc = a.data;
                    else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uvAcc  = a.data;
                    else if (a.type == cgltf_attribute_type_tangent)                  tanAcc = a.data;
                }
                if (!posAcc) continue;

                UINT baseVertex = (UINT)verts.size();
                UINT startIndex = (UINT)inds.size();
                UINT vertCount  = (UINT)posAcc->count;
                UINT idxCount   = (UINT)prim.indices->count;

                for (UINT i = 0; i < vertCount; ++i) {
                    Vertex v = {};
                    float p[3] = {}, n[3] = {0,1,0}, uv[2] = {}, t[4] = {1,0,0,1};

                    cgltf_accessor_read_float(posAcc, i, p, 3);
                    glm::vec4 wp = world * glm::vec4(p[0], p[1], p[2], 1.0f);
                    v.position[0] =  wp.x;
                    v.position[1] =  wp.y;
                    v.position[2] = -wp.z; // glTF RH → DX12 LH

                    if (nrmAcc) {
                        cgltf_accessor_read_float(nrmAcc, i, n, 3);
                        glm::vec3 wn = glm::normalize(normMat * glm::vec3(n[0], n[1], n[2]));
                        v.normal[0] =  wn.x;
                        v.normal[1] =  wn.y;
                        v.normal[2] = -wn.z;
                    }
                    if (uvAcc) {
                        cgltf_accessor_read_float(uvAcc, i, uv, 2);
                        v.uv[0] = uv[0];
                        v.uv[1] = uv[1];
                    }
                    if (tanAcc) {
                        cgltf_accessor_read_float(tanAcc, i, t, 4);
                        glm::vec3 wt = glm::normalize(normMat * glm::vec3(t[0], t[1], t[2]));
                        v.tangent[0] =  wt.x;
                        v.tangent[1] =  wt.y;
                        v.tangent[2] = -wt.z;
                        v.tangent[3] =  t[3]; // handedness ±1 preserved
                    }
                    verts.push_back(v);
                }

                for (UINT i = 0; i < idxCount; ++i)
                    inds.push_back((UINT32)cgltf_accessor_read_index(prim.indices, i));

                UINT albedoIdx = 0, normalIdx = 1, mrIdx = 2;
                if (prim.material) {
                    if (prim.material->has_pbr_metallic_roughness) {
                        auto& tv = prim.material->pbr_metallic_roughness.base_color_texture;
                        if (tv.texture && tv.texture->image)
                            albedoIdx = loadTex(tv.texture->image, 0);
                        auto& mv = prim.material->pbr_metallic_roughness.metallic_roughness_texture;
                        if (mv.texture && mv.texture->image)
                            mrIdx = loadTex(mv.texture->image, 2);
                    }
                    auto& nv = prim.material->normal_texture;
                    if (nv.texture && nv.texture->image)
                        normalIdx = loadTex(nv.texture->image, 1);
                }
                drawCalls.push_back({ idxCount, startIndex, (INT)baseVertex, albedoIdx, normalIdx, mrIdx });
            }
        }
        for (cgltf_size i = 0; i < node->children_count; ++i)
            processNode(node->children[i]);
    };

    cgltf_scene* scene = data->scene
        ? data->scene
        : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
    if (scene)
        for (cgltf_size i = 0; i < scene->nodes_count; ++i)
            processNode(scene->nodes[i]);

    cgltf_free(data);

    auto uploadBuf = [&](ComPtr<ID3D12Resource>& res, ComPtr<ID3D12Resource>& up,
                         const void* srcData, UINT64 size, D3D12_RESOURCE_STATES finalState) {
        auto  hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto  bd = CD3DX12_RESOURCE_DESC::Buffer(size);
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&res));

        auto uhp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        device->CreateCommittedResource(&uhp, D3D12_HEAP_FLAG_NONE, &bd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up));

        void* mapped;
        up->Map(0, nullptr, &mapped);
        memcpy(mapped, srcData, (size_t)size);
        up->Unmap(0, nullptr);

        cmdList->CopyBufferRegion(res.Get(), 0, up.Get(), 0, size);
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(res.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, finalState);
        cmdList->ResourceBarrier(1, &barrier);
    };

    UINT64 vbSize = verts.size() * sizeof(Vertex);
    UINT64 ibSize = inds.size()  * sizeof(UINT32);
    uploadBuf(vertexBuffer, vbUpload, verts.data(), vbSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    uploadBuf(indexBuffer,  ibUpload, inds.data(),  ibSize, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    vbv.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vbv.SizeInBytes    = (UINT)vbSize;
    vbv.StrideInBytes  = sizeof(Vertex);

    ibv.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    ibv.SizeInBytes    = (UINT)ibSize;
    ibv.Format         = DXGI_FORMAT_R32_UINT;

    return true;
}

void Scene::Draw(ID3D12GraphicsCommandList* cmdList,
                 ID3D12DescriptorHeap* srvHeap, UINT srvDescSize) const
{
    cmdList->IASetVertexBuffers(0, 1, &vbv);
    cmdList->IASetIndexBuffer(&ibv);
    for (const DrawCall& dc : drawCalls) {
        CD3DX12_GPU_DESCRIPTOR_HANDLE base(srvHeap->GetGPUDescriptorHandleForHeapStart());
        CD3DX12_GPU_DESCRIPTOR_HANDLE albedo = base; albedo.Offset(dc.albedoIdx, srvDescSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE normal = base; normal.Offset(dc.normalIdx, srvDescSize);
        CD3DX12_GPU_DESCRIPTOR_HANDLE mr     = base; mr.Offset(dc.mrIdx,     srvDescSize);
        cmdList->SetGraphicsRootDescriptorTable(1, albedo);
        cmdList->SetGraphicsRootDescriptorTable(2, normal);
        cmdList->SetGraphicsRootDescriptorTable(3, mr);
        cmdList->DrawIndexedInstanced(dc.indexCount, 1, dc.startIndex, dc.baseVertex, 0);
    }
}
