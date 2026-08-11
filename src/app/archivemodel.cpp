#include "app/archivemodel.h"

#include <QFileIconProvider>
#include <QLocale>

ArchiveModel::ArchiveModel(QObject *parent) : QStandardItemModel(parent)
{
    setHorizontalHeaderLabels({tr("이름"), tr("크기"), tr("저장 크기"), tr("수정한 날짜"), tr("형식")});
}

void ArchiveModel::setEntries(const QList<ArchiveEntry> &entries, const QString &filter)
{
    clear();
    setHorizontalHeaderLabels({tr("이름"), tr("크기"), tr("저장 크기"), tr("수정한 날짜"), tr("형식")});
    QFileIconProvider icons;
    QHash<QString, QStandardItem *> folders;
    folders.insert(QString{}, invisibleRootItem());
    for (const ArchiveEntry &entry : entries) {
        if (!filter.isEmpty() && !entry.path.contains(filter, Qt::CaseInsensitive)) continue;
        const QStringList parts = entry.path.split(u'/', Qt::SkipEmptyParts);
        QString parentPath;
        QStandardItem *parent = invisibleRootItem();
        for (qsizetype i = 0; i < parts.size(); ++i) {
            const QString currentPath = parentPath.isEmpty() ? parts[i] : parentPath + u'/' + parts[i];
            const bool leaf = i == parts.size() - 1;
            if (!leaf && folders.contains(currentPath)) {
                parent = folders.value(currentPath);
                parentPath = currentPath;
                continue;
            }
            const bool directory = !leaf || entry.directory;
            auto *name = new QStandardItem(directory ? icons.icon(QFileIconProvider::Folder)
                                                     : icons.icon(QFileIconProvider::File), parts[i]);
            name->setData(currentPath, PathRole);
            name->setData(directory, DirectoryRole);
            name->setEditable(false);
            QList<QStandardItem *> row{name};
            if (leaf) {
                const QLocale locale;
                row << new QStandardItem(directory ? QString{} : locale.formattedDataSize(entry.originalSize));
                row << new QStandardItem(directory ? QString{} : locale.formattedDataSize(entry.storedSize));
                row << new QStandardItem(entry.modified.toString(QStringLiteral("yyyy-MM-dd HH:mm")));
                row << new QStandardItem(directory ? tr("폴더") : tr("파일"));
            } else {
                for (int column = 1; column < 5; ++column) row << new QStandardItem;
            }
            for (QStandardItem *item : row) item->setEditable(false);
            parent->appendRow(row);
            if (directory) folders.insert(currentPath, name);
            parent = name;
            parentPath = currentPath;
        }
    }
}

QString ArchiveModel::pathForIndex(const QModelIndex &index) const
{
    return index.siblingAtColumn(0).data(PathRole).toString();
}

