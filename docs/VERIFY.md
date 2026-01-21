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

The smoke test runs a fixed-length deterministic loop and exits automatically.

```bash
./build-debug/Mineclone --smoke-test
```

In CI or headless setups, disable GL debug output:

```bash
./build-debug/Mineclone --smoke-test --no-gl-debug
```

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
