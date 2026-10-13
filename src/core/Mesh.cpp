/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Mesh.h"

#include "mikktspace.h"
#include "ufbx.h"

#include <cstring>
#include <limits>
#include <unordered_map>

namespace atherta {

namespace {

// --- MikkTSpace adapter (one face per triangle, 3 corners each) -------------

struct MikkData {
    std::vector<MeshCorner> *corners;
};

int mikkNumFaces(const SMikkTSpaceContext *ctx)
{
    auto *d = static_cast<MikkData *>(ctx->m_pUserData);
    return static_cast<int>(d->corners->size() / 3);
}

int mikkNumVerts(const SMikkTSpaceContext *, const int)
{
    return 3;
}

MeshCorner &mikkCorner(const SMikkTSpaceContext *ctx, int face, int vert)
{
    auto *d = static_cast<MikkData *>(ctx->m_pUserData);
    return (*d->corners)[static_cast<size_t>(face) * 3 + static_cast<size_t>(vert)];
}

void mikkPosition(const SMikkTSpaceContext *ctx, float out[], const int face, const int vert)
{
    const Vec3 &p = mikkCorner(ctx, face, vert).position;
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void mikkNormal(const SMikkTSpaceContext *ctx, float out[], const int face, const int vert)
{
    const Vec3 &n = mikkCorner(ctx, face, vert).normal;
    out[0] = n.x;
    out[1] = n.y;
    out[2] = n.z;
}

void mikkTexCoord(const SMikkTSpaceContext *ctx, float out[], const int face, const int vert)
{
    const Vec2 &uv = mikkCorner(ctx, face, vert).uv;
    out[0] = uv.x;
    out[1] = uv.y;
}

void mikkSetTangent(const SMikkTSpaceContext *ctx, const float t[], const float sign,
                    const int face, const int vert)
{
    mikkCorner(ctx, face, vert).tangent = Vec4(t[0], t[1], t[2], sign);
}

void computeMikkTangents(std::vector<MeshCorner> &corners)
{
    SMikkTSpaceInterface iface = {};
    iface.m_getNumFaces = mikkNumFaces;
    iface.m_getNumVerticesOfFace = mikkNumVerts;
    iface.m_getPosition = mikkPosition;
    iface.m_getNormal = mikkNormal;
    iface.m_getTexCoord = mikkTexCoord;
    iface.m_setTSpaceBasic = mikkSetTangent;

    MikkData data{&corners};
    SMikkTSpaceContext ctx = {};
    ctx.m_pInterface = &iface;
    ctx.m_pUserData = &data;
    genTangSpaceDefault(&ctx);
}

// --- Corner de-duplication ----------------------------------------------

struct CornerKey {
    float v[16];
    bool operator==(const CornerKey &o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
};

struct CornerKeyHash {
    size_t operator()(const CornerKey &k) const
    {
        // FNV-1a over the raw bytes.
        const auto *bytes = reinterpret_cast<const unsigned char *>(k.v);
        uint64_t h = 1469598103934665603ull;
        for (size_t i = 0; i < sizeof(k.v); ++i) {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
        return static_cast<size_t>(h);
    }
};

CornerKey makeKey(const MeshCorner &c)
{
    CornerKey k;
    std::memset(k.v, 0, sizeof(k.v));
    const float vals[] = {c.position.x, c.position.y, c.position.z, c.normal.x,  c.normal.y,
                          c.normal.z,   c.uv.x,       c.uv.y,       c.tangent.x, c.tangent.y,
                          c.tangent.z,  c.tangent.w};
    std::memcpy(k.v, vals, sizeof(vals));
    // Normalise -0.0f to 0.0f so equal values hash equally.
    for (float &f : k.v) {
        if (f == 0.0f) {
            f = 0.0f;
        }
    }
    return k;
}

Vec3 toVec3(const ufbx_vec3 &v)
{
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

} // namespace

void buildMeshFromCorners(std::vector<MeshCorner> corners,
                          const std::vector<MeshPart> &cornerParts,
                          bool hasUVs,
                          bool computeTangents,
                          Mesh &out)
{
    out.clear();
    out.hasUVs = hasUVs;

    if (hasUVs && computeTangents && !corners.empty()) {
        computeMikkTangents(corners);
    }

    std::unordered_map<CornerKey, uint32_t, CornerKeyHash> lookup;
    lookup.reserve(corners.size());
    out.indices.reserve(corners.size());

    Vec3 bmin(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
              std::numeric_limits<float>::max());
    Vec3 bmax = -bmin;

    for (const MeshCorner &c : corners) {
        const auto [it, inserted] =
            lookup.emplace(makeKey(c), static_cast<uint32_t>(out.positions.size()));
        if (inserted) {
            out.positions.push_back(c.position);
            out.normals.push_back(c.normal);
            out.uvs.push_back(c.uv);
            out.tangents.push_back(c.tangent);
            bmin = componentMin(bmin, c.position);
            bmax = componentMax(bmax, c.position);
            if (hasUVs && (c.uv.x < 0.0f || c.uv.x > 1.0f || c.uv.y < 0.0f || c.uv.y > 1.0f)) {
                out.uvsOutsideUnitSquare = true;
            }
        }
        out.indices.push_back(it->second);
    }

    out.parts = cornerParts; // corner index == index buffer position
    if (out.positions.empty()) {
        bmin = bmax = Vec3();
    }
    out.boundsMin = bmin;
    out.boundsMax = bmax;
}

MeshLoadResult loadMesh(const std::string &path, Mesh &out)
{
    MeshLoadResult result;

    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_z_up;
    opts.target_unit_meters = 1.0;
    // .obj has no axis information; assume Y-up like Blender's OBJ importer.
    opts.obj_axes = ufbx_axes_right_handed_y_up;
    opts.generate_missing_normals = true;
    opts.load_external_files = false;
    opts.ignore_missing_external_files = true;

    ufbx_error error;
    ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        char buf[1024];
        ufbx_format_error(buf, sizeof(buf), &error);
        result.error = buf;
        return result;
    }

    std::vector<MeshCorner> corners;
    std::vector<MeshPart> parts;
    bool anyUVs = false;
    bool allUVs = true;
    bool allTangents = true;

    std::vector<uint32_t> triIndices;

    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node *node = scene->nodes.data[ni];
        const ufbx_mesh *mesh = node->mesh;
        if (!mesh || mesh->num_faces == 0) {
            continue;
        }

        const ufbx_matrix &toWorld = node->geometry_to_world;
        const ufbx_matrix normalMatrix = ufbx_matrix_for_normals(&toWorld);
        const bool hasUV = mesh->vertex_uv.exists;
        const bool hasTangent = mesh->vertex_tangent.exists && mesh->vertex_bitangent.exists;
        anyUVs = anyUVs || hasUV;
        allUVs = allUVs && hasUV;
        allTangents = allTangents && hasTangent;

        triIndices.resize(mesh->max_face_triangles * 3);

        // Without material parts, treat the whole mesh as one part.
        std::vector<uint32_t> allFaces;
        if (mesh->material_parts.count == 0) {
            allFaces.resize(mesh->num_faces);
            for (size_t i = 0; i < allFaces.size(); ++i) {
                allFaces[i] = static_cast<uint32_t>(i);
            }
        }
        const size_t partCount = std::max<size_t>(mesh->material_parts.count, 1);

        for (size_t pi = 0; pi < partCount; ++pi) {
            const ufbx_mesh_part *part =
                mesh->material_parts.count ? &mesh->material_parts.data[pi] : nullptr;
            const size_t faceCount = part ? part->face_indices.count : allFaces.size();
            const uint32_t *faceList = part ? part->face_indices.data : allFaces.data();
            if (faceCount == 0) {
                continue;
            }
            MeshPart outPart;
            outPart.firstIndex = static_cast<uint32_t>(corners.size());
            if (part && part->index < mesh->materials.count && mesh->materials.data[part->index]) {
                outPart.material = mesh->materials.data[part->index]->name.data;
            }

            for (size_t fi = 0; fi < faceCount; ++fi) {
                const ufbx_face face = mesh->faces.data[faceList[fi]];
                const uint32_t numTris = ufbx_triangulate_face(
                    triIndices.data(), triIndices.size(), mesh, face);

                for (uint32_t ti = 0; ti < numTris * 3; ++ti) {
                    const uint32_t ix = triIndices[ti];
                    MeshCorner c;
                    c.position = toVec3(ufbx_transform_position(
                        &toWorld, ufbx_get_vertex_vec3(&mesh->vertex_position, ix)));
                    c.normal = normalized(toVec3(ufbx_transform_direction(
                        &normalMatrix, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix))));
                    if (hasUV) {
                        const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix);
                        c.uv = Vec2(static_cast<float>(uv.x), static_cast<float>(uv.y));
                    }
                    if (hasTangent) {
                        const Vec3 t = normalized(toVec3(ufbx_transform_direction(
                            &toWorld, ufbx_get_vertex_vec3(&mesh->vertex_tangent, ix))));
                        const Vec3 b = toVec3(ufbx_transform_direction(
                            &toWorld, ufbx_get_vertex_vec3(&mesh->vertex_bitangent, ix)));
                        const float sign = dot(cross(c.normal, t), b) < 0.0f ? -1.0f : 1.0f;
                        c.tangent = Vec4(t, sign);
                    }
                    corners.push_back(c);
                }
            }

            outPart.indexCount = static_cast<uint32_t>(corners.size()) - outPart.firstIndex;
            parts.push_back(outPart);
        }
    }

    ufbx_free_scene(scene);

    if (corners.empty()) {
        result.error = "The file does not contain any mesh geometry.";
        return result;
    }
    if (!anyUVs) {
        result.warnings.push_back("The model has no UV coordinates; it cannot be painted.");
    } else if (!allUVs) {
        result.warnings.push_back("Some meshes have no UV coordinates; they cannot be painted.");
    }

    // Always use MikkTSpace unless every mesh shipped its own tangents.
    buildMeshFromCorners(std::move(corners), parts, anyUVs, !allTangents, out);

    if (out.uvsOutsideUnitSquare) {
        result.warnings.push_back(
            "Some UVs are outside the 0-1 range (UDIM tiles are not supported yet); "
            "those areas wrap around the texture.");
    }

    result.ok = true;
    return result;
}

} // namespace atherta
