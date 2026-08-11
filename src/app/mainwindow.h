#pragma once

#include "archive/archive.h"

#include <QMainWindow>

class ArchiveModel;
class QAction;
class DropView;
class QLabel;
class QLineEdit;
class QModelIndex;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    bool openArchive(const QString &path);

private slots:
    void newArchive();
    void chooseArchive();
    void addFiles();
    void addFolder();
    void addDropped(const QStringList &paths, const QString &destination);
    void pasteFiles();
    void copySelected();
    void dragSelectedOut(const QStringList &paths);
    void openSelected(const QModelIndex &index);
    void showEntryMenu(const QPoint &position);
    void removeSelected();
    void extractSelected();
    void extractAll();
    void verifyArchive();
    void compactArchive();
    void refresh();
    void configureShell();
    void showAbout();

private:
    QStringList selectedPaths() const;
    bool addLocalPaths(const QStringList &paths, const QString &destination = {});
    bool extractPaths(const QStringList &paths, const QString &destination);
    QStringList exportPaths(const QStringList &paths);
    static void cleanStaleExports();
    void updateActions();
    void showError(const QString &message);

    Archive m_archive;
    ArchiveModel *m_model = nullptr;
    DropView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_summary = nullptr;
    QLabel *m_archiveName = nullptr;
    QLabel *m_archiveLocation = nullptr;
    QList<ArchiveEntry> m_entries;
    QAction *m_addFilesAction = nullptr;
    QAction *m_addFolderAction = nullptr;
    QAction *m_extractAction = nullptr;
    QAction *m_extractAllAction = nullptr;
    QAction *m_verifyAction = nullptr;
    QAction *m_compactAction = nullptr;
    QAction *m_removeAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_openEntryAction = nullptr;
};
