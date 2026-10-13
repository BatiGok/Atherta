/*
 * SPDX-FileCopyrightText: 2026 Atherta
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QObject>
#include <QVariant>

/** Registers the "3D Paint" docker with Krita. */
class Atherta3DPlugin : public QObject
{
    Q_OBJECT
public:
    Atherta3DPlugin(QObject *parent, const QVariantList &);
    ~Atherta3DPlugin() override;
};
