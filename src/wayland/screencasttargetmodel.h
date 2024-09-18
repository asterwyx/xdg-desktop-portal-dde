// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <QAbstractListModel>

// Automatically add screens
class ScreenCastTargetModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct TargetItem
    {
        QString name;
        QObject *item;
    };

    void addItem(const TargetItem &item);
    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    explicit ScreenCastTargetModel(QObject *parent = nullptr);
    [[nodiscard]] QModelIndex parent(const QModelIndex &child) const override;
    [[nodiscard]] int rowCount(const QModelIndex &parent) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

private:
    QList<TargetItem> m_data;
};
