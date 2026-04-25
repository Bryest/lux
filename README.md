# lux

![demo](media/lux-cubes.gif)

Real-time DirectX 12 renderer written from scratch in C++. Built as a learning project and portfolio piece toward a graphics programmer role.

**Author:** Renato Castillo — started 2026-04-20

---

## What it does (current state)

- Win32 window with full resize support
- D3D12 device, double-buffered swap chain, depth buffer, and graphics PSO
- 4 colored cubes rotating in real time, each with its own MVP constant buffer
- HLSL vertex + pixel shaders compiled at runtime via `d3dcompiler`
- Free-fly camera: WASD movement, right-click mouse look, scroll wheel speed
- D3D12 debug layer enabled in Debug builds
- Math via [GLM](https://github.com/g-truc/glm) (fetched automatically by CMake)

---

## Requirements

- Windows 10 or 11
- Visual Studio 2022 with the **Desktop development with C++** workload (includes Windows SDK)
- CMake 3.20+
- A GPU that supports Direct3D 12 Feature Level 12.0

---

## Build

```bat
cmake -S . -B out\build\x64-Release -DCMAKE_BUILD_TYPE=Release
cmake --build out\build\x64-Release --config Release
```

Or open the folder directly in Visual Studio 2022 — it picks up `CMakeLists.txt` automatically.

GLM is fetched by CMake on first configure; no manual dependency install needed.

---

## Run

```bat
out\build\x64-Release\lux.exe
```

`shaders.hlsl` is copied next to the executable automatically as a post-build step.

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
- [ ] Load .obj 3D model
- [ ] Texturing
- [ ] Basic Phong lighting
- [ ] Multiple lights + directional shadow maps
- [ ] Load .gltf model (Sponza)
- [ ] PBR (Physically Based Rendering)
- [ ] Post-processing (SSAO, bloom), skybox
- [ ] Public showcase with screenshots + recording
