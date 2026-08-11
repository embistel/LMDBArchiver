#pragma once

#include "archive/archiveentry.h"

#include <QStandardItemModel>

class ArchiveModel final : public QStandardItemModel {
    Q_OBJECT
public:
    enum Roles { PathRole = Qt::UserRole + 1, DirectoryRole };
    explicit ArchiveModel(QObject *parent = nullptr);
    void setEntries(const QList<ArchiveEntry> &entries, const QString &filter = {});
    QString pathForIndex(const QModelIndex &index) const;
};

