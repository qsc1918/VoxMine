#include "mesher.hpp"
#include "blocks.hpp"
#include <algorithm>
#include <cmath>

namespace {

// precomputed face tables. Index order MUST match the Face enum:
// F_PX=0, F_NX=1, F_PY=2, F_NY=3, F_PZ=4, F_NZ=5
const int kC[6][4][3] = {
    {{1,0,1},{1,0,0},{1,1,0},{1,1,1}}, // +X
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}, // -X
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}}, // +Y
    {{1,0,1},{0,0,1},{0,0,0},{1,0,0}}, // -Y  (winding reversed so it renders from below)
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}, // +Z
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}, // -Z
};
const int kU[6][4] = {
    {15,0,0,15}, {0,15,15,0}, {0,15,15,0}, {15,0,0,15}, {0,15,15,0}, {15,0,0,15},
};
const int kV[6][4] = {
    {15,15,0,0}, {15,15,0,0}, {15,15,0,0}, {15,15,0,0}, {15,15,0,0}, {15,15,0,0},
};
const int kA1[6] = {2,2,0,0,0,0};
const int kA2[6] = {1,1,2,2,1,1};
const int kNormal[6][3] = {
    {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},
};

// whether a face of block `me` facing neighbor `nb` should be culled
inline bool cullFace(uint8_t me, uint8_t nb) {
    if (me == B_GLASS && nb == B_GLASS) return true;
    if (me == B_LEAVES && nb == B_LEAVES) return true;
    if (blockIsOpaque(nb)) return true;
    return false;
}

inline bool waterCull(uint8_t nb) {
    return nb == B_WATER || blockIsOpaque(nb);
}

const float kAoBright[4] = {0.42f, 0.64f, 0.82f, 1.0f};
const float kFaceBright[6] = {0.80f, 0.80f, 1.0f, 0.55f, 0.80f, 0.80f};
const Vec3 kSun(0.42f, 0.82f, 0.32f);

// Precomputed shade lookup table: kShade[face][ao][water]
uint8_t kShade[6][4][2];
struct ShadeInit { ShadeInit() {
    for (int f = 0; f < 6; f++) {
        const Vec3& nrm = *reinterpret_cast<const Vec3*>(&kNormal[f][0]);
        float sun = 0.55f + 0.45f * std::max(0.0f, dot(nrm, kSun) * 0.5f + 0.5f);
        for (int a = 0; a < 4; a++) {
            float br = kFaceBright[f] * kAoBright[a] * sun;
            kShade[f][a][0] = (uint8_t)(std::min(1.0f, std::max(0.0f, br)) * 255.0f);
            float bw = br * 0.82f;
            kShade[f][a][1] = (uint8_t)(std::min(1.0f, std::max(0.0f, bw)) * 255.0f);
        }
    }
} } shadeInit;

inline uint8_t bakeShade(int face, int ao, bool water) {
    return kShade[face][ao][water ? 1 : 0];
}

} // namespace

ChunkMeshData buildChunkMesh(const MeshView& view) {
    ChunkMeshData m;
    auto& ov = m.opaqueVerts;
    auto& oi = m.opaqueIdx;
    auto& wv = m.waterVerts;
    auto& wi = m.waterIdx;
    ov.reserve(8192);
    oi.reserve(12288);
    wv.reserve(512);
    wi.reserve(768);

    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int z = 0; z < 16; z++) {
            for (int x = 0; x < 16; x++) {
                uint8_t id = view.at(x, y, z);
                if (id == B_AIR) continue;

                if (id == B_WATER) {
                    // only emit top face — side/bottom faces are invisible
                    // through the semi-transparent surface and cause dark artifacts
                    if (waterCull(view.at(x, y + 1, z))) continue;
                    {
                        int face = F_PY;
                        uint32_t base = (uint32_t)wv.size();
                        for (int c = 0; c < 4; c++) {
                            TerrainVertex vt;
                            vt.x = (int8_t)(x + kC[face][c][0]);
                            vt.y = (int8_t)(y + kC[face][c][1]);
                            vt.z = (int8_t)(z + kC[face][c][2]);
                            vt.u = (uint8_t)kU[face][c];
                            vt.v = (uint8_t)kV[face][c];
                            vt.tex = T_WATER;
                            vt.shade = bakeShade(face, 3, true);
                            wv.push_back(vt);
                        }
                        wi.push_back(base);
                        wi.push_back(base + 1);
                        wi.push_back(base + 2);
                        wi.push_back(base);
                        wi.push_back(base + 2);
                        wi.push_back(base + 3);
                    }
                    continue;
                }

                if (!blockIsRenderable(id)) continue;

                // quick reject: fully surrounded
                bool anyExposed = false;
                for (int f = 0; f < 6 && !anyExposed; f++) {
                    int dx = kNormal[f][0], dy = kNormal[f][1], dz = kNormal[f][2];
                    if (!cullFace(id, view.at(x + dx, y + dy, z + dz))) anyExposed = true;
                }
                if (!anyExposed) continue;

                uint8_t tile = blockTile(id, 0);
                for (int f = 0; f < 6; f++) {
                    int dx = kNormal[f][0], dy = kNormal[f][1], dz = kNormal[f][2];
                    uint8_t nb = view.at(x + dx, y + dy, z + dz);
                    if (cullFace(id, nb)) continue;

                    uint8_t fTile = blockTile(id, f);
                    uint32_t base = (uint32_t)ov.size();
                    for (int c = 0; c < 4; c++) {
                        TerrainVertex vt;
                        vt.x = (int8_t)(x + kC[f][c][0]);
                        vt.y = (int8_t)(y + kC[f][c][1]);
                        vt.z = (int8_t)(z + kC[f][c][2]);
                        vt.u = (uint8_t)kU[f][c];
                        vt.v = (uint8_t)kV[f][c];
                        vt.tex = fTile;
                        // AO: neighbors are checked at the corner level (on the face),
                        // not at the block origin. For the top face this means checking
                        // y+1, so flat ground does not self-occlude.
                        int a1 = kA1[f], a2 = kA2[f];
                        int cx = x + kC[f][c][0], cy = y + kC[f][c][1], cz = z + kC[f][c][2];
                        int o1 = kC[f][c][a1] == 1 ? 1 : -1;
                        int o2 = kC[f][c][a2] == 1 ? 1 : -1;
                        int co[3] = {0,0,0};
                        co[a1] = o1;
                        int side1x = cx + co[0], side1y = cy + co[1], side1z = cz + co[2];
                        co[a1] = 0; co[a2] = o2;
                        int side2x = cx + co[0], side2y = cy + co[1], side2z = cz + co[2];
                        co[a1] = o1; co[a2] = o2;
                        int diagx = cx + co[0], diagy = cy + co[1], diagz = cz + co[2];

                        bool s1 = blockIsOpaque(view.at(side1x, side1y, side1z));
                        bool s2 = blockIsOpaque(view.at(side2x, side2y, side2z));
                        bool dd = blockIsOpaque(view.at(diagx, diagy, diagz));
                        int ao;
                        if (s1 && s2) ao = 0;
                        else ao = 3 - ((s1 ? 1 : 0) + (s2 ? 1 : 0) + (dd ? 1 : 0));
                        vt.shade = bakeShade(f, ao, false);
                        ov.push_back(vt);
                    }
                    oi.push_back(base);
                    oi.push_back(base + 1);
                    oi.push_back(base + 2);
                    oi.push_back(base);
                    oi.push_back(base + 2);
                    oi.push_back(base + 3);
                }
            }
        }
    }
    return m;
}
