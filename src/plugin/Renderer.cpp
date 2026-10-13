/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Renderer.h"

#include "Shaders.h"

#include <QOpenGLContext>

#include <cmath>

using namespace atherta;

namespace {

constexpr int kFloatsPerVertex = 12; // position 3, normal 3, uv 2, tangent 4

#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif

enum Attribute : GLuint { AttrPos = 0, AttrNormal = 1, AttrUV = 2, AttrTangent = 3 };

void setMat4(QOpenGLExtraFunctions *f, QOpenGLShaderProgram &p, const char *name, const Mat4 &m)
{
    f->glUniformMatrix4fv(p.uniformLocation(name), 1, GL_FALSE, m.m);
}

void setVec3(QOpenGLShaderProgram &p, const char *name, const Vec3 &v)
{
    p.setUniformValue(name, v.x, v.y, v.z);
}

} // namespace

Renderer::Renderer() = default;

Renderer::~Renderer() = default;

bool Renderer::buildProgram(QOpenGLShaderProgram &prog, const char *vs, const char *fs,
                            const QStringList &attributes, QString *error)
{
    const QByteArray header = m_isES ? QByteArrayLiteral("#version 300 es\n"
                                                         "precision highp float;\n"
                                                         "precision highp int;\n"
                                                         "precision highp sampler2D;\n")
                                     : QByteArrayLiteral("#version 330 core\n");
    prog.removeAllShaders();
    if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex, header + vs) ||
        !prog.addShaderFromSourceCode(QOpenGLShader::Fragment, header + fs)) {
        if (error) {
            *error = prog.log();
        }
        return false;
    }
    for (int i = 0; i < attributes.size(); ++i) {
        prog.bindAttributeLocation(attributes[i], i);
    }
    if (!prog.link()) {
        if (error) {
            *error = prog.log();
        }
        return false;
    }
    return true;
}

bool Renderer::initialize(QString *error)
{
    initializeOpenGLFunctions();
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    m_isES = ctx && ctx->isOpenGLES();
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

    const QStringList meshAttrs = {"aPos", "aNormal", "aUV", "aTangent"};
    if (!buildProgram(m_meshProgram, AthertaShaders::meshVertex, AthertaShaders::meshFragment,
                      meshAttrs, error) ||
        !buildProgram(m_gridProgram, AthertaShaders::gridVertex, AthertaShaders::gridFragment,
                      {"aCorner"}, error) ||
        !buildProgram(m_depthProgram, AthertaShaders::depthVertex, AthertaShaders::depthFragment,
                      {"aPos"}, error) ||
        !buildProgram(m_coverageProgram, AthertaShaders::uvVertex, AthertaShaders::coverageFragment,
                      meshAttrs, error) ||
        !buildProgram(m_bakeProgram, AthertaShaders::uvVertex, AthertaShaders::bakeFragment,
                      meshAttrs, error)) {
        return false;
    }

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    const float corners[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);
    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);

    setTextureSize(QSize(1, 1));
    setStrokeLayerSize(QSize(1, 1));

    m_initialized = true;
    return true;
}

void Renderer::cleanup()
{
    if (!m_initialized) {
        return;
    }
    glDeleteVertexArrays(1, &m_vao);
    glDeleteBuffers(1, &m_vbo);
    glDeleteBuffers(1, &m_ebo);
    glDeleteVertexArrays(1, &m_gridVao);
    glDeleteBuffers(1, &m_gridVbo);
    glDeleteTextures(kChannelCount, m_channelTex);
    glDeleteTextures(1, &m_strokeTex);
    glDeleteTextures(1, &m_depthTex);
    glDeleteFramebuffers(1, &m_depthFbo);
    glDeleteTextures(1, &m_bakeTex);
    glDeleteFramebuffers(1, &m_bakeFbo);

    m_vao = m_vbo = m_ebo = m_gridVao = m_gridVbo = 0;
    for (GLuint &t : m_channelTex) {
        t = 0;
    }
    m_strokeTex = m_depthTex = m_depthFbo = m_bakeTex = m_bakeFbo = 0;
    m_indexCount = 0;
    m_textureSize = m_strokeSize = m_depthSize = m_bakeSize = QSize();
    m_coverage.clear();
    m_coverageValid = false;

    m_meshProgram.removeAllShaders();
    m_gridProgram.removeAllShaders();
    m_depthProgram.removeAllShaders();
    m_coverageProgram.removeAllShaders();
    m_bakeProgram.removeAllShaders();
    m_initialized = false;
}

void Renderer::setMesh(const Mesh &mesh)
{
    std::vector<float> data(mesh.vertexCount() * kFloatsPerVertex);
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        float *v = &data[i * kFloatsPerVertex];
        const Vec3 &p = mesh.positions[i];
        const Vec3 &n = mesh.normals[i];
        const Vec2 &uv = mesh.uvs[i];
        const Vec4 &t = mesh.tangents[i];
        v[0] = p.x; v[1] = p.y; v[2] = p.z;
        v[3] = n.x; v[4] = n.y; v[5] = n.z;
        v[6] = uv.x; v[7] = uv.y;
        v[8] = t.x; v[9] = t.y; v[10] = t.z; v[11] = t.w;
    }

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.size() * sizeof(float)), data.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(uint32_t)),
                 mesh.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(AttrPos);
    glVertexAttribPointer(AttrPos, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(AttrNormal);
    glVertexAttribPointer(AttrNormal, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(AttrUV);
    glVertexAttribPointer(AttrUV, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(AttrTangent);
    glVertexAttribPointer(AttrTangent, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void *>(8 * sizeof(float)));
    glBindVertexArray(0);

    m_indexCount = static_cast<GLsizei>(mesh.indices.size());
    m_coverageValid = false;
}

GLuint Renderer::createTexture(const QSize &size, GLenum internalFormat, GLenum format, GLenum type,
                               bool mipmaps)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (format == GL_RGBA) {
        // Start transparent.
        const std::vector<uint8_t> zeros(static_cast<size_t>(size.width()) * size.height() * 4, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), size.width(),
                     size.height(), 0, format, type, zeros.data());
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internalFormat), size.width(),
                     size.height(), 0, format, type, nullptr);
    }
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (ctx && ctx->hasExtension(QByteArrayLiteral("GL_EXT_texture_filter_anisotropic"))) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 8.0f);
        }
    } else {
        const GLint filter = format == GL_DEPTH_COMPONENT ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    return tex;
}

void Renderer::setTextureSize(const QSize &size)
{
    if (size == m_textureSize && m_channelTex[0]) {
        return;
    }
    glDeleteTextures(kChannelCount, m_channelTex);
    for (int i = 0; i < kChannelCount; ++i) {
        m_channelTex[i] = createTexture(size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, true);
        m_channelDirty[i] = false;
    }
    m_textureSize = size;
    m_coverageValid = false;
}

void Renderer::uploadChannel(Channel c, const QImage &rgba, const QPoint &offset)
{
    if (rgba.isNull()) {
        return;
    }
    const QImage img = rgba.format() == QImage::Format_RGBA8888
                           ? rgba
                           : rgba.convertToFormat(QImage::Format_RGBA8888);
    glBindTexture(GL_TEXTURE_2D, m_channelTex[static_cast<int>(c)]);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, offset.x(), offset.y(), img.width(), img.height(), GL_RGBA,
                    GL_UNSIGNED_BYTE, img.constBits());
    m_channelDirty[static_cast<int>(c)] = true;
}

void Renderer::finishChannelUploads()
{
    for (int i = 0; i < kChannelCount; ++i) {
        if (m_channelDirty[i]) {
            glBindTexture(GL_TEXTURE_2D, m_channelTex[i]);
            glGenerateMipmap(GL_TEXTURE_2D);
            m_channelDirty[i] = false;
        }
    }
}

void Renderer::setStrokeLayerSize(const QSize &size)
{
    if (size == m_strokeSize && m_strokeTex) {
        return;
    }
    glDeleteTextures(1, &m_strokeTex);
    m_strokeTex = createTexture(size, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false);
    m_strokeSize = size;
}

void Renderer::uploadStrokeLayer(const QImage &premultiplied, const QPoint &offset)
{
    if (premultiplied.isNull()) {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, m_strokeTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, offset.x(), offset.y(), premultiplied.width(),
                    premultiplied.height(), GL_RGBA, GL_UNSIGNED_BYTE, premultiplied.constBits());
}

void Renderer::clearStrokeLayer()
{
    const QSize size = m_strokeSize;
    m_strokeSize = QSize();
    setStrokeLayerSize(size.isValid() ? size : QSize(1, 1));
}

void Renderer::drawMesh()
{
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Renderer::draw(const DrawParams &p, GLuint targetFbo)
{
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, p.viewportDevice.width(), p.viewportDevice.height());
    // Blender's default 3D viewport background (#3d3d3d).
    glClearColor(0.239f, 0.239f, 0.239f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    if (hasMesh()) {
        m_meshProgram.bind();
        setMat4(this, m_meshProgram, "uViewProj", p.viewProj);
        setVec3(m_meshProgram, "uCameraPos", p.cameraPos);
        setVec3(m_meshProgram, "uViewDir", p.viewDir);
        m_meshProgram.setUniformValue("uOrtho", p.ortho ? 1 : 0);
        m_meshProgram.setUniformValue("uDisplayMode", static_cast<int>(p.displayMode));
        m_meshProgram.setUniformValue("uNormalYSign", p.normalMapDirectX ? -1.0f : 1.0f);
        m_meshProgram.setUniformValue("uStrokeMode", static_cast<int>(p.strokeMode));
        m_meshProgram.setUniformValue("uStrokeChannels",
                                      p.strokeChannels[0] ? 1.0f : 0.0f,
                                      p.strokeChannels[1] ? 1.0f : 0.0f,
                                      p.strokeChannels[2] ? 1.0f : 0.0f,
                                      p.strokeChannels[3] ? 1.0f : 0.0f);
        m_meshProgram.setUniformValue("uStrokeValues", p.strokeRoughness, p.strokeMetallic);
        m_meshProgram.setUniformValue("uViewportSize", float(p.viewportDevice.width()),
                                      float(p.viewportDevice.height()));
        m_meshProgram.setUniformValue("uFadeStart", p.fadeStart);

        const auto &bc = channelInfo(Channel::BaseColor).defaultRgba;
        m_meshProgram.setUniformValue("uDefaultBase", bc[0] / 255.0f, bc[1] / 255.0f, bc[2] / 255.0f);
        m_meshProgram.setUniformValue("uDefaultRoughness",
                                      channelInfo(Channel::Roughness).defaultRgba[0] / 255.0f);
        m_meshProgram.setUniformValue("uDefaultMetallic",
                                      channelInfo(Channel::Metallic).defaultRgba[0] / 255.0f);

        // Studio lights fixed to the camera (key, fill, rim), view space.
        const Quat toWorld = p.viewQuat.conjugated();
        const Vec3 lightDirs[3] = {toWorld.rotate(normalized(Vec3(-0.45f, 0.55f, 0.70f))),
                                   toWorld.rotate(normalized(Vec3(0.65f, 0.15f, 0.55f))),
                                   toWorld.rotate(normalized(Vec3(0.10f, 0.70f, -0.70f)))};
        const float lightColors[9] = {2.6f, 2.5f, 2.35f, 0.55f, 0.6f, 0.7f, 1.2f, 1.2f, 1.25f};
        const float dirs[9] = {lightDirs[0].x, lightDirs[0].y, lightDirs[0].z,
                               lightDirs[1].x, lightDirs[1].y, lightDirs[1].z,
                               lightDirs[2].x, lightDirs[2].y, lightDirs[2].z};
        glUniform3fv(m_meshProgram.uniformLocation("uLightDir[0]"), 3, dirs);
        glUniform3fv(m_meshProgram.uniformLocation("uLightColor[0]"), 3, lightColors);

        const char *samplers[] = {"uBaseColor", "uRoughness", "uMetallic", "uNormalMap"};
        for (int i = 0; i < kChannelCount; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, m_channelTex[i]);
            m_meshProgram.setUniformValue(samplers[i], i);
        }
        glActiveTexture(GL_TEXTURE0 + kChannelCount);
        glBindTexture(GL_TEXTURE_2D, m_strokeTex);
        m_meshProgram.setUniformValue("uStroke", kChannelCount);
        glActiveTexture(GL_TEXTURE0);

        drawMesh();
        m_meshProgram.release();
    }

    if (p.showGrid) {
        // Adaptive spacing like Blender: 1 m at a 10 m view distance.
        const float scale = std::pow(10.0f, std::floor(std::log10(std::max(p.dist, 1e-3f))) - 1.0f);
        const float extent = std::clamp(p.dist * 30.0f, 10.0f * scale, 9000.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        m_gridProgram.bind();
        setMat4(this, m_gridProgram, "uViewProj", p.viewProj);
        m_gridProgram.setUniformValue("uCenter", p.pivot.x, p.pivot.y);
        m_gridProgram.setUniformValue("uExtent", extent);
        m_gridProgram.setUniformValue("uScale", scale);
        glBindVertexArray(m_gridVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        m_gridProgram.release();
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

void Renderer::ensureDepthTarget(const QSize &size)
{
    if (size == m_depthSize && m_depthFbo) {
        return;
    }
    glDeleteTextures(1, &m_depthTex);
    glDeleteFramebuffers(1, &m_depthFbo);
    m_depthTex = createTexture(size, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, false);
    glGenFramebuffers(1, &m_depthFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    const GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
    glReadBuffer(GL_NONE);
    m_depthSize = size;
}

void Renderer::ensureBakeTarget()
{
    if (m_bakeSize == m_textureSize && m_bakeFbo) {
        return;
    }
    glDeleteTextures(1, &m_bakeTex);
    glDeleteFramebuffers(1, &m_bakeFbo);
    m_bakeTex = createTexture(m_textureSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, false);
    glGenFramebuffers(1, &m_bakeFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bakeFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bakeTex, 0);
    m_bakeSize = m_textureSize;
}

bool Renderer::bakeStroke(const BakeParams &p, std::vector<uint8_t> &out, GLuint restoreFbo)
{
    if (!hasMesh() || p.region.isEmpty() || m_textureSize.isEmpty()) {
        return false;
    }

    // 1. Depth of the scene as seen when the stroke was painted.
    ensureDepthTarget(p.viewportDevice);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthFbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
        return false;
    }
    glViewport(0, 0, p.viewportDevice.width(), p.viewportDevice.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    m_depthProgram.bind();
    setMat4(this, m_depthProgram, "uViewProj", p.viewProj);
    drawMesh();
    m_depthProgram.release();

    // 2. Project the stroke onto every visible texel inside the region.
    ensureBakeTarget();
    glBindFramebuffer(GL_FRAMEBUFFER, m_bakeFbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
        return false;
    }
    glViewport(0, 0, m_textureSize.width(), m_textureSize.height());
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_SCISSOR_TEST);
    glScissor(p.region.x, p.region.y, p.region.w, p.region.h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    m_bakeProgram.bind();
    setMat4(this, m_bakeProgram, "uStrokeViewProj", p.viewProj);
    setVec3(m_bakeProgram, "uCameraPos", p.cameraPos);
    setVec3(m_bakeProgram, "uViewDir", p.viewDir);
    m_bakeProgram.setUniformValue("uOrtho", p.ortho ? 1 : 0);
    m_bakeProgram.setUniformValue("uNear", p.nearClip);
    m_bakeProgram.setUniformValue("uFar", p.farClip);
    m_bakeProgram.setUniformValue("uFadeStart", p.fadeStart);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_strokeTex);
    m_bakeProgram.setUniformValue("uStroke", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    m_bakeProgram.setUniformValue("uDepth", 1);
    glActiveTexture(GL_TEXTURE0);
    drawMesh();
    m_bakeProgram.release();

    out.resize(static_cast<size_t>(p.region.w) * p.region.h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(p.region.x, p.region.y, p.region.w, p.region.h, GL_RGBA, GL_UNSIGNED_BYTE,
                 out.data());

    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
    return true;
}

const std::vector<uint8_t> &Renderer::coverage(GLuint restoreFbo)
{
    if (m_coverageValid || !hasMesh() || m_textureSize.isEmpty()) {
        return m_coverage;
    }
    ensureBakeTarget();
    glBindFramebuffer(GL_FRAMEBUFFER, m_bakeFbo);
    glViewport(0, 0, m_textureSize.width(), m_textureSize.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    m_coverageProgram.bind();
    drawMesh();
    m_coverageProgram.release();

    std::vector<uint8_t> rgba(static_cast<size_t>(m_textureSize.width()) * m_textureSize.height() * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, m_textureSize.width(), m_textureSize.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
    m_coverage.resize(rgba.size() / 4);
    for (size_t i = 0; i < m_coverage.size(); ++i) {
        m_coverage[i] = rgba[i * 4];
    }
    m_coverageValid = true;

    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, restoreFbo);
    return m_coverage;
}
