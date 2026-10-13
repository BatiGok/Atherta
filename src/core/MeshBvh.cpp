/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "MeshBvh.h"

#include <array>

namespace atherta {

namespace {

constexpr int kBins = 12;
constexpr uint32_t kMaxLeafSize = 4;

float surfaceArea(const Vec3 &bmin, const Vec3 &bmax)
{
    const Vec3 e = bmax - bmin;
    if (e.x < 0.0f) {
        return 0.0f;
    }
    return 2.0f * (e.x * e.y + e.y * e.z + e.z * e.x);
}

struct Bin {
    Vec3 bmin{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max()};
    Vec3 bmax{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
              -std::numeric_limits<float>::max()};
    uint32_t count = 0;

    void grow(const Vec3 &mn, const Vec3 &mx)
    {
        bmin = componentMin(bmin, mn);
        bmax = componentMax(bmax, mx);
    }
};

/** Slab test. Returns entry distance or +inf when the box is missed. */
float intersectBox(const Ray &ray, const Vec3 &invDir, const Vec3 &bmin, const Vec3 &bmax,
                   float tMax)
{
    float t1 = (bmin.x - ray.origin.x) * invDir.x;
    float t2 = (bmax.x - ray.origin.x) * invDir.x;
    float tmin = std::min(t1, t2);
    float tmax = std::max(t1, t2);
    t1 = (bmin.y - ray.origin.y) * invDir.y;
    t2 = (bmax.y - ray.origin.y) * invDir.y;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    t1 = (bmin.z - ray.origin.z) * invDir.z;
    t2 = (bmax.z - ray.origin.z) * invDir.z;
    tmin = std::max(tmin, std::min(t1, t2));
    tmax = std::min(tmax, std::max(t1, t2));
    if (tmax >= tmin && tmax > 0.0f && tmin < tMax) {
        return tmin;
    }
    return std::numeric_limits<float>::max();
}

float safeInverse(float v)
{
    constexpr float kTiny = 1e-20f;
    if (std::fabs(v) < kTiny) {
        return v < 0.0f ? -1.0f / kTiny : 1.0f / kTiny;
    }
    return 1.0f / v;
}

} // namespace

void MeshBvh::clear()
{
    m_mesh = nullptr;
    m_nodes.clear();
    m_triIndices.clear();
    m_centroids.clear();
    m_triMin.clear();
    m_triMax.clear();
}

void MeshBvh::build(const Mesh &mesh)
{
    clear();
    m_mesh = &mesh;
    const uint32_t triCount = static_cast<uint32_t>(mesh.triangleCount());
    if (triCount == 0) {
        return;
    }

    m_triIndices.resize(triCount);
    m_centroids.resize(triCount);
    m_triMin.resize(triCount);
    m_triMax.resize(triCount);
    for (uint32_t i = 0; i < triCount; ++i) {
        const Vec3 &a = mesh.positions[mesh.indices[i * 3 + 0]];
        const Vec3 &b = mesh.positions[mesh.indices[i * 3 + 1]];
        const Vec3 &c = mesh.positions[mesh.indices[i * 3 + 2]];
        m_triIndices[i] = i;
        m_triMin[i] = componentMin(a, componentMin(b, c));
        m_triMax[i] = componentMax(a, componentMax(b, c));
        m_centroids[i] = (a + b + c) / 3.0f;
    }

    m_nodes.reserve(static_cast<size_t>(triCount) * 2);
    m_nodes.emplace_back();
    m_nodes[0].leftOrFirst = 0;
    m_nodes[0].count = triCount;
    updateBounds(0);
    subdivide(0);

    // Only needed while building.
    m_centroids = {};
    m_triMin = {};
    m_triMax = {};
}

void MeshBvh::updateBounds(uint32_t nodeIndex)
{
    Node &node = m_nodes[nodeIndex];
    Bin b;
    for (uint32_t i = 0; i < node.count; ++i) {
        const uint32_t tri = m_triIndices[node.leftOrFirst + i];
        b.grow(m_triMin[tri], m_triMax[tri]);
    }
    node.bmin = b.bmin;
    node.bmax = b.bmax;
}

void MeshBvh::subdivide(uint32_t rootIndex)
{
    std::vector<uint32_t> stack{rootIndex};
    while (!stack.empty()) {
        const uint32_t nodeIndex = stack.back();
        stack.pop_back();
        // Copy: m_nodes may reallocate below.
        const Node node = m_nodes[nodeIndex];
        if (node.count <= kMaxLeafSize) {
            continue;
        }

        // Centroid bounds.
        Vec3 cmin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                  std::numeric_limits<float>::max());
        Vec3 cmax = -cmin;
        for (uint32_t i = 0; i < node.count; ++i) {
            const Vec3 &c = m_centroids[m_triIndices[node.leftOrFirst + i]];
            cmin = componentMin(cmin, c);
            cmax = componentMax(cmax, c);
        }

        // Binned SAH over all three axes.
        int bestAxis = -1;
        int bestSplit = 0;
        float bestCost = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis) {
            const float extent = cmax[axis] - cmin[axis];
            if (extent <= 0.0f) {
                continue;
            }
            std::array<Bin, kBins> bins;
            const float scale = kBins / extent;
            for (uint32_t i = 0; i < node.count; ++i) {
                const uint32_t tri = m_triIndices[node.leftOrFirst + i];
                const int b = std::min(kBins - 1,
                                       static_cast<int>((m_centroids[tri][axis] - cmin[axis]) * scale));
                bins[b].count++;
                bins[b].grow(m_triMin[tri], m_triMax[tri]);
            }
            std::array<float, kBins - 1> leftArea, rightArea;
            std::array<uint32_t, kBins - 1> leftCount, rightCount;
            Bin l, r;
            uint32_t lc = 0, rc = 0;
            for (int i = 0; i < kBins - 1; ++i) {
                lc += bins[i].count;
                leftCount[i] = lc;
                if (bins[i].count) {
                    l.grow(bins[i].bmin, bins[i].bmax);
                }
                leftArea[i] = surfaceArea(l.bmin, l.bmax);

                const int j = kBins - 1 - i;
                rc += bins[j].count;
                rightCount[j - 1] = rc;
                if (bins[j].count) {
                    r.grow(bins[j].bmin, bins[j].bmax);
                }
                rightArea[j - 1] = surfaceArea(r.bmin, r.bmax);
            }
            for (int i = 0; i < kBins - 1; ++i) {
                if (leftCount[i] == 0 || rightCount[i] == 0) {
                    continue;
                }
                const float cost = leftCount[i] * leftArea[i] + rightCount[i] * rightArea[i];
                if (cost < bestCost) {
                    bestCost = cost;
                    bestAxis = axis;
                    bestSplit = i;
                }
            }
        }

        const float leafCost = node.count * surfaceArea(node.bmin, node.bmax);
        if (bestAxis < 0 || bestCost >= leafCost) {
            continue; // keep as leaf
        }

        // Partition in place.
        const float extent = cmax[bestAxis] - cmin[bestAxis];
        const float splitPos = cmin[bestAxis] + extent * (bestSplit + 1) / kBins;
        uint32_t i = node.leftOrFirst;
        uint32_t j = i + node.count - 1;
        while (i <= j) {
            if (m_centroids[m_triIndices[i]][bestAxis] < splitPos) {
                ++i;
            } else {
                std::swap(m_triIndices[i], m_triIndices[j]);
                if (j == 0) {
                    break;
                }
                --j;
            }
        }
        const uint32_t leftCount = i - node.leftOrFirst;
        if (leftCount == 0 || leftCount == node.count) {
            continue; // degenerate split, keep as leaf
        }

        const uint32_t left = static_cast<uint32_t>(m_nodes.size());
        m_nodes.emplace_back();
        m_nodes.emplace_back();
        m_nodes[left].leftOrFirst = node.leftOrFirst;
        m_nodes[left].count = leftCount;
        m_nodes[left + 1].leftOrFirst = i;
        m_nodes[left + 1].count = node.count - leftCount;
        m_nodes[nodeIndex].leftOrFirst = left;
        m_nodes[nodeIndex].count = 0;
        updateBounds(left);
        updateBounds(left + 1);
        stack.push_back(left);
        stack.push_back(left + 1);
    }
}

RayHit MeshBvh::intersect(const Ray &ray, float tMax) const
{
    RayHit hit;
    if (m_nodes.empty() || !m_mesh) {
        return hit;
    }
    hit.t = tMax;
    const Vec3 invDir(safeInverse(ray.direction.x), safeInverse(ray.direction.y),
                      safeInverse(ray.direction.z));
    const Mesh &mesh = *m_mesh;

    uint32_t stack[64];
    int sp = 0;
    if (intersectBox(ray, invDir, m_nodes[0].bmin, m_nodes[0].bmax, hit.t) ==
        std::numeric_limits<float>::max()) {
        return RayHit();
    }
    stack[sp++] = 0;

    while (sp > 0) {
        const Node &node = m_nodes[stack[--sp]];
        if (node.count > 0) {
            for (uint32_t k = 0; k < node.count; ++k) {
                const uint32_t tri = m_triIndices[node.leftOrFirst + k];
                const Vec3 &p0 = mesh.positions[mesh.indices[tri * 3 + 0]];
                const Vec3 &p1 = mesh.positions[mesh.indices[tri * 3 + 1]];
                const Vec3 &p2 = mesh.positions[mesh.indices[tri * 3 + 2]];
                // Möller-Trumbore, double sided.
                const Vec3 e1 = p1 - p0;
                const Vec3 e2 = p2 - p0;
                const Vec3 pv = cross(ray.direction, e2);
                const float det = dot(e1, pv);
                if (std::fabs(det) < 1e-12f) {
                    continue;
                }
                const float invDet = 1.0f / det;
                const Vec3 tv = ray.origin - p0;
                const float u = dot(tv, pv) * invDet;
                if (u < 0.0f || u > 1.0f) {
                    continue;
                }
                const Vec3 qv = cross(tv, e1);
                const float v = dot(ray.direction, qv) * invDet;
                if (v < 0.0f || u + v > 1.0f) {
                    continue;
                }
                const float t = dot(e2, qv) * invDet;
                if (t > 0.0f && t < hit.t) {
                    hit.hit = true;
                    hit.t = t;
                    hit.triangle = tri;
                    hit.b1 = u;
                    hit.b2 = v;
                }
            }
            continue;
        }

        const uint32_t l = node.leftOrFirst;
        const uint32_t r = l + 1;
        float dl = intersectBox(ray, invDir, m_nodes[l].bmin, m_nodes[l].bmax, hit.t);
        float dr = intersectBox(ray, invDir, m_nodes[r].bmin, m_nodes[r].bmax, hit.t);
        uint32_t nearChild = l, farChild = r;
        if (dr < dl) {
            std::swap(dl, dr);
            std::swap(nearChild, farChild);
        }
        // Push far first so the near child is visited first.
        if (dr != std::numeric_limits<float>::max() && sp < 64) {
            stack[sp++] = farChild;
        }
        if (dl != std::numeric_limits<float>::max() && sp < 64) {
            stack[sp++] = nearChild;
        }
    }

    if (hit.hit) {
        const uint32_t i0 = mesh.indices[hit.triangle * 3 + 0];
        const uint32_t i1 = mesh.indices[hit.triangle * 3 + 1];
        const uint32_t i2 = mesh.indices[hit.triangle * 3 + 2];
        const float b0 = 1.0f - hit.b1 - hit.b2;
        hit.position = ray.origin + ray.direction * hit.t;
        hit.normal = normalized(mesh.normals[i0] * b0 + mesh.normals[i1] * hit.b1 +
                                mesh.normals[i2] * hit.b2);
        hit.uv = mesh.uvs[i0] * b0 + mesh.uvs[i1] * hit.b1 + mesh.uvs[i2] * hit.b2;
    }
    return hit;
}

} // namespace atherta
