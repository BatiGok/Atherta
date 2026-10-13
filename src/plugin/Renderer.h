/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "Channels.h"
#include "GutterDilation.h"
#include "Math.h"
#include "Mesh.h"

#include <QImage>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QRect>
#include <QScopedPointer>
#include <QSize>

#include <vector>

/**
 * All OpenGL work of the 3D viewport: drawing the PBR preview and baking
 * screen-space strokes into UV space. Every method expects the viewport's
 * context to be current.
 */
class Renderer : protected QOpenGLExtraFunctions
{
public:
    enum class DisplayMode { Material = 0, BaseColor, Roughness, Metallic, Normal };
    enum class StrokeMode { None = 0, Paint, Erase };

    struct DrawParams {
        atherta::Mat4 viewProj;
        atherta::Vec3 cameraPos;
        atherta::Vec3 viewDir;
        atherta::Quat viewQuat;
        bool ortho = false;
        float dist = 10.0f;
        atherta::Vec3 pivot;
        QSize viewportDevice;
        DisplayMode displayMode = DisplayMode::Material;
        bool normalMapDirectX = false;
        bool showGrid = true;
        StrokeMode strokeMode = StrokeMode::None;
        bool strokeChannels[atherta::kChannelCount] = {true, false, false, false};
        float strokeRoughness = 0.5f;
        float strokeMetallic = 0.0f;
        float fadeStart = 0.2f;
    };

    struct BakeParams {
        atherta::Mat4 viewProj;
        atherta::Vec3 cameraPos;
        atherta::Vec3 viewDir;
        bool ortho = false;
        float nearClip = 0.01f;
        float farClip = 1000.0f;
        float fadeStart = 0.2f;
        /** Element [1][1] of the projection matrix (not view-projection). */
        float projection11 = 1.0f;
        QSize viewportDevice;
        /** Texture area to bake, from textureRegionForScreenRect(). */
        atherta::IRect region;
    };

    Renderer();
    ~Renderer();

    bool initialize(QString *error);
    void cleanup();
    bool isInitialized() const { return m_initialized; }

    void setMesh(const atherta::Mesh &mesh);
    bool hasMesh() const { return m_indexCount > 0; }

    /** (Re)allocates the channel textures, filled with transparent texels. */
    void setTextureSize(const QSize &size);
    QSize textureSize() const { return m_textureSize; }
    /** Uploads straight-alpha RGBA8888 texels at @p offset. */
    void uploadChannel(atherta::Channel c, const QImage &rgba, const QPoint &offset);
    /** Regenerates mipmaps of channels changed since the last call. */
    void finishChannelUploads();

    /** Live stroke layer (premultiplied RGBA8888, viewport device size). */
    void setStrokeLayerSize(const QSize &size);
    void uploadStrokeLayer(const QImage &premultiplied, const QPoint &offset);
    void clearStrokeLayer();

    void draw(const DrawParams &params, GLuint targetFbo);

    /**
     * Projects the current stroke layer onto the mesh and returns the
     * premultiplied RGBA8 texels of @p params.region (row-major, top-down).
     * Renders the scene depth from the stroke camera first.
     */
    bool bakeStroke(const BakeParams &params, std::vector<uint8_t> &out, GLuint restoreFbo);

    /** Texels covered by a UV triangle (one byte each), for gutter dilation. */
    const std::vector<uint8_t> &coverage(GLuint restoreFbo);

    int maxTextureSize() const { return m_maxTextureSize; }

private:
    bool buildProgram(QOpenGLShaderProgram &prog, const char *vs, const char *fs,
                      const QStringList &attributes, QString *error);
    void bindMeshAttributes(QOpenGLShaderProgram &prog);
    void drawMesh();
    GLuint createTexture(const QSize &size, GLenum internalFormat, GLenum format, GLenum type,
                         bool mipmaps);
    void ensureDepthTarget(const QSize &size);
    void ensureBakeTarget();

    bool m_initialized = false;
    bool m_isES = false;
    int m_maxTextureSize = 4096;

    QOpenGLShaderProgram m_meshProgram;
    QOpenGLShaderProgram m_gridProgram;
    QOpenGLShaderProgram m_depthProgram;
    QOpenGLShaderProgram m_coverageProgram;
    QOpenGLShaderProgram m_bakeProgram;

    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_ebo = 0;
    GLsizei m_indexCount = 0;

    GLuint m_gridVao = 0;
    GLuint m_gridVbo = 0;

    QSize m_textureSize;
    GLuint m_channelTex[atherta::kChannelCount] = {0, 0, 0, 0};
    bool m_channelDirty[atherta::kChannelCount] = {false, false, false, false};

    QSize m_strokeSize;
    GLuint m_strokeTex = 0;

    QSize m_depthSize;
    GLuint m_depthFbo = 0;
    GLuint m_depthTex = 0;

    QSize m_bakeSize;
    GLuint m_bakeFbo = 0;
    GLuint m_bakeTex = 0;

    std::vector<uint8_t> m_coverage;
    bool m_coverageValid = false;
};
