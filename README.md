# MineClone

Bootstrap OpenGL 4.5 voxel-engine foundation (PR-01): window + loop + FPS camera + static cube.

## Build (Windows / Linux)

### Prerequisites
- CMake 3.20+
- C++ compiler with C++20 support
- Internet access for the first configure step (GLFW/GLM fetched via CMake)

### Configure
```bash
cmake -S . -B build
```

### Build
```bash
cmake --build build -config Release
```

### Run
```bash
# From repo root
./build/Mineclone
```

On Windows, run the generated `Mineclone.exe` from the `build/` directory (or from Visual Studio's output directory). The build copies the `shaders/` folder next to the executable automatically.

## Controls
- **W/A/S/D**: Move
- **Mouse**: Look around (FPS camera)
- **ESC**: Release mouse capture
- **Left click**: Re-capture mouse

## Notes
- The executable prints GPU vendor/renderer/version on startup.
- In Debug builds, OpenGL KHR_debug messages are enabled (notifications filtered out).

## Voxel World (PR-02)
- `BlockId` uses `uint16_t` with constants: AIR=0, STONE=1, DIRT=2.
- `CHUNK_SIZE` is 32 (chunks are 32x32x32).
- World-to-chunk conversion uses mathematical floor division/modulo to keep negative coordinates stable:
  - `chunk = floor(world / CHUNK_SIZE)`
  - `local = floor_mod(world, CHUNK_SIZE)` in `[0..CHUNK_SIZE-1]`

## Meshing v1 (PR-03)
- Demo builds a grid of **8x1x8 chunks** centered around the origin.
- CPU mesher emits faces only when the neighbor is AIR.
- Missing neighbor chunks are treated as AIR during meshing so boundary faces are visible.
