/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GLSL sources. They are written in the common subset of GLSL 3.30 core and
 * GLSL ES 3.00 (Krita runs on desktop OpenGL 3.3 and, on Windows, on ANGLE /
 * OpenGL ES 3.0). The #version line is prepended at runtime.
 */

#pragma once

namespace AthertaShaders {

// --- Mesh display ----------------------------------------------------------

inline const char *meshVertex = R"(
in vec3 aPos;
in vec3 aNormal;
in vec2 aUV;
in vec4 aTangent;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vTangent;

void main()
{
    vWorldPos = aPos;
    vNormal = aNormal;
    vUV = aUV;
    vTangent = aTangent;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

inline const char *meshFragment = R"(
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vTangent;

uniform sampler2D uBaseColor;
uniform sampler2D uRoughness;
uniform sampler2D uMetallic;
uniform sampler2D uNormalMap;
uniform sampler2D uStroke;

uniform int uDisplayMode;      // 0 lit, 1 base colour, 2 roughness, 3 metallic, 4 normal
uniform int uStrokeMode;       // 0 none, 1 paint, 2 erase
uniform vec4 uStrokeChannels;  // 1.0 where the channel receives the stroke
uniform vec2 uStrokeValues;    // roughness, metallic values painted by the stroke
uniform vec2 uViewportSize;    // device pixels
uniform float uFadeStart;      // facing ratio where paint starts fading out

uniform vec3 uCameraPos;
uniform vec3 uViewDir;
uniform int uOrtho;
uniform float uNormalYSign;    // +1 OpenGL (Blender), -1 DirectX
uniform vec3 uLightDir[3];     // world space, pointing towards the light
uniform vec3 uLightColor[3];
uniform vec3 uDefaultBase;
uniform float uDefaultRoughness;
uniform float uDefaultMetallic;

out vec4 fragColor;

const float PI = 3.14159265359;

vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

vec3 linearToSrgb(vec3 c)
{
    c = max(c, vec3(0.0));
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));
}

vec3 envBRDFApprox(vec3 F0, float roughness, float NoV)
{
    // Karis, "Physically Based Shading on Mobile".
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

vec3 environment(vec3 dir, float roughness)
{
    // Soft studio: bright sky, dark floor, blurred by roughness.
    vec3 sky = vec3(0.60, 0.64, 0.70);
    vec3 horizon = vec3(0.35, 0.35, 0.36);
    vec3 ground = vec3(0.10, 0.095, 0.09);
    float w = 0.05 + roughness * 0.6;
    float t = smoothstep(-w, w, dir.z);
    vec3 c = mix(ground, horizon, t);
    c = mix(c, sky, smoothstep(0.0, 0.6 + roughness * 0.4, dir.z));
    return c;
}

vec3 acesFilm(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float facingFade(vec3 N, vec3 V)
{
    float f = abs(dot(N, V));
    return smoothstep(0.0, uFadeStart, f);
}

void main()
{
    vec2 uv = vec2(vUV.x, 1.0 - vUV.y);  // image row 0 is the top (V = 1)

    vec4 baseTex = texture(uBaseColor, uv);
    vec4 roughTex = texture(uRoughness, uv);
    vec4 metalTex = texture(uMetallic, uv);
    vec4 normalTex = texture(uNormalMap, uv);

    vec3 base = mix(uDefaultBase, baseTex.rgb, baseTex.a);
    float rough = mix(uDefaultRoughness, roughTex.r, roughTex.a);
    float metal = mix(uDefaultMetallic, metalTex.r, metalTex.a);
    vec3 nmap = mix(vec3(0.5, 0.5, 1.0), normalTex.rgb, normalTex.a);

    vec3 Ng = normalize(vNormal);
    vec3 V = uOrtho == 1 ? -uViewDir : normalize(uCameraPos - vWorldPos);
    if (dot(Ng, V) < 0.0) {
        Ng = -Ng;  // show back faces lit from the viewing side
    }

    // Live preview of the stroke being painted (screen space).
    if (uStrokeMode != 0) {
        vec2 suv = vec2(gl_FragCoord.x / uViewportSize.x, 1.0 - gl_FragCoord.y / uViewportSize.y);
        vec4 s = texture(uStroke, suv);  // premultiplied
        float fade = facingFade(Ng, V);
        float a = s.a * fade;
        if (uStrokeMode == 1) {
            base = mix(base, base * (1.0 - a) + s.rgb * fade, uStrokeChannels.x);
            rough = mix(rough, mix(rough, uStrokeValues.x, a), uStrokeChannels.y);
            metal = mix(metal, mix(metal, uStrokeValues.y, a), uStrokeChannels.z);
        } else {
            base = mix(base, mix(base, uDefaultBase, a), uStrokeChannels.x);
            rough = mix(rough, mix(rough, uDefaultRoughness, a), uStrokeChannels.y);
            metal = mix(metal, mix(metal, uDefaultMetallic, a), uStrokeChannels.z);
        }
    }

    if (uDisplayMode == 1) { fragColor = vec4(base, 1.0); return; }
    if (uDisplayMode == 2) { fragColor = vec4(vec3(rough), 1.0); return; }
    if (uDisplayMode == 3) { fragColor = vec4(vec3(metal), 1.0); return; }
    if (uDisplayMode == 4) { fragColor = vec4(nmap, 1.0); return; }

    // Tangent space normal mapping (MikkTSpace convention).
    vec3 T = vTangent.xyz - Ng * dot(Ng, vTangent.xyz);
    vec3 N = Ng;
    if (dot(T, T) > 1e-8) {
        T = normalize(T);
        vec3 B = cross(Ng, T) * (vTangent.w < 0.0 ? -1.0 : 1.0);
        vec3 tn = nmap * 2.0 - 1.0;
        tn.y *= uNormalYSign;
        N = normalize(T * tn.x + B * tn.y + Ng * max(tn.z, 0.0));
    }

    vec3 albedo = srgbToLinear(base);
    float r = clamp(rough, 0.03, 1.0);
    float alpha = r * r;
    vec3 F0 = mix(vec3(0.04), albedo, metal);
    vec3 diffuseColor = albedo * (1.0 - metal);
    float NoV = max(dot(N, V), 1e-4);

    vec3 color = vec3(0.0);
    for (int i = 0; i < 3; ++i) {
        vec3 L = uLightDir[i];
        vec3 H = normalize(L + V);
        float NoL = max(dot(N, L), 0.0);
        float NoH = max(dot(N, H), 0.0);
        float VoH = max(dot(V, H), 0.0);
        float a2 = alpha * alpha;
        float d = NoH * NoH * (a2 - 1.0) + 1.0;
        float D = a2 / (PI * d * d);
        float k = (r + 1.0) * (r + 1.0) / 8.0;
        float G = (NoV / (NoV * (1.0 - k) + k)) * (NoL / (NoL * (1.0 - k) + k));
        vec3 F = F0 + (1.0 - F0) * pow(1.0 - VoH, 5.0);
        vec3 spec = D * G * F / max(4.0 * NoV * NoL, 1e-4);
        vec3 kd = (1.0 - F) * diffuseColor / PI;
        color += (kd + spec) * uLightColor[i] * NoL;
    }

    // Image based ambient from the procedural studio.
    vec3 R = reflect(-V, N);
    vec3 irradiance = environment(N, 1.0);
    vec3 specEnv = environment(R, r) * envBRDFApprox(F0, r, NoV);
    color += diffuseColor * irradiance + specEnv;

    fragColor = vec4(linearToSrgb(acesFilm(color)), 1.0);
}
)";

// --- Floor grid ------------------------------------------------------------

inline const char *gridVertex = R"(
in vec2 aCorner;

uniform mat4 uViewProj;
uniform vec2 uCenter;
uniform float uExtent;

out vec2 vXY;

void main()
{
    vXY = uCenter + aCorner * uExtent;
    gl_Position = uViewProj * vec4(vXY, 0.0, 1.0);
}
)";

inline const char *gridFragment = R"(
in vec2 vXY;

uniform vec2 uCenter;
uniform float uExtent;
uniform float uScale;

out vec4 fragColor;

float gridLine(vec2 p, float scale)
{
    vec2 c = p / scale;
    vec2 d = fwidth(c);
    vec2 g = abs(fract(c - 0.5) - 0.5) / max(d, vec2(1e-6));
    return 1.0 - min(min(g.x, g.y), 1.0);
}

void main()
{
    // Blender theme colours: grid #545454, X axis #ff3352, Y axis #8bdc00.
    float minor = gridLine(vXY, uScale);
    float major = gridLine(vXY, uScale * 10.0);
    vec4 col = vec4(vec3(0.33), max(minor * 0.35, major * 0.7));

    vec2 w = fwidth(vXY) * 1.2;
    if (abs(vXY.y) < w.y) {
        col = vec4(1.0, 0.2, 0.32, 1.0);
    } else if (abs(vXY.x) < w.x) {
        col = vec4(0.545, 0.863, 0.0, 1.0);
    }

    float fade = 1.0 - smoothstep(uExtent * 0.35, uExtent, length(vXY - uCenter));
    col.a *= fade;
    if (col.a < 0.005) {
        discard;
    }
    fragColor = col;
}
)";

// --- Depth only (stroke camera) ---------------------------------------------

inline const char *depthVertex = R"(
in vec3 aPos;
uniform mat4 uViewProj;
void main()
{
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

inline const char *depthFragment = R"(
out vec4 fragColor;
void main()
{
    fragColor = vec4(1.0);
}
)";

// --- UV space passes -------------------------------------------------------

// Rasterises triangles at their UV position. Texture row 0 (top) is V = 1,
// so framebuffer row y equals image row y.
inline const char *uvVertex = R"(
in vec3 aPos;
in vec3 aNormal;
in vec2 aUV;

out vec3 vWorldPos;
out vec3 vNormal;

void main()
{
    vWorldPos = aPos;
    vNormal = aNormal;
    gl_Position = vec4(aUV.x * 2.0 - 1.0, 1.0 - 2.0 * aUV.y, 0.0, 1.0);
}
)";

inline const char *coverageFragment = R"(
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 fragColor;
void main()
{
    fragColor = vec4(1.0);
}
)";

inline const char *bakeFragment = R"(
in vec3 vWorldPos;
in vec3 vNormal;

uniform mat4 uStrokeViewProj;
uniform sampler2D uStroke;      // premultiplied, row 0 = top of the viewport
uniform sampler2D uDepth;       // depth of the scene seen by the stroke camera
uniform vec3 uCameraPos;
uniform vec3 uViewDir;
uniform int uOrtho;
uniform float uNear;
uniform float uFar;
uniform float uFadeStart;
uniform float uPixelScale;      // world size of one screen pixel at depth 1 (perspective) or anywhere (ortho)

out vec4 fragColor;

float linearDepth(float d)
{
    float z = d * 2.0 - 1.0;
    if (uOrtho == 1) {
        return uNear + (z + 1.0) * 0.5 * (uFar - uNear);
    }
    return (2.0 * uNear * uFar) / (uFar + uNear - z * (uFar - uNear));
}

void main()
{
    vec4 clip = uStrokeViewProj * vec4(vWorldPos, 1.0);
    if (clip.w <= 0.0) {
        discard;
    }
    vec3 ndc = clip.xyz / clip.w;
    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z < -1.0 || ndc.z > 1.0) {
        discard;
    }
    vec2 suv = ndc.xy * 0.5 + 0.5;

    vec3 N = normalize(vNormal);
    vec3 V = uOrtho == 1 ? -uViewDir : normalize(uCameraPos - vWorldPos);
    float facingSigned = dot(N, V);
    float facing = abs(facingSigned);

    // Only paint surfaces that were visible when the stroke was made. The
    // depth map has one sample per screen pixel, so at grazing angles a
    // surface spans a large depth range inside a pixel: compare against the
    // farthest *surface* depth of the 3x3 neighbourhood with a slope-scaled
    // tolerance, otherwise the test aliases into stripes. Background samples
    // are ignored, or texels on the far side would leak in at silhouettes.
    ivec2 size = textureSize(uDepth, 0);
    ivec2 p = clamp(ivec2(suv * vec2(size)), ivec2(0), size - 1);
    float fragDepth = linearDepth(ndc.z * 0.5 + 0.5);
    float centre = texelFetch(uDepth, p, 0).r;
    float farthest = -1.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            ivec2 q = clamp(p + ivec2(x, y), ivec2(0), size - 1);
            float d = texelFetch(uDepth, q, 0).r;
            if (d < 1.0) {
                farthest = max(farthest, d);
            }
        }
    }
    float pixelWorld = uPixelScale * (uOrtho == 1 ? 1.0 : fragDepth);
    float tolerance = max(1e-4, fragDepth * 0.002) + pixelWorld;
    float sceneDepth;
    if (facingSigned > 0.0) {
        float slope = sqrt(max(1.0 - facing * facing, 0.0)) / max(facing, 0.05);
        tolerance += pixelWorld * 2.0 * slope;
        sceneDepth = farthest < 0.0 ? fragDepth : linearDepth(farthest);
    } else {
        // Back faces (e.g. a plane seen from behind): strict test only.
        sceneDepth = centre < 1.0 ? linearDepth(centre) : fragDepth;
    }
    if (fragDepth - sceneDepth > tolerance) {
        discard;
    }

    float fade = smoothstep(0.0, uFadeStart, facing);

    fragColor = texture(uStroke, vec2(suv.x, 1.0 - suv.y)) * fade;
}
)";

} // namespace AthertaShaders
