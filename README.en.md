# VoxMine — a native-multithreaded Minecraft-like voxel game (Vulkan)

A Minecraft-style sandbox game written in C++20 with a hand-rolled Vulkan renderer and
native worker threads. Content follows early Minecraft (Beta ~1.8 era): grass, dirt,
stone, bedrock, the classic ores (coal/iron/gold/redstone/diamond), oak trees, sand,
gravel, snow biomes and water oceans.

## Building

Requirements: Vulkan SDK (any recent 1.x), CMake >= 3.20, Ninja, a C++20 compiler
(MinGW GCC 12+ or MSVC).

```sh
cmake -G Ninja -S . -B build
cmake --build build
```

The build copies `assets/` (block textures) and compiled shaders next to the exe.

> Note: `assets/` contains textures extracted from a Minecraft jar. Per the Mojang EULA
> these must not be redistributed, so the directory is in `.gitignore`. Use the bundled
> `tools/extract_assets` tool to extract them from your own Minecraft installation.

## Running

```sh
build/voxmine.exe
```

## Controls

| Key | Action |
|-----|--------|
| W / A / S / D | Move |
| Mouse | Look |
| Space | Jump (or ascend while flying) |
| Space (double-tap) | Toggle fly (creative, like vanilla) |
| Shift | Descend while flying |
| E | Open inventory (all blocks, click to select) |
| Left click | Break block (water unbreakable; bedrock breakable in creative) |
| Right click | Place block |
| 1..9 / scroll | Select hotbar slot |
| T | Time-lapse (day/night speed-up) |
| Esc | Pause menu in-game (or close inventory); go back in menus |

The game boots into a title menu: Singleplayer / Options / Quit.
- Singleplayer opens save management (new worlds are infinite).
- Options → Video settings (vsync toggle).
- Press Esc in-game for the pause menu (Save & return to title / Options).

Saves are stored as simple text files under `saves/` next to the .exe (seed, spawn,
block edits); loading regenerates the world from the seed and re-applies edits.

## Command line options

```
--seed N          world seed (default 1337)
--render-dist N   chunk render distance (default 8)
--threads N       worker thread count (default = hardware concurrency)
--pos x,y,z       spawn position
--yaw F --pitch F camera orientation (radians)
--time F          starting time of day, 0..1 (0.25 = morning, 0.5 = night)
--screenshot out.png   render a few frames then save a screenshot and exit
--frames N        run N frames then exit
--no-ui           hide crosshair/hotbar/block highlight
--no-vsync        disable vertical sync (uncapped frame rate)
--inventory       (debug) start with the inventory screen open
--drive           (debug) auto-walk forward
--break x,y,z     (debug) break one block before rendering
```

## Architecture

- `src/world/` terrain generation, chunk storage, worker pool (`World`), meshing.
  Terrain is generated and meshed by a pool of native `std::thread`s; generation is
  scheduled nearest-first and meshing happens once a chunk and its neighbors exist.
  Block data edits are guarded by a shared mutex; meshers copy the 5-chunk border
  region under a read lock.
- `src/renderer.cpp` Vulkan pipelines (opaque terrain, translucent water, sky gradient,
  wireframe block highlight, screen-space UI), a mip-mapped block-texture atlas, and
  per-chunk vertex/index buffers (host-visible, map + memcpy upload).
- `src/vk.cpp` device/swapchain/render-pass setup via volk (dynamic loader, no static
  vulkan lib needed).
- `shaders/` GLSL compiled with glslc at build time.

Textures are extracted from a Minecraft installation's jar (`assets/minecraft/textures/block`).
Modern MC ships some tintable (grayscale) textures (grass top, leaves, water); these are
tinted at atlas build time.

## Asset extraction tool

`tools/extract_assets.cpp`: a small utility that lets you pick an official Minecraft `.jar`
and extracts the block textures the game needs into `assets/block/`. Build & run:

```sh
g++ tools/extract_assets.cpp -o extract_assets.exe -lcomdlg32
extract_assets.exe
```

A Chinese version of this document is available in `README.md`.
