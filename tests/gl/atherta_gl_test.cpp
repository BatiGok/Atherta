/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GL integration test for the viewport and the stroke bake. Needs a display
 * (Xvfb with Mesa works):
 *
 *   atherta_gl_test [--gles] [output-dir]
 *
 * Renders a UV sphere in every display mode (screenshots are written to
 * output-dir), bakes a disc-shaped screen-space stroke and checks that it
 * lands on the visible texels only.
 */

#include "core/GutterDilation.h"
#include "core/MeshBvh.h"
#include "core/StrokeRegion.h"
#include "plugin/Viewport.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QSurfaceFormat>

#include <cmath>
#include <cstdio>

using namespace atherta;

namespace {

int g_failures = 0;

#define EXPECT(cond, ...)                                                                          \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++g_failures;                                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s ", __FILE__, __LINE__, #cond);                    \
            std::fprintf(stderr, __VA_ARGS__);                                                     \
            std::fprintf(stderr, "\n");                                                            \
        } else {                                                                                   \
            std::fprintf(stdout, "ok   %s\n", #cond);                                              \
        }                                                                                          \
    } while (0)

Mesh makeUvSphere(int segments, int rings, float radius)
{
    std::vector<MeshCorner> corners;
    auto vertex = [&](int s, int r) {
        const float u = float(s) / segments;
        const float v = float(r) / rings;
        const float theta = u * 2.0f * kPi;
        const float phi = v * kPi; // 0 at the bottom pole
        MeshCorner c;
        c.normal = Vec3(std::sin(phi) * std::cos(theta), std::sin(phi) * std::sin(theta), -std::cos(phi));
        c.position = c.normal * radius;
        c.uv = Vec2(u, v);
        return c;
    };
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const MeshCorner a = vertex(s, r), b = vertex(s + 1, r);
            const MeshCorner c = vertex(s + 1, r + 1), d = vertex(s, r + 1);
            for (const MeshCorner &m : {a, b, c, a, c, d}) {
                corners.push_back(m);
            }
        }
    }
    Mesh mesh;
    buildMeshFromCorners(corners, {}, true, true, mesh);
    return mesh;
}

QImage checker(int size, QColor a, QColor b)
{
    QImage img(size, size, QImage::Format_RGBA8888);
    QPainter p(&img);
    const int cell = size / 8;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            p.fillRect(x * cell, y * cell, cell, cell, (x + y) % 2 ? a : b);
        }
    }
    return img;
}

QImage flat(int size, QColor c)
{
    QImage img(size, size, QImage::Format_RGBA8888);
    img.fill(c);
    return img;
}

void waitFor(QApplication &app, const std::function<bool()> &cond, int ms = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        app.processEvents(QEventLoop::AllEvents, 20);
    }
}

bool isBackground(QRgb c)
{
    return std::abs(qRed(c) - 61) < 4 && std::abs(qGreen(c) - 61) < 4 && std::abs(qBlue(c) - 61) < 4;
}

} // namespace

int main(int argc, char **argv)
{
    bool gles = false;
    QString outDir = QDir::tempPath();
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--gles")) {
            gles = true;
        } else {
            outDir = arg;
        }
    }

    // Same requests as Krita (kis_opengl.cpp): desktop 3.3 compatibility or GLES 3.0.
    QSurfaceFormat fmt;
    if (gles) {
        QCoreApplication::setAttribute(Qt::AA_UseOpenGLES);
        fmt.setRenderableType(QSurfaceFormat::OpenGLES);
        fmt.setVersion(3, 0);
    } else {
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
    }
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    QApplication app(argc, argv);
    const QString tag = gles ? QStringLiteral("gles") : QStringLiteral("gl");

    Viewport vp;
    QString glError;
    QObject::connect(&vp, &Viewport::glError, [&](const QString &e) { glError = e; });
    vp.resize(800, 600);
    vp.show();
    waitFor(app, [&] { return vp.isGLReady() || !glError.isEmpty(); });
    EXPECT(vp.isGLReady(), "GL init failed: %s", qPrintable(glError));
    if (!vp.isGLReady()) {
        return 1;
    }
    vp.makeCurrent();
    std::printf("context: %s %d.%d\n", vp.context()->isOpenGLES() ? "GLES" : "GL",
                vp.format().majorVersion(), vp.format().minorVersion());
    vp.doneCurrent();
    EXPECT(vp.context()->isOpenGLES() == gles, "unexpected context type");

    const Mesh mesh = makeUvSphere(64, 32, 1.0f);
    MeshBvh bvh;
    bvh.build(mesh);
    vp.setMesh(&mesh, &bvh);
    vp.frameAll(false);

    const int texSize = 512;
    vp.withContext([&](Renderer &r, GLuint) {
        r.setTextureSize(QSize(texSize, texSize));
        r.uploadChannel(Channel::BaseColor, checker(texSize, QColor(200, 60, 40), QColor(230, 230, 230)), {});
        r.uploadChannel(Channel::Roughness, flat(texSize, QColor(90, 90, 90)), {});
        r.uploadChannel(Channel::Metallic, flat(texSize, QColor(0, 0, 0)), {});
        r.uploadChannel(Channel::Normal, flat(texSize, QColor(128, 128, 255)), {});
        r.finishChannelUploads();
    });

    // --- Display modes -----------------------------------------------------
    const char *modeNames[] = {"material", "basecolor", "roughness", "metallic", "normal"};
    for (int m = 0; m < 5; ++m) {
        vp.setDisplayMode(static_cast<Renderer::DisplayMode>(m));
        const QImage shot = vp.grabFramebuffer();
        shot.save(QDir(outDir).filePath(QStringLiteral("atherta_%1_%2.png").arg(tag, modeNames[m])));
        const QRgb centre = shot.pixel(shot.width() / 2, shot.height() / 2);
        const QRgb corner = shot.pixel(5, shot.height() - 5);
        EXPECT(!isBackground(centre), "%s: sphere not drawn at the centre", modeNames[m]);
        EXPECT(isBackground(corner) || m == 0, "%s: corner should be background", modeNames[m]);
        if (m == 2) {
            EXPECT(std::abs(qRed(centre) - 90) <= 2, "roughness view shows %d", qRed(centre));
        }
    }
    vp.setDisplayMode(Renderer::DisplayMode::Material);

    // --- Bake a disc stroke at the viewport centre -------------------------
    const QSize dev = vp.deviceSize();
    QImage stroke(dev, QImage::Format_RGBA8888_Premultiplied);
    stroke.fill(Qt::transparent);
    {
        QPainter p(&stroke);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(20, 40, 250));
        p.drawEllipse(QPointF(dev.width() / 2.0, dev.height() / 2.0), 40, 40);
    }

    const ViewNavigator &nav = vp.navigator();
    const Mat4 vpMat = nav.viewProjectionMatrix();
    const IRect screenRect{dev.width() / 2 - 41, dev.height() / 2 - 41, 82, 82};
    const IRect region = textureRegionForScreenRect(mesh, vpMat, dev.width(), dev.height(),
                                                    screenRect, texSize, texSize, 8);
    EXPECT(!region.isEmpty(), "empty bake region");
    EXPECT(region.w < texSize, "region should not span the whole texture width (%d)", region.w);

    std::vector<uint8_t> baked;
    std::vector<uint8_t> coverage;
    bool bakedOk = false;
    vp.withContext([&](Renderer &r, GLuint fbo) {
        r.setStrokeLayerSize(dev);
        r.uploadStrokeLayer(stroke, QPoint(0, 0));
        Renderer::BakeParams p;
        p.viewProj = vpMat;
        p.cameraPos = nav.eyePosition();
        p.viewDir = nav.viewDirection();
        p.ortho = nav.state().projection == Projection::Orthographic;
        p.nearClip = nav.nearClip();
        p.farClip = nav.farClip();
        p.projection11 = nav.projectionMatrix().at(1, 1);
        p.viewportDevice = dev;
        p.region = region;
        bakedOk = r.bakeStroke(p, baked, fbo);
        coverage = r.coverage(fbo);
    });
    EXPECT(bakedOk, "bake failed");

    // The texel under the screen centre must be painted blue...
    const QPointF centre(vp.width() / 2.0, vp.height() / 2.0);
    const RayHit hit = bvh.intersect(nav.rayThroughPixel(Vec2(float(centre.x()), float(centre.y()))));
    EXPECT(hit.hit, "centre ray missed the sphere");
    auto texel = [&](const Vec2 &uv) -> const uint8_t * {
        const int x = int(uv.x * texSize) - region.x;
        const int y = int((1.0f - uv.y) * texSize) - region.y;
        if (x < 0 || y < 0 || x >= region.w || y >= region.h) {
            return nullptr;
        }
        return &baked[(size_t(y) * region.w + x) * 4];
    };
    if (bakedOk && hit.hit) {
        const uint8_t *t = texel(hit.uv);
        EXPECT(t != nullptr, "centre texel outside the region");
        if (t) {
            EXPECT(t[3] > 240, "centre texel alpha %d", t[3]);
            EXPECT(t[2] > 200 && t[0] < 60, "centre texel colour %d %d %d", t[0], t[1], t[2]);
        }
        // ...and the point on the far side of the sphere must stay untouched.
        const Ray behind{hit.position + nav.viewDirection() * 3.0f, -nav.viewDirection()};
        const RayHit back = bvh.intersect(behind);
        EXPECT(back.hit, "back ray missed");
        const uint8_t *tb = back.hit ? texel(back.uv) : nullptr;
        EXPECT(tb == nullptr || tb[3] == 0, "occluded texel was painted (alpha %d)", tb ? tb[3] : -1);

        // Painted area roughly matches the disc (pi * 40^2 px seen on a sphere).
        size_t painted = 0;
        for (size_t i = 3; i < baked.size(); i += 4) {
            painted += baked[i] > 128;
        }
        std::printf("painted texels: %zu\n", painted);
        EXPECT(painted > 200, "too few painted texels");

        QImage out(baked.data(), region.w, region.h, region.w * 4, QImage::Format_RGBA8888_Premultiplied);
        out.copy().save(QDir(outDir).filePath(QStringLiteral("atherta_%1_baked.png").arg(tag)));
    }

    // --- Grazing angles must not alias into stripes (depth acne) -------------
    {
        QImage full(dev, QImage::Format_RGBA8888_Premultiplied);
        full.fill(QColor(255, 255, 255));
        std::vector<uint8_t> all;
        const IRect whole{0, 0, texSize, texSize};
        bool ok = false;
        vp.withContext([&](Renderer &r, GLuint fbo) {
            r.uploadStrokeLayer(full, QPoint(0, 0));
            Renderer::BakeParams p;
            p.viewProj = vpMat;
            p.cameraPos = nav.eyePosition();
            p.viewDir = nav.viewDirection();
            p.ortho = false;
            p.nearClip = nav.nearClip();
            p.farClip = nav.farClip();
            p.projection11 = nav.projectionMatrix().at(1, 1);
            p.fadeStart = 0.0001f; // no fade: every visible texel must be fully painted
            p.viewportDevice = dev;
            p.region = whole;
            ok = r.bakeStroke(p, all, fbo);
        });
        EXPECT(ok, "full bake failed");
        size_t holes = 0, paintedTexels = 0;
        auto alphaAt = [&](int x, int y) { return all[(size_t(y) * texSize + x) * 4 + 3]; };
        for (int y = 1; y + 1 < texSize; ++y) {
            for (int x = 1; x + 1 < texSize; ++x) {
                const bool neighboursPainted = alphaAt(x - 1, y) > 200 && alphaAt(x + 1, y) > 200 &&
                                               alphaAt(x, y - 1) > 200 && alphaAt(x, y + 1) > 200;
                paintedTexels += alphaAt(x, y) > 200;
                holes += neighboursPainted && alphaAt(x, y) < 50;
            }
        }
        std::printf("visible texels: %zu, holes: %zu\n", paintedTexels, holes);

        // No texel facing away from the camera may be painted (closed mesh).
        size_t backPainted = 0;
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            Vec3 c, n;
            Vec2 uv;
            for (int k = 0; k < 3; ++k) {
                c += mesh.positions[mesh.indices[t + k]] / 3.0f;
                n += mesh.normals[mesh.indices[t + k]];
                uv = uv + mesh.uvs[mesh.indices[t + k]] / 3.0f;
            }
            const Vec3 toEye = normalized(nav.eyePosition() - c);
            if (dot(normalized(n), toEye) > -0.15f) {
                continue; // only clearly back-facing triangles
            }
            const int x = std::clamp(int(uv.x * texSize), 0, texSize - 1);
            const int y = std::clamp(int((1.0f - uv.y) * texSize), 0, texSize - 1);
            backPainted += alphaAt(x, y) > 0;
        }
        std::printf("back-facing triangles painted: %zu\n", backPainted);
        EXPECT(backPainted == 0, "%zu back-facing triangles received paint", backPainted);
        // Roughly the front hemisphere is visible.
        EXPECT(paintedTexels > size_t(texSize * texSize / 4), "too few visible texels");

        // Every clearly front-facing triangle (the sphere is convex, so it is
        // visible) must be painted everywhere inside: depth acne shows up as
        // missed samples. Isolated holes right on the silhouette do not matter,
        // the facing fade removes paint there anyway.
        size_t frontSamples = 0, frontMissed = 0;
        const float bary[4][3] = {{1 / 3.f, 1 / 3.f, 1 / 3.f}, {2 / 3.f, 1 / 6.f, 1 / 6.f},
                                  {1 / 6.f, 2 / 3.f, 1 / 6.f}, {1 / 6.f, 1 / 6.f, 2 / 3.f}};
        for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
            const uint32_t i0 = mesh.indices[t], i1 = mesh.indices[t + 1], i2 = mesh.indices[t + 2];
            const Vec3 c = (mesh.positions[i0] + mesh.positions[i1] + mesh.positions[i2]) / 3.0f;
            const Vec3 n = normalized(mesh.normals[i0] + mesh.normals[i1] + mesh.normals[i2]);
            if (dot(n, normalized(nav.eyePosition() - c)) < 0.1f) {
                continue;
            }
            for (const auto &b : bary) {
                const Vec2 uv = mesh.uvs[i0] * b[0] + mesh.uvs[i1] * b[1] + mesh.uvs[i2] * b[2];
                const int x = std::clamp(int(uv.x * texSize), 0, texSize - 1);
                const int y = std::clamp(int((1.0f - uv.y) * texSize), 0, texSize - 1);
                ++frontSamples;
                frontMissed += alphaAt(x, y) < 200;
            }
        }
        std::printf("front-facing samples: %zu, missed: %zu\n", frontSamples, frontMissed);
        EXPECT(frontSamples > 1000, "too few front samples");
        EXPECT(frontMissed == 0, "%zu visible samples were not painted (depth acne)", frontMissed);
        QImage out(all.data(), texSize, texSize, texSize * 4, QImage::Format_RGBA8888_Premultiplied);
        out.copy().save(QDir(outDir).filePath(QStringLiteral("atherta_%1_visibility.png").arg(tag)));

        // Restore the disc stroke for the overlay check below.
        vp.withContext([&](Renderer &r, GLuint) { r.uploadStrokeLayer(stroke, QPoint(0, 0)); });
    }

    size_t covered = 0;
    for (uint8_t c : coverage) {
        covered += c != 0;
    }
    std::printf("coverage: %.1f%%\n", 100.0 * covered / double(texSize * texSize));
    EXPECT(coverage.size() == size_t(texSize * texSize), "coverage size");
    EXPECT(covered > size_t(texSize * texSize * 0.95), "sphere UVs should cover the texture");

    // --- Live stroke overlay -----------------------------------------------
    vp.drawParams().strokeMode = Renderer::StrokeMode::Paint;
    vp.drawParams().strokeChannels[0] = true;
    vp.setDisplayMode(Renderer::DisplayMode::BaseColor);
    const QImage overlay = vp.grabFramebuffer();
    overlay.save(QDir(outDir).filePath(QStringLiteral("atherta_%1_overlay.png").arg(tag)));
    const QRgb oc = overlay.pixel(overlay.width() / 2, overlay.height() / 2);
    EXPECT(qBlue(oc) > 200 && qRed(oc) < 60, "overlay centre %d %d %d", qRed(oc), qGreen(oc), qBlue(oc));

    // --- Navigation keeps rendering ----------------------------------------
    vp.navigator().setAxisView(AxisView::Top, 0.0);
    vp.navigator().finishAnimation();
    vp.drawParams().strokeMode = Renderer::StrokeMode::None;
    vp.setDisplayMode(Renderer::DisplayMode::Material);
    vp.grabFramebuffer().save(QDir(outDir).filePath(QStringLiteral("atherta_%1_top.png").arg(tag)));

    std::printf("%s: %d failure(s)\n", qPrintable(tag), g_failures);
    return g_failures == 0 ? 0 : 1;
}
