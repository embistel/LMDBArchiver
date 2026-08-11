#pragma once

#include <QTreeView>

class DropView final : public QTreeView {
    Q_OBJECT
public:
    explicit DropView(QWidget *parent = nullptr);
signals:
    void localFilesDropped(const QStringList &paths, const QString &destination);
    void archiveDragRequested(const QStringList &archivePaths);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
};
