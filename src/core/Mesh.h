/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "AthertaMath.h"

#include <cstdint>
#include <string>
#include <vector>

namespace atherta {

/** A run of triangles sharing one material. */
struct MeshPart {
    std::string material;
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
};

/**
 * Triangulated, indexed mesh in world space (Z-up, metres, like Blender).
 *
 * Tangents follow the MikkTSpace convention (xyz = tangent, w = bitangent
 * sign) so tangent space normal maps match Blender, Substance and game
 * engines.
 */
struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<Vec4> tangents;
    std::vector<uint32_t> indices;
    std::vector<MeshPart> parts;

    Vec3 boundsMin;
    Vec3 boundsMax;
    /** False when the source had no UV coordinates (painting is not possible). */
    bool hasUVs = false;
    /** True when some UVs are outside the 0..1 tile (UDIM or wrapped UVs). */
    bool uvsOutsideUnitSquare = false;

    size_t vertexCount() const { return positions.size(); }
    size_t triangleCount() const { return indices.size() / 3; }
    bool isEmpty() const { return indices.empty(); }
    void clear() { *this = Mesh(); }
};

/** One triangle corner before indexing. */
struct MeshCorner {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec4 tangent;
};

/**
 * Builds a Mesh from unindexed triangle corners (3 per triangle).
 * Computes MikkTSpace tangents when @p computeTangents is set and UVs exist,
 * merges identical corners and updates the bounds.
 * @p partStarts gives the first corner of each part (sorted), with names.
 */
void buildMeshFromCorners(std::vector<MeshCorner> corners,
                          const std::vector<MeshPart> &cornerParts,
                          bool hasUVs,
                          bool computeTangents,
                          Mesh &out);

struct MeshLoadResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
};

/**
 * Loads an .obj or .fbx file. All mesh instances are merged and transformed
 * to world space. OBJ files are assumed to be Y-up (Blender's OBJ default);
 * FBX files carry their own axes and units, which are converted to Z-up metres.
 */
MeshLoadResult loadMesh(const std::string &path, Mesh &out);

} // namespace atherta
