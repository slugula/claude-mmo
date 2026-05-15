# client-native

Standalone native OpenGL 4.6 Core client for the OSRS-inspired MMO prototype.
Replaces the browser-based Babylon.js client. The Node.js/TypeScript server
under `../server/` is unchanged.

See `../.claude/plans/huge-architectural-refactor-move-glittery-wall.md` for the
full migration plan.

## Prerequisites (one-time)

1. **Visual Studio 2022** with the "Desktop development with C++" workload
   (or the standalone Build Tools for VS 2022).
2. **CMake ≥ 3.25** — bundled with VS 2022, or install separately.
3. **vcpkg** — clone and bootstrap once:
   ```
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg
   ```
   Open a new shell after `setx` so `VCPKG_ROOT` is in the environment.

## Configure & build

From this directory:

```
cmake --preset windows
cmake --build --preset windows-debug
```

The first configure will take a few minutes while vcpkg fetches and builds GLFW,
GLAD, GLM, and Dear ImGui. Subsequent builds are fast.

The executable lands at `build/Debug/client-native.exe` (or `Release/`).

Run from File Explorer or:

```
build\Debug\client-native.exe
```

## Verifying Phase 0

A window opens (1280×720), sky-blue clear color, the Dear ImGui demo window
visible. ESC closes the window. Resizing works.

## Layout

```
client-native/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── shaders/        ← GLSL programs (Phase 1+)
├── assets/         ← worldMap.json + glTF models (Phase 1+)
└── src/
    ├── main.cpp
    ├── app/        ← App, Window
    └── render/     ← MSAA framebuffer, GL debug
```
