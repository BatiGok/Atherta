/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "doctest.h"

#include "StrokeRegion.h"
#include "ViewNavigator.h"

using namespace atherta;

namespace {

/** Two quads side by side, facing -Y; the left one uses the left half of UV space. */
Mesh twoQuads()
{
    std::vector<MeshCorner> corners;
    auto quad = [&](float x0, float x1, float u0, float u1) {
        const Vec3 p[4] = {{x0, 0, -1}, {x1, 0, -1}, {x1, 0, 1}, {x0, 0, 1}};
        const Vec2 uv[4] = {{u0, 0}, {u1, 0}, {u1, 1}, {u0, 1}};
        for (int idx : {0, 1, 2, 0, 2, 3}) {
            MeshCorner c;
            c.position = p[idx];
            c.normal = Vec3(0, -1, 0);
            c.uv = uv[idx];
            corners.push_back(c);
        }
    };
    quad(-2, 0, 0.0f, 0.5f);
    quad(0, 2, 0.5f, 1.0f);
    Mesh mesh;
    buildMeshFromCorners(corners, {}, true, false, mesh);
    return mesh;
}

} // namespace

TEST_CASE("Stroke region covers only the UVs of triangles under the stroke")
{
    const Mesh mesh = twoQuads();
    ViewNavigator nav;
    nav.setViewportSize(800, 600);
    ViewState s;
    s.viewQuat = axisViewQuat(AxisView::Front);
    s.dist = 10.0f;
    nav.setState(s);
    const Mat4 vp = nav.viewProjectionMatrix();

    // A stroke on the left quad.
    const Vec2 left = nav.worldToPixel(Vec3(-1, 0, 0));
    IRect r = textureRegionForScreenRect(mesh, vp, 800, 600,
                                         {int(left.x) - 5, int(left.y) - 5, 10, 10}, 1024, 512, 0);
    CHECK(r.x == 0);
    CHECK(r.right() <= 513);
    CHECK(r.y == 0);
    CHECK(r.bottom() == 512);

    // Padding grows the region, clamped to the texture.
    IRect padded = textureRegionForScreenRect(mesh, vp, 800, 600,
                                              {int(left.x) - 5, int(left.y) - 5, 10, 10}, 1024, 512, 8);
    CHECK(padded.right() > r.right());
    CHECK(padded.x == 0);

    // Away from the mesh: nothing to bake.
    r = textureRegionForScreenRect(mesh, vp, 800, 600, {0, 0, 10, 10}, 1024, 512, 0);
    CHECK(r.isEmpty());
}
