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
- **W/A/S/D**: Move (physics-driven)
- **Mouse**: Look around (FPS camera)
- **SPACE**: Jump (when grounded)
- **ESC**: Release mouse capture
- **Left click**: Re-capture mouse (when released) / break block (when captured)
- **Right click**: Place block (adjacent to targeted face)
- **R**: Reset player to spawn (debug builds)
- **[ / ]**: Decrease/increase render radius (chunks)
- **, / .**: Decrease/increase load radius (chunks)
- **F1**: Toggle frustum culling
- **F2**: Toggle distance culling
- **F3**: Toggle performance stats in window title
- **F4**: Toggle periodic perf logging to stdout
- **F5**: Force-save all dirty loaded chunks
- **F6**: Toggle streaming (pause/resume)
- **Menu**: The main/pause menu is shown in the window title bar. Press **1** for New/Continue, **2** for Load/Save, **3** to Exit.

## Notes
- The executable prints GPU vendor/renderer/version on startup.
- In Debug builds, OpenGL KHR_debug messages are enabled (notifications filtered out).
- All block textures are authored at **32x32** pixels and stored under `textures/`.

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

## Culling (PR-04)
- Chunk-level frustum culling (AABB vs frustum) and distance culling are enabled by default.
- Render radius defaults to **8 chunks** using a Chebyshev distance in XZ from the camera chunk.
- Window title shows live stats for loaded, drawn, culled chunks, and draw calls.

## Streaming (PR-05)
- Chunks are loaded/unloaded around the player in a square (Chebyshev) radius on the XZ plane (single Y layer).
- **Load radius** defaults to **10 chunks**, **render radius** defaults to **8 chunks** (load radius clamps to render radius).
- Per-frame budgets (defaults): **3** chunk creates, **2** chunk meshes, **3** GPU uploads.
- Window title shows player chunk, loaded/GPU-ready counts, queue sizes, and budget usage.

## Multithreaded Jobs (PR-06)
- Chunk generation and CPU meshing run on worker threads; **all OpenGL work stays on the main thread**.
- Default worker thread count: **2** (`ChunkStreamingConfig::workerThreads`).
- Worker jobs:
  - Generate chunk block data deterministically.
  - Mesh CPU buffers (missing or not-yet-generated neighbors are treated as AIR).
- Main thread uploads meshes to GPU with a per-frame budget.
- Window title now shows generated/meshed/GPU-ready counts, queue sizes, and worker thread count.

## Interaction (PR-07)
- Raycast reach distance: **6 blocks** (voxel DDA).
- Targeted blocks show a highlighted wireframe outline.
- Editing a block remeshes the impacted chunk and any touched neighbors.

## Player Physics (PR-08)
- Player collider: AABB **0.6w x 1.8h x 0.6d** (feet position origin).
- Gravity: **-20.0 m/s²**
- Jump impulse: **+8.0 m/s**
- Move speed: **4.5 m/s** on XZ plane (camera yaw only).

## Profiling + 80/20 Optimizations (PR-09)
- Timings are collected with `std::chrono::steady_clock` on both the main thread and workers.
- Window title (when enabled) shows:
  - **frame / upd / up / rnd**: EMA of frame, update, GPU upload, and render times (ms).
  - **gen / mesh**: average ms per completed worker job with job counts per window.
  - **loaded / gpu / queues / drawn**: streaming and render counts for quick context.
- The optional stdout report prints a one-line summary every ~5s when enabled.

## Chunk Persistence (PR-10)
- Saves are written under `./saves/world_0/` (relative to the executable working directory).
- Each chunk is stored as `chunk_<cx>_<cy>_<cz>.bin` with format version **1**.
- Chunks are saved on unload and when forcing a save with **F5** (also on shutdown).
- Chunks load from disk before falling back to deterministic generation if a valid file exists.
