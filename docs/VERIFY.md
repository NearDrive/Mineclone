# Verification Guide

This project includes deterministic verification checks, a smoke test mode, and optional sanitizers.

## Build (Release + Debug)

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release --config Release

cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug --config Debug
```

## Warnings as Errors (Optional)

```bash
cmake -S . -B build-werror -DCMAKE_BUILD_TYPE=Debug -DMINECLONE_WARNINGS_AS_ERRORS=ON
cmake --build build-werror --config Debug
```

## Sanitizers (Optional, GCC/Clang Debug only)

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DMINECLONE_SANITIZE=ON
cmake --build build-sanitize --config Debug
```

## Smoke Test

The smoke test runs a deterministic menu flow (new world -> pause -> save -> exit to menu -> exit app) and exits automatically. On success it prints `[Smoke] OK: menu flow + save completed`.

```bash
./build-debug/Mineclone --smoke-test
```

In CI or headless setups, disable GL debug output:

```bash
./build-debug/Mineclone --smoke-test --no-gl-debug
```

## Visual Validation

Use these scenarios to validate lighting and shadowing visually. Compare the on-screen result
to the expected light levels listed below and confirm there are no obvious artifacts (banding,
flicker, incorrect shadow edges, or stale lighting after edits).

### Test Scenes

1. **Tall tower in sunlight + shadows**
   - Build a tall, narrow tower in an open area under direct sunlight.
   - Expected: clear light falloff with crisp shadow projection on nearby terrain.

2. **Sealed cave with torches**
   - Create a fully enclosed cave, then place torches inside.
   - Expected: cave interior is dark without torches; torch light provides smooth falloff with
     no light leaking through solid blocks.

3. **Real-time editing (add/remove blocks)**
   - While observing lighting, add and remove blocks near light sources and shadows.
   - Expected: lighting updates immediately, with no lingering bright/dark patches.

4. **Compare against expected light levels**
   - Sample light values (if displayed/debuggable) in bright, mid, and dark regions.
   - Expected: values match the intended lighting model and remain consistent across frames.

### Expected Result

Visual validation is considered successful when lighting and shadows appear consistent and
artifact-free across the scenarios above.

## Menu Controls

Main menu (on startup):

- `1` New World
- `2` Load World
- `3` Exit

Pause menu (press `ESC` while playing):

- `1` Continue
- `2` Save World
- `3` Exit to Main Menu

Saves are stored under `./saves/world_0/`.

## Self-Tests Coverage

Startup verification checks include:

- Voxel coordinate floor division/modulo roundtrips.
- Chunk linear indexing mapping sanity.
- Read-only chunk lookups do not create chunks.
- A basic raycast hit on a known block.
- Border edits schedule remeshes for neighbors.
- Job scheduling avoids duplicate remesh jobs.
- Persistence save/load roundtrip (temp folder).
- Worker pool starts and stops cleanly.
