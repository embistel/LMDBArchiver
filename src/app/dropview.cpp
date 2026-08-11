#include "app/dropview.h"
#include "app/archivemodel.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

DropView::DropView(QWidget *parent) : QTreeView(parent)
{
    setAcceptDrops(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(true);
}

void DropView::startDrag(Qt::DropActions)
{
    QStringList paths;
    for (const QModelIndex &index : selectionModel()->selectedRows(0))
        paths << index.data(ArchiveModel::PathRole).toString();
    paths.removeAll(QString{});
    paths.removeDuplicates();
    if (!paths.isEmpty()) emit archiveDragRequested(paths);
}

void DropView::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void DropView::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void DropView::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
    if (paths.isEmpty()) return;
    QString destination;
    const QModelIndex target = indexAt(event->position().toPoint()).siblingAtColumn(0);
    if (target.isValid()) {
        destination = target.data(ArchiveModel::PathRole).toString();
        if (!target.data(ArchiveModel::DirectoryRole).toBool()) destination = destination.section(u'/', 0, -2);
    }
    emit localFilesDropped(paths, destination);
    event->acceptProposedAction();
}
