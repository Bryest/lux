# lux

![Sponza with normal maps](media/sponza-normal-maps.png)

Real-time DirectX 12 renderer written from scratch in C++. Built as a learning project and portfolio piece toward a graphics programmer role.

**Author:** Renato Castillo — started 2026-04-20

---

## What it does (current state)

- Win32 window with full resize support
- D3D12 device, double-buffered swap chain, depth buffer, and graphics PSO
- glTF 2.0 scene loading via [cgltf](https://github.com/jkuhlmann/cgltf) — rendering the Khronos Sponza scene (~69 textures, hundreds of draw calls)
- Per-material diffuse textures + normal maps: SRV descriptor heap, two textures bound per draw call
- Normal mapping via TBN matrix in pixel shader — tangent-space normals transformed to world space
- Diffuse texture loading via [stb_image](https://github.com/nothings/stb) — PNG/JPG loaded at runtime
- Phong lighting (ambient + diffuse + specular) with a directional light
- HLSL vertex + pixel shaders compiled at runtime via `d3dcompiler`
- Free-fly camera: WASD movement, right-click mouse look, scroll wheel speed
- D3D12 debug layer enabled in Debug builds
- Math via [GLM](https://github.com/g-truc/glm) — fetched automatically by CMake

---

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload (includes Windows SDK)
- CMake 3.20+
- A GPU that supports Direct3D 12 Feature Level 12.0

---

## Build & Run

```bat
cmake -S . -B out -G "Visual Studio 17 2022" -A x64
cmake --build out --config Debug
out\Debug\lux.exe
```

GLM, tinyobjloader, and cgltf are fetched automatically on first configure. `shaders.hlsl` and the `models/` folder are copied next to the executable as a post-build step.

---

## Controls

| Input | Action |
|-------|--------|
| W / A / S / D | Move camera forward / left / back / right |
| Space | Move camera up |
| Ctrl | Move camera down |
| Right-click + drag | Mouse look |
| Scroll wheel | Adjust movement speed |
| Esc | Quit |

---

## Roadmap

- [x] Win32 window + DX12 device + clear screen
- [x] First triangle
- [x] Indexed cube with vertex colors
- [x] Constant buffer + per-object MVP
- [x] Multiple spinning cubes
- [x] Free camera (WASD + mouse look)
- [x] Load .obj 3D model (Suzanne)
- [x] Diffuse texture mapping (stb_image, SRV heap, runtime PNG/JPG loading)
- [x] Phong lighting (ambient + diffuse + specular)
- [x] Load .gltf model (Sponza) — cgltf, per-material textures, full scene traversal
- [x] Normal mapping — TBN matrix, tangent-space normal maps per material
- [ ] Multiple lights + directional shadow maps
- [ ] PBR (Physically Based Rendering)
- [ ] Post-processing (SSAO, bloom), skybox
- [ ] Public showcase with screenshots + recording
