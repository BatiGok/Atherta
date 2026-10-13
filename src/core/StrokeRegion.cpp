/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "StrokeRegion.h"

#include <cmath>
#include <limits>

namespace atherta {

IRect textureRegionForScreenRect(const Mesh &mesh, const Mat4 &viewProj, int viewportWidth,
                                 int viewportHeight, const IRect &screenRect, int textureWidth,
                                 int textureHeight, int padding)
{
    if (screenRect.isEmpty() || mesh.isEmpty() || !mesh.hasUVs) {
        return IRect();
    }

    // Project every vertex once.
    struct Projected {
        float x, y;
        bool valid;
    };
    std::vector<Projected> proj(mesh.positions.size());
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec4 clip = viewProj * Vec4(mesh.positions[i], 1.0f);
        if (clip.w <= 1e-6f) {
            proj[i] = {0.0f, 0.0f, false};
            continue;
        }
        const float nx = clip.x / clip.w;
        const float ny = clip.y / clip.w;
        proj[i] = {(nx + 1.0f) * 0.5f * static_cast<float>(viewportWidth),
                   (1.0f - ny) * 0.5f * static_cast<float>(viewportHeight), true};
    }

    const float sx0 = static_cast<float>(screenRect.x);
    const float sy0 = static_cast<float>(screenRect.y);
    const float sx1 = static_cast<float>(screenRect.right());
    const float sy1 = static_cast<float>(screenRect.bottom());

    float u0 = std::numeric_limits<float>::max(), v0 = u0;
    float u1 = -u0, v1 = -u0;
    bool any = false;

    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t + 1], i2 = mesh.indices[t + 2];
        const Projected &a = proj[i0], &b = proj[i1], &c = proj[i2];
        bool hit;
        if (!a.valid || !b.valid || !c.valid) {
            // Crosses the camera plane: be conservative.
            hit = a.valid || b.valid || c.valid;
        } else {
            const float minX = std::min({a.x, b.x, c.x});
            const float maxX = std::max({a.x, b.x, c.x});
            const float minY = std::min({a.y, b.y, c.y});
            const float maxY = std::max({a.y, b.y, c.y});
            hit = maxX >= sx0 && minX <= sx1 && maxY >= sy0 && minY <= sy1;
        }
        if (!hit) {
            continue;
        }
        any = true;
        for (uint32_t i : {i0, i1, i2}) {
            const Vec2 &uv = mesh.uvs[i];
            u0 = std::min(u0, uv.x);
            u1 = std::max(u1, uv.x);
            v0 = std::min(v0, uv.y);
            v1 = std::max(v1, uv.y);
        }
    }
    if (!any) {
        return IRect();
    }

    // Row 0 is the top of the texture (V = 1).
    int x0 = static_cast<int>(std::floor(u0 * textureWidth)) - padding - 1;
    int x1 = static_cast<int>(std::ceil(u1 * textureWidth)) + padding + 1;
    int y0 = static_cast<int>(std::floor((1.0f - v1) * textureHeight)) - padding - 1;
    int y1 = static_cast<int>(std::ceil((1.0f - v0) * textureHeight)) + padding + 1;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(textureWidth, x1);
    y1 = std::min(textureHeight, y1);
    if (x1 <= x0 || y1 <= y0) {
        return IRect();
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace atherta
