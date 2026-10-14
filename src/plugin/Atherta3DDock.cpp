/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Atherta3DDock.h"

#include "core/GutterDilation.h"
#include "core/StrokeRegion.h"
#include "StrokeSession.h"

#include <KisDocument.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisViewManager.h>
#include <KoCanvasBase.h>
#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoCompositeOpRegistry.h>
#include <KoInputDevice.h>
#include <KoToolManager.h>
#include <kis_canvas2.h>
#include <kis_command_utils.h>
#include <kis_config.h>
#include <kis_icon_utils.h>
#include <kis_image.h>
#include <kis_paint_device.h>
#include <kis_paintop_preset.h>
#include <kis_paintop_settings.h>
#include <kis_painter.h>
#include <kis_processing_applicator.h>
#include <kis_selection.h>
#include <kis_transaction.h>
#include <klocalizedstring.h>
#include <kundo2magicstring.h>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QSlider>
#include <QTabletEvent>
#include <QToolButton>
#include <QVBoxLayout>

using namespace atherta;

namespace {

KConfigGroup settingsGroup()
{
    return KSharedConfig::openConfig()->group("Atherta3DPaint");
}

QToolButton *makeButton(const QString &text, const QString &icon, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    if (!icon.isEmpty()) {
        b->setIcon(KisIconUtils::loadIcon(icon));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    b->setAutoRaise(true);
    return b;
}

QSlider *makeValueSlider(int value, QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(0, 100);
    s->setValue(value);
    s->setMinimumWidth(50);
    return s;
}

} // namespace

Atherta3DDock::Atherta3DDock()
    : QDockWidget(i18n("3D Paint"))
{
    buildUi();
    loadSettings();
    applySettingsToViewport();

    m_syncTimer.setSingleShot(true);
    m_syncTimer.setInterval(33);
    connect(&m_syncTimer, &QTimer::timeout, this, &Atherta3DDock::slotFlushTextureUpdates);

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(50);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this]() {
        KisNodeSP before[kChannelCount];
        for (int i = 0; i < kChannelCount; ++i) {
            before[i] = m_textureSet.channelGroup(static_cast<Channel>(i));
        }
        m_textureSet.refresh();
        bool changed = false;
        for (int i = 0; i < kChannelCount; ++i) {
            changed = changed || before[i] != m_textureSet.channelGroup(static_cast<Channel>(i));
        }
        if (changed) {
            fullTextureSync();
        }
        updateState();
    });

    updateState();
}

Atherta3DDock::~Atherta3DDock()
{
    m_stroke.reset();
}

// ---------------------------------------------------------------------------
// UI

void Atherta3DDock::buildUi()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Row 1: model, texture set, frame, settings.
    auto *row1 = new QHBoxLayout();
    QToolButton *load = makeButton(i18n("Load Model..."), QStringLiteral("document-open"), page);
    load->setToolTip(i18n("Load a UV-unwrapped OBJ or FBX model"));
    connect(load, &QToolButton::clicked, this, &Atherta3DDock::slotLoadModel);
    row1->addWidget(load);

    m_textureSetButton = makeButton(i18n("Texture Set"), QStringLiteral("layer-group-add"), page);
    m_textureSetButton->setPopupMode(QToolButton::InstantPopup);
    auto *tsMenu = new QMenu(m_textureSetButton);
    tsMenu->addAction(i18n("Create in Current Document"), this, &Atherta3DDock::slotCreateTextureSet);
    QMenu *newDoc = tsMenu->addMenu(i18n("New Texture Document"));
    for (int size : {1024, 2048, 4096}) {
        newDoc->addAction(QStringLiteral("%1 x %1").arg(size), this,
                          [this, size]() { slotNewDocument(size); });
    }
    tsMenu->addSeparator();
    tsMenu->addAction(i18n("Export Textures..."), this, &Atherta3DDock::slotExportTextures);
    m_textureSetButton->setMenu(tsMenu);
    row1->addWidget(m_textureSetButton);

    QToolButton *frame = makeButton(i18n("Frame"), QStringLiteral("zoom-fit"), page);
    frame->setToolTip(i18n("Frame the model (Home / Numpad .)"));
    row1->addWidget(frame);
    row1->addStretch(1);

    QToolButton *settings = makeButton(QString(), QString(), page);
    settings->setIcon(KisIconUtils::loadIcon(QStringLiteral("configure")));
    settings->setToolTip(i18n("Viewport settings"));
    settings->setPopupMode(QToolButton::InstantPopup);
    auto *settingsMenu = new QMenu(settings);
    auto addToggle = [&](const QString &key, const QString &text, bool def) {
        QAction *a = settingsMenu->addAction(text);
        a->setCheckable(true);
        a->setChecked(settingsGroup().readEntry(key, def));
        a->setData(key);
        connect(a, &QAction::toggled, this, [this, key](bool on) {
            settingsGroup().writeEntry(key, on);
            applySettingsToViewport();
        });
    };
    addToggle(QStringLiteral("trackball"), i18n("Trackball Orbit"), false);
    addToggle(QStringLiteral("zoomToMouse"), i18n("Zoom to Mouse Position"), false);
    addToggle(QStringLiteral("autoPerspective"), i18n("Auto Perspective"), true);
    addToggle(QStringLiteral("invertZoom"), i18n("Invert Zoom Direction"), false);
    addToggle(QStringLiteral("emulate3Button"), i18n("Emulate 3 Button Mouse (Alt+LMB)"), false);
    settingsMenu->addSeparator();
    addToggle(QStringLiteral("fingerPainting"), i18n("Paint with Finger"), false);
    addToggle(QStringLiteral("showGrid"), i18n("Show Grid"), true);
    addToggle(QStringLiteral("normalDirectX"), i18n("DirectX Normal Maps (Y-)"), false);
    settings->setMenu(settingsMenu);
    row1->addWidget(settings);
    layout->addLayout(row1);

    // Row 2: which channel the viewport shows.
    auto *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel(i18n("View:"), page));
    m_viewCombo = new QComboBox(page);
    m_viewCombo->addItems({i18n("Material"), i18n("Base Color"), i18n("Roughness"),
                           i18n("Metallic"), i18n("Normal")});
    m_viewCombo->setToolTip(i18n("Channel shown in the viewport (C cycles)"));
    row2->addWidget(m_viewCombo, 1);
    layout->addLayout(row2);

    // Row 3: which channels the brush paints, and the values for data channels.
    auto *row3 = new QHBoxLayout();
    row3->addWidget(new QLabel(i18n("Paint:"), page));
    m_paintColor = new QCheckBox(i18n("Color"), page);
    m_paintColor->setChecked(true);
    m_paintRoughness = new QCheckBox(i18n("Rough"), page);
    m_roughnessValue = makeValueSlider(50, page);
    m_paintMetallic = new QCheckBox(i18n("Metal"), page);
    m_metallicValue = makeValueSlider(0, page);
    m_paintNormal = new QCheckBox(i18n("Normal"), page);
    m_paintRoughness->setToolTip(i18n("Paint the roughness channel with the value on the right"));
    m_paintMetallic->setToolTip(i18n("Paint the metallic channel with the value on the right"));
    m_paintNormal->setToolTip(i18n("Paint the normal channel with the brush colour"));
    row3->addWidget(m_paintColor);
    row3->addWidget(m_paintRoughness);
    row3->addWidget(m_roughnessValue, 1);
    row3->addWidget(m_paintMetallic);
    row3->addWidget(m_metallicValue, 1);
    row3->addWidget(m_paintNormal);
    layout->addLayout(row3);

    m_viewport = new Viewport(page);
    m_viewport->setPaintTarget(this);
    layout->addWidget(m_viewport, 1);

    m_status = new QLabel(page);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    setWidget(page);

    connect(frame, &QToolButton::clicked, this, [this]() { m_viewport->frameAll(true); });
    connect(m_viewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_viewport->setDisplayMode(static_cast<Renderer::DisplayMode>(i));
    });
    connect(m_viewport, &Viewport::displayModeChanged, this, [this](int mode) {
        QSignalBlocker blocker(m_viewCombo);
        m_viewCombo->setCurrentIndex(mode);
    });
    connect(m_viewport, &Viewport::glReset, this, &Atherta3DDock::slotGlReset);
    connect(m_viewport, &Viewport::glError, this, [this](const QString &error) {
        m_status->setText(i18n("OpenGL error: %1", error));
    });
    for (QCheckBox *cb : {m_paintColor, m_paintRoughness, m_paintMetallic, m_paintNormal}) {
        connect(cb, &QCheckBox::toggled, this, [this]() {
            saveSettings();
            updateState();
        });
    }
    for (QSlider *s : {m_roughnessValue, m_metallicValue}) {
        connect(s, &QSlider::valueChanged, this, [this]() { saveSettings(); });
    }
}

void Atherta3DDock::loadSettings()
{
    KConfigGroup cfg = settingsGroup();
    m_paintColor->setChecked(cfg.readEntry("paintColor", true));
    m_paintRoughness->setChecked(cfg.readEntry("paintRoughness", false));
    m_paintMetallic->setChecked(cfg.readEntry("paintMetallic", false));
    m_paintNormal->setChecked(cfg.readEntry("paintNormal", false));
    m_roughnessValue->setValue(cfg.readEntry("roughnessValue", 50));
    m_metallicValue->setValue(cfg.readEntry("metallicValue", 0));
    m_padding = cfg.readEntry("padding", 8);
    m_fadeStart = static_cast<float>(cfg.readEntry("fadeStart", 0.2));
}

void Atherta3DDock::saveSettings()
{
    KConfigGroup cfg = settingsGroup();
    cfg.writeEntry("paintColor", m_paintColor->isChecked());
    cfg.writeEntry("paintRoughness", m_paintRoughness->isChecked());
    cfg.writeEntry("paintMetallic", m_paintMetallic->isChecked());
    cfg.writeEntry("paintNormal", m_paintNormal->isChecked());
    cfg.writeEntry("roughnessValue", m_roughnessValue->value());
    cfg.writeEntry("metallicValue", m_metallicValue->value());
}

void Atherta3DDock::applySettingsToViewport()
{
    KConfigGroup cfg = settingsGroup();
    NavigationPreferences &prefs = m_viewport->navigator().preferences();
    prefs.orbitMethod = cfg.readEntry("trackball", false) ? OrbitMethod::Trackball : OrbitMethod::Turntable;
    prefs.zoomToMousePosition = cfg.readEntry("zoomToMouse", false);
    prefs.autoPerspective = cfg.readEntry("autoPerspective", true);
    prefs.invertZoomDirection = cfg.readEntry("invertZoom", false);
    m_viewport->options().emulate3ButtonMouse = cfg.readEntry("emulate3Button", false);
    m_viewport->options().fingerPainting = cfg.readEntry("fingerPainting", false);
    m_viewport->options().showGrid = cfg.readEntry("showGrid", true);
    m_viewport->drawParams().normalMapDirectX = cfg.readEntry("normalDirectX", false);
    m_viewport->update();
}

bool Atherta3DDock::channelEnabled(Channel c) const
{
    switch (c) {
    case Channel::BaseColor: return m_paintColor->isChecked();
    case Channel::Roughness: return m_paintRoughness->isChecked();
    case Channel::Metallic: return m_paintMetallic->isChecked();
    case Channel::Normal: return m_paintNormal->isChecked();
    case Channel::Count: break;
    }
    return false;
}

float Atherta3DDock::channelValue(Channel c) const
{
    switch (c) {
    case Channel::Roughness: return m_roughnessValue->value() / 100.0f;
    case Channel::Metallic: return m_metallicValue->value() / 100.0f;
    default: return 1.0f;
    }
}

void Atherta3DDock::updateState()
{
    QString reason;
    if (canPaint(&reason)) {
        m_viewport->setMessage(QString());
    } else {
        m_viewport->setMessage(m_mesh.isEmpty() ? reason : QString());
    }

    QStringList info;
    if (!m_mesh.isEmpty()) {
        info << i18n("%1: %2 triangles", QFileInfo(m_modelPath).fileName(),
                     QLocale().toString(qulonglong(m_mesh.triangleCount())));
    }
    if (m_textureSet.isValid()) {
        const QSize s = m_textureSet.size();
        info << i18n("Texture set %1 x %2", s.width(), s.height());
    }
    if (!m_mesh.isEmpty() && !reason.isEmpty()) {
        info << reason;
    }
    m_status->setText(info.join(QStringLiteral(" | ")));
}

// ---------------------------------------------------------------------------
// Canvas

void Atherta3DDock::setCanvas(KoCanvasBase *canvas)
{
    if (m_canvas && m_canvas->image()) {
        m_canvas->image()->disconnect(this);
    }
    m_stroke.reset();
    m_canvas = dynamic_cast<KisCanvas2 *>(canvas);
    KisImageSP image = m_canvas ? KisImageSP(m_canvas->image()) : KisImageSP();
    m_textureSet.setImage(image);

    if (image) {
        KisImage *img = image.data();
        // These are emitted from Krita's worker threads.
        connect(img, &KisImage::sigImageUpdated, this, &Atherta3DDock::slotImageUpdated,
                Qt::QueuedConnection);
        connect(img, &KisImage::sigLayersChangedAsync, this, &Atherta3DDock::slotLayerStackChanged,
                Qt::QueuedConnection);
        connect(img, &KisImage::sigNodeAddedAsync, this, &Atherta3DDock::slotLayerStackChanged,
                Qt::QueuedConnection);
        connect(img, &KisImage::sigRemoveNodeAsync, this, &Atherta3DDock::slotLayerStackChanged,
                Qt::QueuedConnection);
        connect(img, &KisImage::sigNodeChanged, this, &Atherta3DDock::slotLayerStackChanged,
                Qt::QueuedConnection);
        connect(img, &KisImage::sigSizeChanged, this, &Atherta3DDock::slotImageSizeChanged,
                Qt::QueuedConnection);

        if (m_createTextureSetOnNextCanvas && !m_textureSet.isValid()) {
            m_createTextureSetOnNextCanvas = false;
            TextureSet::create(m_canvas->viewManager());
            image->waitForDone();
            m_textureSet.refresh();
        }
    }

    fullTextureSync();
    updateState();
}

void Atherta3DDock::unsetCanvas()
{
    if (m_canvas && m_canvas->image()) {
        m_canvas->image()->disconnect(this);
    }
    m_stroke.reset();
    m_canvas = nullptr;
    m_textureSet.setImage(KisImageSP());
    fullTextureSync();
    updateState();
}

// ---------------------------------------------------------------------------
// Texture synchronisation (Krita layers -> GPU)

void Atherta3DDock::fullTextureSync()
{
    if (!m_textureSet.isValid()) {
        m_viewport->withContext([](Renderer &r, GLuint) { r.setTextureSize(QSize(1, 1)); });
        return;
    }
    const QSize size = m_textureSet.size();
    m_viewport->withContext([&](Renderer &r, GLuint) {
        if (size.width() > r.maxTextureSize() || size.height() > r.maxTextureSize()) {
            m_status->setText(i18n("The document is larger than the GPU texture limit (%1).",
                                   r.maxTextureSize()));
            r.setTextureSize(QSize(1, 1));
            return;
        }
        r.setTextureSize(size);
        const QRect full(QPoint(0, 0), size);
        for (int i = 0; i < kChannelCount; ++i) {
            const Channel c = static_cast<Channel>(i);
            r.uploadChannel(c, m_textureSet.readChannel(c, full), QPoint(0, 0));
        }
        r.finishChannelUploads();
    });
}

void Atherta3DDock::syncRect(const QRect &rect)
{
    if (!m_textureSet.isValid()) {
        return;
    }
    const QRect r = rect & QRect(QPoint(0, 0), m_textureSet.size());
    if (r.isEmpty()) {
        return;
    }
    m_viewport->withContext([&](Renderer &renderer, GLuint) {
        if (renderer.textureSize() != m_textureSet.size()) {
            return;
        }
        for (int i = 0; i < kChannelCount; ++i) {
            const Channel c = static_cast<Channel>(i);
            renderer.uploadChannel(c, m_textureSet.readChannel(c, r), r.topLeft());
        }
        renderer.finishChannelUploads();
    });
}

void Atherta3DDock::slotImageUpdated(const QRect &rect)
{
    m_pendingDirty |= rect;
    if (!m_syncTimer.isActive()) {
        m_syncTimer.start();
    }
}

void Atherta3DDock::slotFlushTextureUpdates()
{
    const QRect r = m_pendingDirty;
    m_pendingDirty = QRect();
    syncRect(r);
}

void Atherta3DDock::slotLayerStackChanged()
{
    m_refreshTimer.start();
}

void Atherta3DDock::slotImageSizeChanged()
{
    m_textureSet.refresh();
    fullTextureSync();
    updateState();
}

void Atherta3DDock::slotGlReset()
{
    // New GL context: everything must be uploaded again.
    fullTextureSync();
}

// ---------------------------------------------------------------------------
// Actions

void Atherta3DDock::slotLoadModel()
{
    KConfigGroup cfg = settingsGroup();
    const QString dir = cfg.readEntry("lastModelDir", QDir::homePath());
    const QString path = QFileDialog::getOpenFileName(
        this, i18n("Load 3D Model"), dir, i18n("3D models (*.obj *.fbx);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    cfg.writeEntry("lastModelDir", QFileInfo(path).absolutePath());

    Mesh mesh;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const MeshLoadResult result = loadMesh(path.toUtf8().toStdString(), mesh);
    QApplication::restoreOverrideCursor();

    if (!result.ok) {
        QMessageBox::warning(this, i18n("Load 3D Model"),
                             i18n("Could not load %1:\n%2", QFileInfo(path).fileName(),
                                  QString::fromStdString(result.error)));
        return;
    }

    m_stroke.reset();
    m_viewport->setMesh(nullptr, nullptr);
    m_mesh = std::move(mesh);
    m_bvh.build(m_mesh);
    m_modelPath = path;
    m_viewport->setMesh(&m_mesh, &m_bvh);
    m_viewport->frameAll(false);

    if (!result.warnings.empty()) {
        QStringList warnings;
        for (const std::string &w : result.warnings) {
            warnings << QString::fromStdString(w);
        }
        QMessageBox::information(this, i18n("Load 3D Model"), warnings.join(QLatin1Char('\n')));
    }
    updateState();
}

void Atherta3DDock::slotCreateTextureSet()
{
    if (!m_canvas || !m_canvas->viewManager()) {
        QMessageBox::information(this, i18n("Texture Set"),
                                 i18n("Open a document first, or create a new texture document."));
        return;
    }
    if (m_textureSet.isValid()) {
        QMessageBox::information(this, i18n("Texture Set"),
                                 i18n("This document already has a 3D texture set."));
        return;
    }
    TextureSet::create(m_canvas->viewManager());
    m_canvas->image()->waitForDone();
    m_textureSet.refresh();
    fullTextureSync();
    updateState();
}

void Atherta3DDock::slotNewDocument(int size)
{
    KisMainWindow *mainWindow = KisPart::instance()->currentMainwindow();
    if (!mainWindow) {
        return;
    }
    const KoColorSpace *cs = KoColorSpaceRegistry::instance()->rgb8();
    const QString name = m_modelPath.isEmpty()
                             ? i18n("Textures")
                             : i18n("%1 Textures", QFileInfo(m_modelPath).completeBaseName());

    KisDocument *doc = KisPart::instance()->createDocument();
    if (!doc->newImage(name, size, size, cs, KoColor(QColor(128, 128, 128), cs),
                       KisConfig::CANVAS_COLOR, 0, QString(), 72.0 / 72.0)) {
        delete doc;
        return;
    }
    KisPart::instance()->addDocument(doc);
    // The texture set is created once the new view becomes the active canvas.
    m_createTextureSetOnNextCanvas = true;
    mainWindow->addViewAndNotifyLoadingCompleted(doc);
}

void Atherta3DDock::slotExportTextures()
{
    if (!m_textureSet.isValid()) {
        QMessageBox::information(this, i18n("Export Textures"),
                                 i18n("The active document has no 3D texture set."));
        return;
    }
    KConfigGroup cfg = settingsGroup();
    const QString dir = QFileDialog::getExistingDirectory(
        this, i18n("Export Textures"), cfg.readEntry("lastExportDir", QDir::homePath()));
    if (dir.isEmpty()) {
        return;
    }
    cfg.writeEntry("lastExportDir", dir);

    const QString base = m_modelPath.isEmpty() ? QStringLiteral("texture")
                                               : QFileInfo(m_modelPath).completeBaseName();
    const QRect full(QPoint(0, 0), m_textureSet.size());
    QStringList written;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_textureSet.image()->waitForDone();
    for (int i = 0; i < kChannelCount; ++i) {
        const Channel c = static_cast<Channel>(i);
        QImage img = m_textureSet.readChannel(c, full);
        if (img.isNull()) {
            continue;
        }
        if (c == Channel::Roughness || c == Channel::Metallic) {
            img = img.convertToFormat(QImage::Format_Grayscale8);
        } else if (c == Channel::Normal) {
            img = img.convertToFormat(QImage::Format_RGB888);
        }
        const QString path = QDir(dir).filePath(
            QStringLiteral("%1_%2.png").arg(base, QString::fromLatin1(channelInfo(c).id)));
        if (img.save(path)) {
            written << QFileInfo(path).fileName();
        }
    }
    QApplication::restoreOverrideCursor();
    m_status->setText(i18n("Exported: %1", written.join(QStringLiteral(", "))));
}

// ---------------------------------------------------------------------------
// Painting

bool Atherta3DDock::canPaint(QString *reason) const
{
    auto fail = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };
    if (m_mesh.isEmpty()) {
        return fail(i18n("Load a UV-unwrapped OBJ or FBX model with \"Load Model...\"."));
    }
    if (!m_mesh.hasUVs) {
        return fail(i18n("This model has no UVs, so it cannot be painted."));
    }
    if (!m_canvas || !m_canvas->image()) {
        return fail(i18n("Open a document for the textures (Texture Set > New Texture Document)."));
    }
    if (!m_textureSet.isValid()) {
        return fail(i18n("Create a texture set: Texture Set > Create in Current Document."));
    }
    bool anyChannel = false;
    bool anyTarget = false;
    KisNodeSP active = m_canvas->viewManager() ? m_canvas->viewManager()->activeNode() : nullptr;
    for (int i = 0; i < kChannelCount; ++i) {
        const Channel c = static_cast<Channel>(i);
        if (channelEnabled(c)) {
            anyChannel = true;
            anyTarget = anyTarget || m_textureSet.paintTarget(c, active);
        }
    }
    if (!anyChannel) {
        return fail(i18n("Enable at least one channel to paint."));
    }
    if (!anyTarget) {
        return fail(i18n("The layers of the enabled channels are locked or hidden."));
    }
    return true;
}

void Atherta3DDock::beginStroke(KoPointerEvent *event, const QPointF &devicePos)
{
    if (!m_canvas) {
        return;
    }
    const QSize size = m_viewport->deviceSize();
    m_stroke.reset(new StrokeSession(m_canvas->resourceManager(), size));
    connect(m_stroke.data(), &StrokeSession::updated, this, &Atherta3DDock::slotStrokeUpdated);

    Renderer::DrawParams &p = m_viewport->drawParams();
    p.strokeMode = m_stroke->isEraser() ? Renderer::StrokeMode::Erase : Renderer::StrokeMode::Paint;
    for (int i = 0; i < kChannelCount; ++i) {
        p.strokeChannels[i] = channelEnabled(static_cast<Channel>(i));
    }
    p.strokeRoughness = channelValue(Channel::Roughness);
    p.strokeMetallic = channelValue(Channel::Metallic);
    p.fadeStart = m_fadeStart;

    m_viewport->withContext([&](Renderer &r, GLuint) {
        r.setStrokeLayerSize(size);
        r.clearStrokeLayer();
    });
    m_viewport->setMessage(QString());
    m_stroke->begin(event, devicePos);
}

void Atherta3DDock::continueStroke(KoPointerEvent *event, const QPointF &)
{
    if (m_stroke) {
        m_stroke->move(event);
    }
}

void Atherta3DDock::slotStrokeUpdated()
{
    if (!m_stroke) {
        return;
    }
    const QRect r = m_stroke->takeDirtyRect();
    const QImage img = m_stroke->read(r);
    if (!img.isNull()) {
        m_viewport->withContext([&](Renderer &renderer, GLuint) {
            renderer.uploadStrokeLayer(img, r.intersected(QRect(QPoint(), m_stroke->size())).topLeft());
        });
    }
}

void Atherta3DDock::endStroke()
{
    if (!m_stroke) {
        return;
    }
    m_stroke->end();

    const QSize devSize = m_stroke->size();
    const QRect screenRect = m_stroke->totalDirtyRect() & QRect(QPoint(0, 0), devSize);
    const bool eraser = m_stroke->isEraser();
    const QString compositeOp = m_stroke->compositeOp();
    const QImage strokeImage = m_stroke->read(screenRect);
    const QSize texSize = m_textureSet.size();

    std::vector<uint8_t> baked;
    IRect region;
    if (!screenRect.isEmpty() && m_textureSet.isValid()) {
        m_viewport->withContext([&](Renderer &r, GLuint fbo) {
            if (r.textureSize() != texSize) {
                return;
            }
            r.uploadStrokeLayer(strokeImage, screenRect.topLeft());
            const ViewNavigator &nav = m_viewport->navigator();
            const Mat4 viewProj = nav.viewProjectionMatrix();
            region = textureRegionForScreenRect(
                m_mesh, viewProj, devSize.width(), devSize.height(),
                {screenRect.x(), screenRect.y(), screenRect.width(), screenRect.height()},
                texSize.width(), texSize.height(), m_padding);
            if (region.isEmpty()) {
                return;
            }
            Renderer::BakeParams params;
            params.viewProj = viewProj;
            params.cameraPos = nav.eyePosition();
            params.viewDir = nav.viewDirection();
            params.ortho = nav.state().projection == Projection::Orthographic;
            params.nearClip = nav.nearClip();
            params.farClip = nav.farClip();
            params.fadeStart = m_fadeStart;
            params.projection11 = nav.projectionMatrix().at(1, 1);
            params.viewportDevice = devSize;
            params.region = region;
            if (!r.bakeStroke(params, baked, fbo)) {
                region = IRect();
                return;
            }
            // Grow the paint into the UV gutter so seams do not show.
            const std::vector<uint8_t> &coverage = r.coverage(fbo);
            if (coverage.size() == size_t(texSize.width()) * texSize.height()) {
                std::vector<uint8_t> localCoverage(size_t(region.w) * region.h);
                for (int y = 0; y < region.h; ++y) {
                    std::copy_n(&coverage[size_t(region.y + y) * texSize.width() + region.x],
                                region.w, &localCoverage[size_t(y) * region.w]);
                }
                dilateIntoGutter(baked.data(), region.w, region.h, localCoverage.data(),
                                 {0, 0, region.w, region.h}, m_padding);
            }
        });
    }

    if (!region.isEmpty()) {
        const IRect painted = alphaBounds(baked.data(), region.w, region.h);
        if (!painted.isEmpty()) {
            // Crop to the painted texels.
            std::vector<uint8_t> cropped(size_t(painted.w) * painted.h * 4);
            for (int y = 0; y < painted.h; ++y) {
                std::copy_n(&baked[(size_t(painted.y + y) * region.w + painted.x) * 4],
                            size_t(painted.w) * 4, &cropped[size_t(y) * painted.w * 4]);
            }
            applyBakedStroke(cropped,
                             QRect(region.x + painted.x, region.y + painted.y, painted.w, painted.h),
                             eraser, compositeOp);
        }
    }

    m_stroke.reset();
    m_viewport->drawParams().strokeMode = Renderer::StrokeMode::None;
    m_viewport->withContext([](Renderer &r, GLuint) { r.clearStrokeLayer(); });
}

void Atherta3DDock::applyBakedStroke(const std::vector<uint8_t> &premultiplied, const QRect &rect,
                                     bool eraser, const QString &compositeOp)
{
    KisImageSP image = m_canvas ? KisImageSP(m_canvas->image()) : KisImageSP();
    if (!image) {
        return;
    }
    const QImage premul(premultiplied.data(), rect.width(), rect.height(), rect.width() * 4,
                        QImage::Format_RGBA8888_Premultiplied);
    // Deep copy with straight alpha, as Krita stores it.
    const QImage colorImage = premul.convertToFormat(QImage::Format_ARGB32);

    const KoColorSpace *srgb = KoColorSpaceRegistry::instance()->rgb8();
    KisViewManager *view = m_canvas->viewManager();
    KisNodeSP active = view ? view->activeNode() : nullptr;
    KisSelectionSP selection = view ? view->selection() : nullptr;

    KisProcessingApplicator applicator(image, nullptr, KisProcessingApplicator::NONE,
                                       KisImageSignalVector(), kundo2_i18n("3D Paint"));
    bool any = false;
    for (int i = 0; i < kChannelCount; ++i) {
        const Channel c = static_cast<Channel>(i);
        if (!channelEnabled(c)) {
            continue;
        }
        KisNodeSP target = m_textureSet.paintTarget(c, active);
        if (!target) {
            continue;
        }

        QImage src;
        if (c == Channel::BaseColor || c == Channel::Normal) {
            src = colorImage;
        } else {
            // Data channel: the configured value, masked by the stroke.
            const int v = qBound(0, qRound(channelValue(c) * 255.0f), 255);
            src = QImage(colorImage.size(), QImage::Format_ARGB32);
            for (int y = 0; y < src.height(); ++y) {
                const QRgb *in = reinterpret_cast<const QRgb *>(colorImage.constScanLine(y));
                QRgb *out = reinterpret_cast<QRgb *>(src.scanLine(y));
                for (int x = 0; x < src.width(); ++x) {
                    out[x] = qRgba(v, v, v, qAlpha(in[x]));
                }
            }
        }
        KisPaintDeviceSP srcDev = new KisPaintDevice(srgb);
        srcDev->convertFromQImage(src, nullptr, rect.x(), rect.y());

        const QString op = eraser ? COMPOSITE_ERASE
                                  : (c == Channel::BaseColor ? compositeOp : COMPOSITE_OVER);

        applicator.applyCommand(new KisCommandUtils::LambdaCommand(
            kundo2_i18n("3D Paint"), [target, srcDev, rect, op, selection]() -> KUndo2Command * {
                KisPaintDeviceSP dev = target->paintDevice();
                KisTransaction transaction(dev);
                KisPainter gc(dev);
                gc.setCompositeOpId(op);
                if (selection) {
                    gc.setSelection(selection);
                }
                gc.bitBlt(rect.topLeft(), srcDev, rect);
                dev->setDirty(rect);
                return transaction.endAndTake();
            }));
        any = true;
    }
    applicator.end();

    if (any) {
        // Show the result before the live stroke overlay disappears.
        image->waitForDone();
        syncRect(rect);
    }
}

void Atherta3DDock::tabletPointerChanged(const QTabletEvent *event)
{
    // Lets Krita switch to the eraser preset when the pen is flipped.
    KoToolManager::instance()->switchInputDeviceRequested(
        KoInputDevice(event->deviceType(), event->pointerType(), event->uniqueId()));
}

qreal Atherta3DDock::brushDiameter() const
{
    if (!m_canvas) {
        return 0.0;
    }
    const KisPaintOpPresetSP preset = m_canvas->resourceManager()
                                          ->resource(KoCanvasResource::CurrentPaintOpPreset)
                                          .value<KisPaintOpPresetSP>();
    if (!preset || !preset->settings()) {
        return 0.0;
    }
    return preset->settings()->paintOpSize();
}
