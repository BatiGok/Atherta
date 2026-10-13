/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Atherta3DPlugin.h"

#include "Atherta3DDock.h"

#include <KoDockFactoryBase.h>
#include <KoDockRegistry.h>
#include <kpluginfactory.h>

K_PLUGIN_FACTORY_WITH_JSON(Atherta3DPluginFactory, "krita_atherta3d.json",
                           registerPlugin<Atherta3DPlugin>();)

namespace {

class Atherta3DDockFactory : public KoDockFactoryBase
{
public:
    QString id() const override { return QStringLiteral("Atherta3DPaintDocker"); }

    virtual Qt::DockWidgetArea defaultDockWidgetArea() const { return Qt::RightDockWidgetArea; }

    QDockWidget *createDockWidget() override
    {
        auto *dock = new Atherta3DDock();
        dock->setObjectName(id());
        return dock;
    }

    DockPosition defaultDockPosition() const override { return DockRight; }
};

} // namespace

Atherta3DPlugin::Atherta3DPlugin(QObject *parent, const QVariantList &)
    : QObject(parent)
{
    KoDockRegistry::instance()->add(new Atherta3DDockFactory());
}

Atherta3DPlugin::~Atherta3DPlugin() = default;

#include "Atherta3DPlugin.moc"
