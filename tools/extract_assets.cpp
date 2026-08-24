// extract_assets.cpp
// Small utility: pick a Minecraft .jar and extract the block + GUI textures this game
// needs into assets/ next to the executable.
//
// Build:
//   g++ tools/extract_assets.cpp -o extract_assets.exe -lcomdlg32
// Run:
//   extract_assets.exe [path/to/minecraft.jar]
//
// Extracted files are Minecraft assets and must NOT be redistributed (Mojang EULA).

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// (path inside the jar, destination relative path under assets/)
struct AssetEntry {
    const char* src;
    const char* dst;
};

static const AssetEntry kAssets[] = {
    // block textures (assets/block)
    {"assets/minecraft/textures/block/grass_block_top.png",       "block/grass_block_top.png"},
    {"assets/minecraft/textures/block/grass_block_side.png",       "block/grass_block_side.png"},
    {"assets/minecraft/textures/block/dirt.png",                   "block/dirt.png"},
    {"assets/minecraft/textures/block/stone.png",                  "block/stone.png"},
    {"assets/minecraft/textures/block/bedrock.png",                "block/bedrock.png"},
    {"assets/minecraft/textures/block/cobblestone.png",            "block/cobblestone.png"},
    {"assets/minecraft/textures/block/oak_planks.png",             "block/oak_planks.png"},
    {"assets/minecraft/textures/block/oak_log.png",                "block/oak_log.png"},
    {"assets/minecraft/textures/block/oak_log_top.png",            "block/oak_log_top.png"},
    {"assets/minecraft/textures/block/oak_leaves.png",             "block/oak_leaves.png"},
    {"assets/minecraft/textures/block/sand.png",                   "block/sand.png"},
    {"assets/minecraft/textures/block/gravel.png",                 "block/gravel.png"},
    {"assets/minecraft/textures/block/coal_ore.png",               "block/coal_ore.png"},
    {"assets/minecraft/textures/block/iron_ore.png",               "block/iron_ore.png"},
    {"assets/minecraft/textures/block/gold_ore.png",               "block/gold_ore.png"},
    {"assets/minecraft/textures/block/diamond_ore.png",            "block/diamond_ore.png"},
    {"assets/minecraft/textures/block/redstone_ore.png",           "block/redstone_ore.png"},
    {"assets/minecraft/textures/block/water_still.png",            "block/water_still.png"},
    {"assets/minecraft/textures/block/snow.png",                   "block/snow.png"},
    {"assets/minecraft/textures/block/glass.png",                  "block/glass.png"},
    {"assets/minecraft/textures/block/white_concrete.png",         "block/white_concrete.png"},
    // GUI textures (assets/gui)
    {"assets/minecraft/textures/gui/sprites/widget/button.png",               "gui/button.png"},
    {"assets/minecraft/textures/gui/sprites/widget/button_highlighted.png",   "gui/button_highlighted.png"},
    {"assets/minecraft/textures/gui/sprites/widget/text_field.png",           "gui/text_field.png"},
};

static std::string pickJarFile() {
    char buf[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Minecraft jar\0*.jar\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Select a Minecraft jar";
    if (!GetOpenFileNameA(&ofn)) return "";
    return std::string(buf);
}

static std::string outputRoot() {
    // write assets/ next to this executable (place extract_assets.exe in the project root)
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

static bool runTar(const std::string& jar, const std::string& outDir) {
    std::string cmd = "tar -xf \"" + jar + "\" -C \"" + outDir
                      + "\" assets/minecraft/textures/block assets/minecraft/textures/gui/sprites/widget 2>nul";
    return system(cmd.c_str()) == 0;
}

int main(int argc, char** argv) {
    std::string jar = argc > 1 ? argv[1] : pickJarFile();
    if (jar.empty()) {
        printf("No jar selected.\n");
        return 1;
    }
    if (!fs::exists(jar)) {
        printf("File not found: %s\n", jar.c_str());
        return 1;
    }

    bool haveOldTextField = false;
    {
        // check which text_field path exists in this jar
        std::string chk = "tar -tf \"" + jar + "\" 2>nul | findstr /C:\"textures/gui/sprites/widget/text_field.png\" >nul";
        haveOldTextField = system(chk.c_str()) == 0;
        (void)haveOldTextField;
    }

    fs::path tmp = fs::temp_directory_path() / "voxmine_assets";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    printf("Extracting from %s ...\n", jar.c_str());
    if (!runTar(jar, tmp.string())) {
        printf("tar extraction failed. Is bsdtar available? (Win10+ ships tar.exe)\n");
        fs::remove_all(tmp);
        return 1;
    }

    fs::path dstRoot = fs::path(outputRoot()) / "assets";
    int copied = 0, missing = 0;
    for (const AssetEntry& e : kAssets) {
        fs::path src = tmp / e.src;
        fs::path dst = dstRoot / e.dst;
        fs::create_directories(dst.parent_path());
        if (!fs::exists(src)) {
            // fallback: the dark text field path
            fs::path alt = tmp / "assets/minecraft/textures/gui/sprites/widget/text_field.png";
            if (std::string(e.src).find("text_field") != std::string::npos && fs::exists(alt)) {
                src = alt;
            } else {
                printf("  MISSING: %s\n", e.src);
                missing++;
                continue;
            }
        }
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        copied++;
    }

    printf("Done: %d resources copied to %s, %d missing.\n", copied,
           dstRoot.string().c_str(), missing);
    fs::remove_all(tmp);
    return 0;
}
