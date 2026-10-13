/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "Math.h"
#include "Mesh.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace atherta {

struct RayHit {
    bool hit = false;
    float t = std::numeric_limits<float>::max();
    uint32_t triangle = 0;
    /** Barycentric weights of the 2nd and 3rd triangle corners. */
    float b1 = 0.0f;
    float b2 = 0.0f;
    Vec3 position;
    /** Interpolated, normalised shading normal. */
    Vec3 normal;
    Vec2 uv;
};

/**
 * Bounding volume hierarchy over a Mesh's triangles, built with binned SAH.
 * Used for picking: brush cursor placement, "view selected" under the cursor
 * and similar queries. The mesh must outlive the BVH.
 */
class MeshBvh
{
public:
    void build(const Mesh &mesh);
    void clear();
    bool isEmpty() const { return m_nodes.empty(); }

    RayHit intersect(const Ray &ray, float tMax = std::numeric_limits<float>::max()) const;

    size_t nodeCount() const { return m_nodes.size(); }

private:
    struct Node {
        Vec3 bmin;
        uint32_t leftOrFirst = 0; // inner: index of left child (right = left + 1); leaf: first tri
        Vec3 bmax;
        uint32_t count = 0;       // > 0 for leaves
    };

    void subdivide(uint32_t nodeIndex);
    void updateBounds(uint32_t nodeIndex);

    const Mesh *m_mesh = nullptr;
    std::vector<Node> m_nodes;
    std::vector<uint32_t> m_triIndices;
    std::vector<Vec3> m_centroids;
    std::vector<Vec3> m_triMin;
    std::vector<Vec3> m_triMax;
};

} // namespace atherta
