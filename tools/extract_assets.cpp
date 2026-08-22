// extract_assets.cpp
// Small utility: pick a Minecraft .jar and extract the block textures this game
// needs into assets/block/ next to the executable.
//
// Build:
//   g++ tools/extract_assets.cpp -o extract_assets.exe -lcomdlg32 -lshlwapi
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

static const char* kNeededFiles[] = {
    "grass_block_top.png",
    "grass_block_side.png",
    "dirt.png",
    "stone.png",
    "bedrock.png",
    "cobblestone.png",
    "oak_planks.png",
    "oak_log.png",
    "oak_log_top.png",
    "oak_leaves.png",
    "sand.png",
    "gravel.png",
    "coal_ore.png",
    "iron_ore.png",
    "gold_ore.png",
    "diamond_ore.png",
    "redstone_ore.png",
    "water_still.png",
    "snow.png",
    "glass.png",
    "white_concrete.png",
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

static std::string exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    size_t pos = p.find_last_of("\\/");
    return pos == std::string::npos ? "." : p.substr(0, pos);
}

static bool runTar(const std::string& jar, const std::string& outDir) {
    // bsdtar (bundled with Windows 10+) can read zip/jar containers.
    std::string cmd = "tar -xf \"" + jar + "\" -C \"" + outDir + "\" assets/minecraft/textures/block 2>nul";
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

    fs::path tmp = fs::temp_directory_path() / "voxmine_assets";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    printf("Extracting from %s ...\n", jar.c_str());
    if (!runTar(jar, tmp.string())) {
        printf("tar extraction failed. Is bsdtar available? (Win10+ ships tar.exe)\n");
        fs::remove_all(tmp);
        return 1;
    }

    fs::path srcRoot = tmp / "assets" / "minecraft" / "textures" / "block";
    fs::path dstDir = fs::path(exeDir()) / "assets" / "block";
    fs::create_directories(dstDir);

    int copied = 0, missing = 0;
    for (const char* f : kNeededFiles) {
        fs::path src = srcRoot / f;
        if (!fs::exists(src)) {
            printf("  MISSING: %s\n", f);
            missing++;
            continue;
        }
        fs::copy_file(src, dstDir / f, fs::copy_options::overwrite_existing);
        copied++;
    }

    printf("Done: %d textures copied to %s, %d missing.\n", copied,
           dstDir.string().c_str(), missing);
    fs::remove_all(tmp);
    return 0;
}
