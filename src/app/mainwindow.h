#pragma once

#include "archive/archive.h"

#include <QMainWindow>

class ArchiveModel;
class QAction;
class DropView;
class QLabel;
class QLineEdit;

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
    void removeSelected();
    void extractSelected();
    void extractAll();
    void refresh();
    void configureShell();
    void showAbout();

private:
    QStringList selectedPaths() const;
    bool addLocalPaths(const QStringList &paths, const QString &destination = {});
    void updateActions();
    void showError(const QString &message);

    Archive m_archive;
    ArchiveModel *m_model = nullptr;
    DropView *m_view = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_summary = nullptr;
    QList<ArchiveEntry> m_entries;
    QAction *m_addFilesAction = nullptr;
    QAction *m_addFolderAction = nullptr;
    QAction *m_extractAction = nullptr;
    QAction *m_extractAllAction = nullptr;
    QAction *m_removeAction = nullptr;
};
