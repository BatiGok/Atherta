/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "TextureSet.h"

#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KisViewManager.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_node.h>
#include <kis_node_commands_adapter.h>
#include <kis_paint_device.h>
#include <kis_paint_layer.h>
#include <klocalizedstring.h>
#include <kundo2magicstring.h>

using atherta::Channel;
using atherta::channelInfo;
using atherta::kChannelCount;

namespace {

KisNodeSP findChildByName(KisNodeSP parent, const QString &name, bool groupsOnly)
{
    if (!parent) {
        return nullptr;
    }
    // Topmost match wins.
    for (KisNodeSP child = parent->lastChild(); child; child = child->prevSibling()) {
        if (child->name() == name && (!groupsOnly || child->inherits("KisGroupLayer"))) {
            return child;
        }
    }
    return nullptr;
}

bool isEditablePaintLayer(KisNodeSP node)
{
    return node && node->inherits("KisPaintLayer") && node->isEditable(true) && node->paintDevice();
}

} // namespace

QString TextureSet::rootGroupName()
{
    return QStringLiteral("Atherta 3D");
}

QString TextureSet::channelGroupName(Channel c)
{
    return QString::fromLatin1(channelInfo(c).layerName);
}

TextureSet::TextureSet(KisImageSP image)
{
    setImage(image);
}

void TextureSet::setImage(KisImageSP image)
{
    m_image = image;
    refresh();
}

void TextureSet::refresh()
{
    m_root = nullptr;
    for (KisNodeSP &n : m_channels) {
        n = nullptr;
    }
    if (!m_image) {
        return;
    }
    m_root = findChildByName(m_image->root(), rootGroupName(), true);
    for (int i = 0; i < kChannelCount; ++i) {
        m_channels[i] = findChildByName(m_root, channelGroupName(static_cast<Channel>(i)), true);
    }
}

bool TextureSet::isValid() const
{
    if (!m_image || !m_root) {
        return false;
    }
    for (const KisNodeSP &n : m_channels) {
        if (!n) {
            return false;
        }
    }
    return true;
}

QSize TextureSet::size() const
{
    return m_image ? m_image->bounds().size() : QSize();
}

KisNodeSP TextureSet::channelGroup(Channel c) const
{
    return m_channels[static_cast<int>(c)];
}

KisNodeSP TextureSet::paintTarget(Channel c, KisNodeSP activeNode) const
{
    KisNodeSP group = channelGroup(c);
    if (!group) {
        return nullptr;
    }
    // The active layer, if it lives (possibly nested) inside this channel.
    for (KisNodeSP n = activeNode; n; n = n->parent()) {
        if (n == group) {
            if (isEditablePaintLayer(activeNode)) {
                return activeNode;
            }
            break;
        }
    }
    for (KisNodeSP child = group->lastChild(); child; child = child->prevSibling()) {
        if (isEditablePaintLayer(child)) {
            return child;
        }
    }
    return nullptr;
}

QImage TextureSet::readChannel(Channel c, const QRect &rect) const
{
    KisNodeSP group = channelGroup(c);
    if (!group || !group->projection() || rect.isEmpty()) {
        return QImage();
    }
    const KoColorProfile *srgb = KoColorSpaceRegistry::instance()->rgb8()->profile();
    const QImage img = group->projection()->convertToQImage(srgb, rect);
    return img.convertToFormat(QImage::Format_RGBA8888);
}

bool TextureSet::create(KisViewManager *view)
{
    if (!view || !view->image()) {
        return false;
    }
    KisImageSP image = view->image();
    const KoColorSpace *cs = image->colorSpace();

    KisNodeCommandsAdapter adapter(view);
    adapter.beginMacro(kundo2_i18n("Create 3D Texture Set"));

    KisNodeSP rootParent = image->root();
    KisGroupLayerSP root = new KisGroupLayer(image, rootGroupName(), OPACITY_OPAQUE_U8);
    adapter.addNode(root, rootParent, rootParent->lastChild());

    // Added bottom-up so that Base Color ends up on top in the layer docker
    // (and is what the 2D canvas shows).
    for (int i = kChannelCount - 1; i >= 0; --i) {
        const Channel c = static_cast<Channel>(i);
        const auto &info = channelInfo(c);
        const QString name = channelGroupName(c);

        KisGroupLayerSP group = new KisGroupLayer(image, name, OPACITY_OPAQUE_U8);
        adapter.addNode(group, root, root->lastChild());

        KisPaintLayerSP base = new KisPaintLayer(image, name + QStringLiteral(" Base"), OPACITY_OPAQUE_U8, cs);
        // A default pixel costs no memory but fills the whole layer.
        const QColor def(info.defaultRgba[0], info.defaultRgba[1], info.defaultRgba[2], info.defaultRgba[3]);
        base->paintDevice()->setDefaultPixel(KoColor(def, cs));
        adapter.addNode(base, group, KisNodeSP());

        KisPaintLayerSP paint = new KisPaintLayer(image, name + QStringLiteral(" Paint"), OPACITY_OPAQUE_U8, cs);
        adapter.addNode(paint, group, base);
    }

    adapter.endMacro();
    return true;
}
