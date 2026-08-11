#include "app/app.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("embistel"));
    QCoreApplication::setApplicationName(QStringLiteral("LMDBArchiver"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LMDBARCHIVER_VERSION));
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    application.setStyleSheet(QStringLiteral(
        "QMainWindow, QDialog { background: #f6f7fb; }"
        "QMenuBar { background: #ffffff; border-bottom: 1px solid #dfe3eb; padding: 3px; }"
        "QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #dfe3eb; padding: 7px; spacing: 5px; }"
        "QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 6px 9px; }"
        "QToolButton:hover { background: #eaf2ff; border-color: #c7dafb; }"
        "QLineEdit { background: #ffffff; border: 1px solid #ccd3df; border-radius: 7px; padding: 8px 11px; }"
        "QLineEdit:focus { border: 2px solid #3478d4; padding: 7px 10px; }"
        "QTreeView { background: #ffffff; border: 1px solid #d9dee8; border-radius: 8px; alternate-background-color: #f8faff; padding: 3px; }"
        "QTreeView::item { min-height: 28px; }"
        "QTreeView::item:selected { background: #cfe3ff; color: #10243e; border-radius: 4px; }"
        "QFrame#ArchiveBanner { background: #ffffff; border: 1px solid #d9dee8; border-radius: 9px; }"
        "QLabel#ArchiveName { color: #14213d; font-size: 15px; font-weight: 700; }"
        "QLabel#ArchiveLocation { color: #667085; font-size: 9pt; }"
        "QHeaderView::section { background: #eef1f6; border: 0; border-right: 1px solid #d7dce5; border-bottom: 1px solid #d7dce5; padding: 7px; font-weight: 600; }"
        "QStatusBar { background: #ffffff; border-top: 1px solid #dfe3eb; }"));
    return App::run(application);
}
