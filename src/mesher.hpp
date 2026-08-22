#pragma once
#include "world.hpp"

// Builds opaque + water mesh data for a chunk from its local block view.
ChunkMeshData buildChunkMesh(const MeshView& view);
