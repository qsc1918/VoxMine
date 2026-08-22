#include "camera.hpp"
#include "player.hpp"
#include "raycast.hpp"
#include "renderer.hpp"
#include "vk.hpp"
#include "window.hpp"
#include "world.hpp"
#include <windows.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

struct Args {
    uint32_t seed = 1337;
    std::string screenshot;
    std::string posStr;
    std::string breakBlock;
    float yaw = 0.0f, pitch = -0.35f;
    float timeArg = -1.0f;
    int renderDist = 8;
    int threads = 0;
    int frames = 0;
    bool noUI = false;
    bool drive = false;
    bool noVsync = false;
    bool invStart = false;
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    a.threads = (int)std::thread::hardware_concurrency();
    if (a.threads < 1) a.threads = 4;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            return i + 1 < argc ? argv[++i] : std::string();
        };
        if (arg == "--seed") a.seed = (uint32_t)std::stoul(next());
        else if (arg == "--screenshot") a.screenshot = next();
        else if (arg == "--pos") a.posStr = next();
        else if (arg == "--yaw") a.yaw = std::stof(next());
        else if (arg == "--pitch") a.pitch = std::stof(next());
        else if (arg == "--render-dist") a.renderDist = std::stoi(next());
        else if (arg == "--threads") a.threads = std::stoi(next());
        else if (arg == "--frames") a.frames = std::stoi(next());
        else if (arg == "--no-ui") a.noUI = true;
        else if (arg == "--break") a.breakBlock = next();
        else if (arg == "--time") a.timeArg = std::stof(next());
        else if (arg == "--drive") a.drive = true;
        else if (arg == "--no-vsync") a.noVsync = true;
        else if (arg == "--inventory") a.invStart = true;
        else if (arg == "--help") {
            printf("Usage: voxmine [--seed N] [--render-dist N] [--threads N]\n"
                   "  [--pos x,y,z] [--yaw f] [--pitch f] [--screenshot out.png] [--frames N]\n"
                   "  [--no-ui] [--no-vsync] [--time f] [--drive] [--break x,y,z]\n");
        }
    }
    return a;
}

int main(int argc, char** argv) {
    Args a = parseArgs(argc, argv);
    SetProcessDPIAware();

    Window win;
    if (!win.init(1280, 720, "VoxMine - Vulkan")) {
        printf("Failed to create window\n");
        return 1;
    }

    VkCtx ctx;
    ctx.vsync = !a.noVsync;
    if (!ctx.init(win, 1280, 720)) {
        printf("Vulkan init failed: %s\n", ctx.lastError.c_str());
        return 1;
    }

    World world(a.seed);
    world.startWorkers(a.threads);

    Renderer renderer;
    std::string exeDir;
    {
        char buf[MAX_PATH];
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string p(buf);
        size_t pos = p.find_last_of("\\/");
        exeDir = pos == std::string::npos ? "." : p.substr(0, pos);
    }
    if (a.timeArg >= 0.0f) renderer.setTimeOfDay(a.timeArg);
    if (a.invStart) renderer.setInventoryOpen(true);
    if (!renderer.init(ctx, world, win, exeDir + "\\assets", exeDir + "\\shaders")) {
        printf("Renderer init failed\n");
        return 1;
    }

    Player player;
    player.cam.yaw = a.yaw;
    player.cam.pitch = a.pitch;
    if (!a.posStr.empty()) {
        float x = 0, y = 80, z = 0;
        if (sscanf(a.posStr.c_str(), "%f,%f,%f", &x, &y, &z) >= 1) {
            player.cam.pos = Vec3(x, y, z);
        }
    } else {
        player.cam.pos = Vec3(8.5f, 80.0f, 8.5f);
    }

    // settle: generate & mesh all chunks in range before rendering (mostly for screenshots)
    auto settle = [&]() {
        auto t0 = std::chrono::steady_clock::now();
        while (true) {
            world.update(player.cam.pos.x, player.cam.pos.z, a.renderDist);
            renderer.uploadChunks(ctx);
            int ready = 0;
            world.forEachChunk([&](std::shared_ptr<Chunk>& c, int, int) {
                if (c->state.load() >= 2) ready++;
            });
            if (ready >= (2 * a.renderDist + 1) * (2 * a.renderDist + 1)) break;
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0).count();
            if (el > 20000) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    settle();
    {
        int g = 0, m = 0;
        world.forEachChunk([&](std::shared_ptr<Chunk>& c, int, int) {
            if (c->state.load() >= 1) g++;
            if (c->state.load() >= 2) m++;
        });
        fprintf(stderr, "[main] chunks=%zu generated=%d meshed=%d\n", world.chunkCount(), g, m);
    }

    // If no explicit spawn position, drop onto the terrain surface.
    if (a.posStr.empty()) {
        int sx = (int)player.cam.pos.x, sz = (int)player.cam.pos.z;
        for (int y = WORLD_HEIGHT - 1; y >= 0; y--) {
            uint8_t b = world.getBlock(sx, y, sz);
            if (blockIsSolid(b)) {
                player.cam.pos.y = (float)y + 2.0f + player.eyeHeight;
                break;
            }
        }
        fprintf(stderr, "[main] spawned at (%.1f, %.1f, %.1f)\n", player.cam.pos.x,
                player.cam.pos.y, player.cam.pos.z);
    }

    if (!a.breakBlock.empty()) {
        int bx = 0, by = 0, bz = 0;
        sscanf(a.breakBlock.c_str(), "%d,%d,%d", &bx, &by, &bz);
        uint8_t before = world.getBlock(bx, by, bz);
        bool changed = world.setBlock(bx, by, bz, B_AIR);
        fprintf(stderr, "[main] break (%d,%d,%d) before=%d changed=%d\n", bx, by, bz, before, (int)changed);
    }

    if (!a.screenshot.empty()) {
        // render a few frames to warm the swapchain, then capture
        for (int i = 0; i < 30 && win.pump(); i++) {
            if (i == 20) renderer.requestScreenshot(a.screenshot);
            Input& in = win.input();
            renderer.render(ctx, player.cam, player, in, 1.0f / 60.0f, (float)a.renderDist, !a.noUI);
            win.endFrame();
        }
        ctx.shutdown();
        win.shutdown();
        return 0;
    }

    win.setCapture(true);
    ShowCursor(FALSE);

    bool prevL = false, prevR = false;
    bool invKey = false;
    auto last = std::chrono::steady_clock::now();
    int frame = 0;
    int lastW = 0, lastH = 0;

    while (true) {
        if (!win.pump()) break;

        RECT rc;
        GetClientRect((HWND)win.hwnd(), &rc);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        if (cw <= 0 || ch <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }
        if (cw != lastW || ch != lastH) {
            ctx.recreateSwapchain(cw, ch);
            lastW = cw;
            lastH = ch;
        }

        Input& in = win.input();
        if (in.keys[VK_ESCAPE]) break;
        if (a.drive) { in.keys['W'] = true; player.cam.pitch = -0.1f; }

        // E toggles the inventory screen
        if (in.keys['E'] && !invKey) {
            renderer.setInventoryOpen(!renderer.inventoryOpen());
            if (renderer.inventoryOpen()) { win.setCapture(false); ShowCursor(TRUE); }
            else { win.setCapture(true); ShowCursor(FALSE); }
        }
        invKey = in.keys['E'];

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        if (renderer.inventoryOpen()) {
            float ccx, ccy;
            win.cursorPos(ccx, ccy);
            renderer.setCursor(ccx, ccy);
        } else {
            float mx, my;
            win.pollMouse(mx, my);
            player.cam.addYaw(-mx * 0.003f);
            player.cam.addPitch(-my * 0.003f);

            player.update(in, world, dt);

            // block interaction
            RayHit hit = raycastWorld(world, player.cam.pos, player.cam.forward(), 6.0f);
            if (in.mouse[0] && !prevL) {
                if (hit.hit) {
                    uint8_t target = world.getBlock(hit.x, hit.y, hit.z);
                    if (target != B_AIR && target != B_WATER)
                        world.setBlock(hit.x, hit.y, hit.z, B_AIR);
                }
            }
            if (in.mouse[1] && !prevR) {
                if (hit.hit) {
                    int px = hit.px, py = hit.py, pz = hit.pz;
                    uint8_t b = renderer.selectedBlock();
                    // don't place inside the player
                    float feet = player.cam.pos.y - player.eyeHeight;
                    bool inside = !(px + 1 <= player.cam.pos.x - player.halfWidth ||
                                    px >= player.cam.pos.x + player.halfWidth ||
                                    py + 1 <= feet ||
                                    py >= feet + player.height ||
                                    pz + 1 <= player.cam.pos.z - player.halfWidth ||
                                    pz >= player.cam.pos.z + player.halfWidth);
                    if (!inside) world.setBlock(px, py, pz, b);
                }
            }
            prevL = in.mouse[0];
            prevR = in.mouse[1];
        }

        world.update(player.cam.pos.x, player.cam.pos.z, a.renderDist);
        renderer.uploadChunks(ctx);
        renderer.render(ctx, player.cam, player, in, dt, (float)a.renderDist, !a.noUI);

        if (a.frames > 0 && ++frame >= a.frames) break;

        if (frame % 120 == 1) {
            fprintf(stderr, "[main] fps=%.0f pos=(%.1f, %.2f, %.1f) %s\n", renderer.fps(),
                    player.cam.pos.x, player.cam.pos.y, player.cam.pos.z,
                    player.onGround ? "grounded" : "air");
        }

        // fps title
        if (frame % 60 == 0) {
            char title[128];
            snprintf(title, sizeof(title), "VoxMine - FPS %d - chunks %zu", (int)renderer.fps(),
                     world.chunkCount());
            SetWindowTextA((HWND)win.hwnd(), title);
        }

        win.endFrame();
    }

    win.setCapture(false);
    renderer.shutdown(ctx);
    world.stopWorkers();
    ctx.shutdown();
    win.shutdown();
    return 0;
}
