// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "screencasttargetmodel.h"

#include <QApplication>
#include <QScreen>

ScreenCastTargetModel::ScreenCastTargetModel(QObject *parent)
    : QAbstractListModel(parent)
{
    beginResetModel();
    // Automatically add all screens
    for (auto &screen : QApplication::screens()) {
        m_data << TargetItem{ screen->name(), screen };
    }
    endResetModel();
}

QModelIndex ScreenCastTargetModel::parent(const QModelIndex &child) const
{
    return {};
}

int ScreenCastTargetModel::rowCount(const QModelIndex &parent) const
{
    return static_cast<int>(m_data.count());
}

int ScreenCastTargetModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

QVariant ScreenCastTargetModel::data(const QModelIndex &index, int role) const
{
    switch (role) {
    case Qt::DisplayRole:
        return QVariant::fromValue(m_data[index.row()].name);
    default:
        return {};
    }
}

void ScreenCastTargetModel::addItem(const ScreenCastTargetModel::TargetItem &item)
{
    beginInsertRows(QModelIndex(), rowCount({}), rowCount({}) + 1);
    m_data.append(item);
    endInsertRows();
}

QModelIndex ScreenCastTargetModel::index(int row, int column, const QModelIndex &parent) const
{
    return QAbstractItemModel::createIndex(row, column, m_data[row].item);
}
