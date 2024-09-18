// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <QDialog>
#include <QListView>
class ScreenCastTargetModel;

class ScreenCastChooserDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScreenCastChooserDialog(QWidget *parent = nullptr);
    [[nodiscard]] ScreenCastTargetModel *model() const;
    [[nodiscard]] QListView *listView() const;

private:
    ScreenCastTargetModel *m_model;
    QListView *m_listView;
};
