#include "app/dropview.h"
#include "app/archivemodel.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QUrl>

DropView::DropView(QWidget *parent) : QTreeView(parent)
{
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setDropIndicatorShown(true);
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

