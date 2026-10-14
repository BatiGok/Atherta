/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "doctest.h"

#include "core/Mesh.h"
#include "core/MeshBvh.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>

using namespace atherta;
using doctest::Approx;

namespace {

std::string writeTempFile(const std::string &name, const std::string &contents)
{
    const char *tmp = std::getenv("TMPDIR");
    std::string path = std::string(tmp ? tmp : "/tmp") + "/" + name;
#ifdef _WIN32
    if (!tmp) {
        const char *winTmp = std::getenv("TEMP");
        path = std::string(winTmp ? winTmp : ".") + "\\" + name;
    }
#endif
    std::ofstream f(path, std::ios::binary);
    f << contents;
    return path;
}

void checkVec(const Vec3 &a, const Vec3 &b)
{
    CHECK(a.x == Approx(b.x).scale(1.0));
    CHECK(a.y == Approx(b.y).scale(1.0));
    CHECK(a.z == Approx(b.z).scale(1.0));
}

// A 2x2 quad in the OBJ's XY plane (Y-up file), facing +Z.
const char *kQuadObj = R"(
v 0 0 0
v 2 0 0
v 2 2 0
v 0 2 0
vt 0 0
vt 1 0
vt 1 1
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1 4/4/1
)";

} // namespace

TEST_CASE("OBJ loads Y-up data as Z-up, like Blender's importer")
{
    const std::string path = writeTempFile("atherta_quad.obj", kQuadObj);
    Mesh mesh;
    const MeshLoadResult r = loadMesh(path, mesh);
    std::remove(path.c_str());

    REQUIRE(r.ok);
    CHECK(r.warnings.empty());
    CHECK(mesh.hasUVs);
    CHECK_FALSE(mesh.uvsOutsideUnitSquare);
    CHECK(mesh.triangleCount() == 2);
    CHECK(mesh.vertexCount() == 4);
    checkVec(mesh.boundsMin, Vec3(0, 0, 0));
    checkVec(mesh.boundsMax, Vec3(2, 0, 2));
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        checkVec(mesh.normals[i], Vec3(0, -1, 0));
        // U runs along +X, V along +Z: MikkTSpace tangent +X with a positive sign.
        checkVec(mesh.tangents[i].xyz(), Vec3(1, 0, 0));
        CHECK(mesh.tangents[i].w == Approx(1.0f));
    }
}

TEST_CASE("OBJ material groups become mesh parts")
{
    const std::string path = writeTempFile("atherta_parts.obj", R"(
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
v 2 0 0
v 2 1 0
vt 0 0
vt 1 1
vt 0.5 0
usemtl Body
f 1/1 2/3 3/2
f 1/1 3/2 4/1
usemtl Eyes
f 2/3 5/1 6/2
)");
    Mesh mesh;
    const MeshLoadResult r = loadMesh(path, mesh);
    std::remove(path.c_str());

    REQUIRE(r.ok);
    REQUIRE(mesh.parts.size() == 2);
    CHECK(mesh.parts[0].material == "Body");
    CHECK(mesh.parts[0].indexCount == 6);
    CHECK(mesh.parts[1].material == "Eyes");
    CHECK(mesh.parts[1].firstIndex == 6);
    CHECK(mesh.parts[1].indexCount == 3);
    // No normals in the file: they are generated.
    CHECK(mesh.normals[0].length() == Approx(1.0f));
}

TEST_CASE("Missing UVs produce a warning, bad files an error")
{
    const std::string path = writeTempFile("atherta_nouv.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    Mesh mesh;
    MeshLoadResult r = loadMesh(path, mesh);
    std::remove(path.c_str());
    REQUIRE(r.ok);
    CHECK_FALSE(mesh.hasUVs);
    CHECK(r.warnings.size() == 1);

    r = loadMesh("/nonexistent/atherta.fbx", mesh);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.error.empty());
}

TEST_CASE("BVH ray hit returns position, normal and UV")
{
    const std::string path = writeTempFile("atherta_quad2.obj", kQuadObj);
    Mesh mesh;
    REQUIRE(loadMesh(path, mesh).ok);
    std::remove(path.c_str());

    MeshBvh bvh;
    bvh.build(mesh);
    const RayHit hit = bvh.intersect({Vec3(1.5f, -5.0f, 0.5f), Vec3(0, 1, 0)});
    REQUIRE(hit.hit);
    CHECK(hit.t == Approx(5.0f));
    checkVec(hit.position, Vec3(1.5f, 0, 0.5f));
    checkVec(hit.normal, Vec3(0, -1, 0));
    CHECK(hit.uv.x == Approx(0.75f));
    CHECK(hit.uv.y == Approx(0.25f));

    CHECK_FALSE(bvh.intersect({Vec3(3.0f, -5.0f, 0.5f), Vec3(0, 1, 0)}).hit);
    CHECK_FALSE(bvh.intersect({Vec3(1.0f, -5.0f, 1.0f), Vec3(0, -1, 0)}).hit);
}

TEST_CASE("BVH agrees with brute force on a random triangle soup")
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> pos(-10.0f, 10.0f);
    std::uniform_real_distribution<float> small(-0.8f, 0.8f);

    std::vector<MeshCorner> corners;
    for (int i = 0; i < 3000; ++i) {
        const Vec3 c(pos(rng), pos(rng), pos(rng));
        for (int k = 0; k < 3; ++k) {
            MeshCorner mc;
            mc.position = c + Vec3(small(rng), small(rng), small(rng));
            mc.normal = Vec3(0, 0, 1);
            corners.push_back(mc);
        }
    }
    Mesh mesh;
    buildMeshFromCorners(corners, {}, false, false, mesh);
    MeshBvh bvh;
    bvh.build(mesh);
    CHECK(bvh.nodeCount() > 1);

    int hits = 0;
    for (int i = 0; i < 500; ++i) {
        const Ray ray{Vec3(pos(rng), pos(rng), pos(rng)) * 1.5f,
                      normalized(Vec3(pos(rng), pos(rng), pos(rng)))};
        // Brute force.
        float best = std::numeric_limits<float>::max();
        for (size_t t = 0; t < mesh.triangleCount(); ++t) {
            const Vec3 &p0 = mesh.positions[mesh.indices[t * 3]];
            const Vec3 &p1 = mesh.positions[mesh.indices[t * 3 + 1]];
            const Vec3 &p2 = mesh.positions[mesh.indices[t * 3 + 2]];
            const Vec3 e1 = p1 - p0, e2 = p2 - p0;
            const Vec3 pv = cross(ray.direction, e2);
            const float det = dot(e1, pv);
            if (std::fabs(det) < 1e-12f) {
                continue;
            }
            const Vec3 tv = ray.origin - p0;
            const float u = dot(tv, pv) / det;
            const Vec3 qv = cross(tv, e1);
            const float v = dot(ray.direction, qv) / det;
            const float tt = dot(e2, qv) / det;
            if (u >= 0 && v >= 0 && u + v <= 1 && tt > 0 && tt < best) {
                best = tt;
            }
        }
        const RayHit hit = bvh.intersect(ray);
        if (best == std::numeric_limits<float>::max()) {
            CHECK_FALSE(hit.hit);
        } else {
            REQUIRE(hit.hit);
            CHECK(hit.t == Approx(best).epsilon(1e-4));
            ++hits;
        }
    }
    CHECK(hits > 20);
}

TEST_CASE("Real FBX/OBJ files (set ATHERTA_TEST_DATA to ufbx's data directory)")
{
    const char *dir = std::getenv("ATHERTA_TEST_DATA");
    if (!dir) {
        return;
    }
    const std::string base(dir);

    SUBCASE("Maya cube: centimetres and Y-up are converted to metres and Z-up")
    {
        Mesh mesh;
        const MeshLoadResult r = loadMesh(base + "/maya_cube_7500_binary.fbx", mesh);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(mesh.triangleCount() == 12);
        CHECK(mesh.hasUVs);
        const Vec3 size = mesh.boundsMax - mesh.boundsMin;
        MESSAGE("maya cube size " << size.x << " " << size.y << " " << size.z);
        CHECK(size.x == Approx(size.z));
        CHECK(size.x > 0.0f);
    }
    SUBCASE("Blender cube")
    {
        Mesh mesh;
        const MeshLoadResult r = loadMesh(base + "/blender_272_cube_7400_binary.fbx", mesh);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(mesh.triangleCount() == 12);
        const Vec3 size = mesh.boundsMax - mesh.boundsMin;
        MESSAGE("blender cube size " << size.x << " " << size.y << " " << size.z);
        CHECK(size.x == Approx(2.0f).epsilon(1e-3));
        CHECK(size.z == Approx(2.0f).epsilon(1e-3));
    }
    SUBCASE("Blender OBJ ball")
    {
        Mesh mesh;
        const MeshLoadResult r = loadMesh(base + "/blender_279_ball_0_obj.obj", mesh);
        REQUIRE_MESSAGE(r.ok, r.error);
        CHECK(mesh.triangleCount() > 0);
        MeshBvh bvh;
        bvh.build(mesh);
        const Vec3 c = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
        CHECK(bvh.intersect({c + Vec3(0, -10, 0), Vec3(0, 1, 0)}).hit);
    }
}
