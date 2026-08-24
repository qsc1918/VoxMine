#include "camera.hpp"
#include "player.hpp"
#include "raycast.hpp"
#include "renderer.hpp"
#include "save.hpp"
#include "menu.hpp"
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
#include <memory>

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
    std::string menuShot;
    int menuScreen = 1; // Menuscreen::MainMenu
};

static Args parseArgs(int argc, char** argv) {
    Args a;
    a.threads = (int)std::thread::hardware_concurrency();
    if (a.threads < 1) a.threads = 4;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
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
        else if (arg == "--menu-shot") a.menuShot = next();
        else if (arg == "--menu-screen") a.menuScreen = std::stoi(next());
        else if (arg == "--help") {
            printf("Usage: voxmine [--seed N] [--render-dist N] [--threads N] [--pos x,y,z]\n"
                   "  [--screenshot out.png] [--frames N] [--no-vsync] [--no-ui]\n"
                   "  [--time f] [--drive] [--break x,y,z] [--inventory]\n");
        }
    }
    return a;
}

static std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

int main(int argc, char** argv) {
    Args a = parseArgs(argc, argv);
    SetProcessDPIAware();

    Window win;
    if (!win.init(1280, 720, "VoxMine - Vulkan")) { return 1; }

    VkCtx ctx;
    ctx.vsync = !a.noVsync;
    if (!ctx.init(win, 1280, 720)) { printf("Vulkan init failed: %s\n", ctx.lastError.c_str()); return 1; }

    Renderer renderer;
    Menu menu;
    if (!renderer.init(ctx, win, exeDir() + "\\assets", exeDir() + "\\shaders")) { printf("Renderer init failed\n"); return 1; }
    
    if (!menu.init(ctx, win, exeDir() + "\\assets")) { printf("Menu init failed\n"); return 1; }
    

    // ---- game state ----
    enum class GS { MainMenu, SaveSelect, NewWorld, Options, Video, Pause, Play };
    const bool startDirect = !a.screenshot.empty() || a.drive || !a.breakBlock.empty()
                             || a.frames > 0 || !a.posStr.empty() || a.invStart;
    GS gs = startDirect ? GS::Play : GS::MainMenu;

    std::unique_ptr<World> world;
    Player player;
    std::string worldName;
    std::string seedText;
    std::vector<WorldSave> saves;
    Menuscreen optionsReturnTo = Menuscreen::MainMenu;
    int frame = 0;
    int lastW = 0, lastH = 0;
    bool escPrev = false, ePrev = false, in_prevL = false, in_prevR = false;
    auto last = std::chrono::steady_clock::now();

    auto screenFromGS = [](GS g) -> Menuscreen {
        switch (g) {
            case GS::MainMenu: return Menuscreen::MainMenu;
            case GS::SaveSelect: return Menuscreen::SaveSelect;
            case GS::NewWorld: return Menuscreen::NewWorld;
            case GS::Options: return Menuscreen::Options;
            case GS::Video: return Menuscreen::VideoSettings;
            case GS::Pause: return Menuscreen::Pause;
            default: return Menuscreen::None;
        }
    };

    auto applyFirst = [&]() {
        if (a.timeArg >= 0.0f) renderer.setTimeOfDay(a.timeArg);
        if (a.invStart) renderer.setInventoryOpen(true);
    };
    applyFirst();

    // Enter a world. `loadFrom` is a save name (or empty for a fresh world).
    auto enterWorld = [&](uint32_t seed, const std::string& name, const std::string& loadFrom) {
        world = std::make_unique<World>(seed);
        world->startWorkers(a.threads);
        renderer.setWorld(*world);
        worldName = name;
        player = Player();
        player.cam.yaw = a.yaw;
        player.cam.pitch = a.pitch;
        if (!a.posStr.empty()) {
            float x = 0, y = 80, z = 0;
            if (sscanf(a.posStr.c_str(), "%f,%f,%f", &x, &y, &z) >= 1) player.cam.pos = Vec3(x, y, z);
        } else {
            player.cam.pos = Vec3(8.5f, 80.0f, 8.5f);
            world->forceGenerateChunk(0, 0); // generate the spawn column
            int sx = 8, sz = 8;
            for (int y = WORLD_HEIGHT - 1; y >= 0; y--)
                if (blockIsSolid(world->getBlock(sx, y, sz))) { player.cam.pos.y = (float)y + 2.0f + player.eyeHeight; break; }
        }
        if (!loadFrom.empty()) {
            // re-apply saved edits
            applyEdits(*world, loadFrom, savesDir());
            WorldSave info = saveInfo(loadFrom, savesDir());
            if (info.spawnY > 0) player.cam.pos = Vec3(info.spawnX, info.spawnY, info.spawnZ);
        }
        renderer.setInventoryOpen(false);
        win.setCapture(true);
        ShowCursor(FALSE);
        gs = GS::Play;
        
    };

    // settle chunks around the player (for a smooth start)
    auto settle = [&]() {
        auto t0 = std::chrono::steady_clock::now();
        while (true) {
            world->update(player.cam.pos.x, player.cam.pos.z, a.renderDist);
            renderer.uploadChunks(ctx);
            int ready = 0;
            world->forEachChunk([&](std::shared_ptr<Chunk>& c, int, int) { if (c->state.load() >= 2) ready++; });
            if (ready >= (2 * a.renderDist + 1) * (2 * a.renderDist + 1)) break;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count() > 20000) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };

    if (gs == GS::Play) {
        enterWorld(a.seed, "world", std::string());
        
        settle();
        
        if (!a.breakBlock.empty()) {
            int bx = 0, by = 0, bz = 0;
            sscanf(a.breakBlock.c_str(), "%d,%d,%d", &bx, &by, &bz);
            fprintf(stderr, "[main] break (%d,%d,%d)\n", bx, by, bz);
            world->setBlock(bx, by, bz, B_AIR);
        }
        if (!a.screenshot.empty()) {
            for (int i = 0; i < 30 && win.pump(); i++) {
                if (i == 20) renderer.requestScreenshot(a.screenshot);
                Input& in = win.input();
                renderer.render(ctx, player.cam, player, in, 1.0f / 60.0f, (float)a.renderDist, !a.noUI);
                win.endFrame();
            }
            menu.shutdown(ctx);
            renderer.shutdown(ctx);
            world->stopWorkers();
            ctx.shutdown();
            win.shutdown();
            return 0;
        }
    }

    // seed input helper
    auto seedFromText = [&]() -> uint32_t {
        if (seedText.empty()) return (uint32_t)std::chrono::steady_clock::now().time_since_epoch().count();
        try {
            size_t pos;
            unsigned long long v = std::stoull(seedText, &pos);
            if (pos == seedText.size()) return (uint32_t)v;
        } catch (...) {}
        // non-numeric: hash to a seed
        uint32_t h = 2166136261u;
        for (char ch : seedText) { h ^= (uint8_t)ch; h *= 16777619u; }
        return h;
    };

    // unique world name
    int saveSeq = 0;
    auto newWorldName = [&]() -> std::string {
        std::string n = "涓栫晫" + std::to_string(++saveSeq);
        for (auto& s : saves) if (s.name == n) n = "涓栫晫" + std::to_string(++saveSeq);
        return n;
    };

    if (!a.menuShot.empty()) {
        Menuscreen ms = (Menuscreen)a.menuScreen;
        MenuData md;
        md.saves = listSaves(savesDir());
        md.vsync = ctx.vsync;
        md.seedText = "123456";
        menu.renderMenu(ctx, ms, md, 640.0f, 300.0f, false);
        menu.debugSaveMenu(a.menuShot);
        menu.shutdown(ctx);
        renderer.shutdown(ctx);
        ctx.shutdown();
        win.shutdown();
        return 0;
    }

    while (true) {
        if (!win.pump()) break;
        RECT rc;
        GetClientRect((HWND)win.hwnd(), &rc);
        int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
        if (cw <= 0 || ch <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(16)); continue; }
        if (cw != lastW || ch != lastH) { ctx.recreateSwapchain(cw, ch); menu.onResize(ctx, cw, ch); lastW = cw; lastH = ch; }

        Input& in = win.input();
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        bool esc = in.keys[VK_ESCAPE], escEdge = esc && !escPrev;
        bool eKey = in.keys['E'], eEdge = eKey && !ePrev;
        escPrev = esc;
        ePrev = eKey;

        if (gs == GS::Play) {
            if (escEdge) {
                if (renderer.inventoryOpen()) { renderer.setInventoryOpen(false); win.setCapture(true); ShowCursor(FALSE); }
                else { gs = GS::Pause; win.setCapture(false); ShowCursor(TRUE); }
            }
            if (eEdge) {
                bool o = !renderer.inventoryOpen();
                renderer.setInventoryOpen(o);
                if (o) { win.setCapture(false); ShowCursor(TRUE); }
                else { win.setCapture(true); ShowCursor(FALSE); }
            }
            if (a.drive) { in.keys['W'] = true; player.cam.pitch = -0.1f; }

            if (renderer.inventoryOpen()) {
                float ccx, ccy; win.cursorPos(ccx, ccy); renderer.setCursor(ccx, ccy);
            } else {
                float mx, my;
                win.pollMouse(mx, my);
                player.cam.addYaw(-mx * 0.003f);
                player.cam.addPitch(-my * 0.003f);
                
                player.update(in, *world, dt);
                
                RayHit hit = raycastWorld(*world, player.cam.pos, player.cam.forward(), 6.0f);
                if (in.mouse[0] && !in_prevL) {
                    if (hit.hit) {
                        uint8_t t = world->getBlock(hit.x, hit.y, hit.z);
                        if (t != B_AIR && t != B_WATER) world->setBlock(hit.x, hit.y, hit.z, B_AIR);
                    }
                }
                if (in.mouse[1] && !in_prevR) {
                    if (hit.hit) {
                        int px = hit.px, py = hit.py, pz = hit.pz;
                        uint8_t b = renderer.selectedBlock();
                        float feet = player.cam.pos.y - player.eyeHeight;
                        bool inside = !(px + 1 <= player.cam.pos.x - player.halfWidth ||
                                        px >= player.cam.pos.x + player.halfWidth ||
                                        py + 1 <= feet || py >= feet + player.height ||
                                        pz + 1 <= player.cam.pos.z - player.halfWidth ||
                                        pz >= player.cam.pos.z + player.halfWidth);
                        if (!inside) world->setBlock(px, py, pz, b);
                    }
                }
                in_prevL = in.mouse[0];
                in_prevR = in.mouse[1];
            }

            world->update(player.cam.pos.x, player.cam.pos.z, a.renderDist);
            renderer.uploadChunks(ctx);
            renderer.render(ctx, player.cam, player, in, dt, (float)a.renderDist, !a.noUI);

            if (a.frames > 0 && ++frame >= a.frames) break;
        } else {
            // ---- menu screens ----
            if (escEdge) {
                switch (gs) {
                    case GS::SaveSelect: gs = GS::MainMenu; break;
                    case GS::NewWorld: gs = GS::SaveSelect; break;
                    case GS::Options: gs = optionsReturnTo == Menuscreen::Pause ? GS::Pause : GS::MainMenu; break;
                    case GS::Video: gs = GS::Options; break;
                    case GS::Pause: win.setCapture(true); ShowCursor(FALSE); gs = GS::Play; break;
                    case GS::MainMenu: default: break;
                }
            }

            // seed input
            if (gs == GS::NewWorld) {
                for (UINT k : {0x30u,0x31u,0x32u,0x33u,0x34u,0x35u,0x36u,0x37u,0x38u,0x39u,
                               (UINT)'A',(UINT)'B',(UINT)'C',(UINT)'D',(UINT)'E',(UINT)'F'})
                    if (in.keys[k]) seedText += (char)k;
                if (in.keys[VK_SPACE]) seedText += ' ';
                if (in.keys[VK_OEM_MINUS]) seedText += '-';
                if (in.keys[VK_BACK] && !seedText.empty()) seedText.pop_back();
            }

            float cx, cy; win.cursorPos(cx, cy);
            MenuData md;
            md.saves = saves;
            md.seedText = seedText;
            md.vsync = ctx.vsync;
            md.titleText = "鏂板缓涓栫晫";
            int clicked = menu.renderMenu(ctx, screenFromGS(gs), md, cx, cy, in.mouse[0]);

            switch (clicked) {
                case MENU_SINGLEPLAYER: saves = listSaves(savesDir()); gs = GS::SaveSelect; break;
                case MENU_QUIT: goto shutdown_all;
                case MENU_OPTIONS:
                    optionsReturnTo = (gs == GS::Pause) ? Menuscreen::Pause : Menuscreen::MainMenu;
                    gs = GS::Options; break;
                case MENU_NEWWORLD: seedText.clear(); gs = GS::NewWorld; break;
                case MENU_SAVEANDTITLE:
                    saveWorld(*world, player, worldName, savesDir());
                    world->stopWorkers();
                    world.reset();
                    gs = GS::MainMenu; win.setCapture(false); ShowCursor(TRUE);
                    break;
                case MENU_CREATE: {
                    uint32_t seed = seedFromText();
                    std::string name = newWorldName();
                    enterWorld(seed, name, std::string());
                    settle();
                    saveWorld(*world, player, name, savesDir()); // create the save entry
                    break;
                }
                case MENU_CANCEL: gs = GS::SaveSelect; break;
                case MENU_VIDEO: gs = GS::Video; break;
                case MENU_BACK:
                    if (gs == GS::SaveSelect) gs = GS::MainMenu;
                    else if (gs == GS::Options) gs = optionsReturnTo == Menuscreen::Pause ? GS::Pause : GS::MainMenu;
                    else if (gs == GS::Video) gs = GS::Options;
                    break;
                case MENU_VSYNC:
                    ctx.vsync = !ctx.vsync;
                    ctx.recreateSwapchain(cw, ch);
                    break;
                default:
                    // save-list buttons
                    if (clicked >= MENU_SAVE_FIRST && clicked < MENU_SAVE_FIRST + (int)saves.size() && gs == GS::SaveSelect) {
                        int i = clicked - MENU_SAVE_FIRST;
                        enterWorld(saves[i].seed, saves[i].name, saves[i].name);
                        settle();
                    }
                    break;
            }
        }

        if (frame % 60 == 0) {
            char title[128];
            snprintf(title, sizeof(title), "VoxMine - %s", gs == GS::Play ? "娓告垙" : "鑿滃崟");
            SetWindowTextA((HWND)win.hwnd(), title);
        }
        win.endFrame();
        if (a.frames > 0 && ++frame >= a.frames) break;
    }

shutdown_all:
    win.setCapture(false);
    if (world) world->stopWorkers();
    menu.shutdown(ctx);
    renderer.shutdown(ctx);
    ctx.shutdown();
    win.shutdown();
    return 0;
}
