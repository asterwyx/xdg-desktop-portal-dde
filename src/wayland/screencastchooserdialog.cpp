// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "screencastchooserdialog.h"

#include "screencasttargetmodel.h"

ScreenCastChooserDialog::ScreenCastChooserDialog(QWidget *parent)
    : QDialog(parent)
    , m_model(new ScreenCastTargetModel(this))
    , m_listView(new QListView(this))
{
    m_listView->setModel(m_model);
}

ScreenCastTargetModel *ScreenCastChooserDialog::model() const
{
    return m_model;
}

QListView *ScreenCastChooserDialog::listView() const
{
    return m_listView;
}
