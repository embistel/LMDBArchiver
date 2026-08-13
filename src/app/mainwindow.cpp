#include "app/mainwindow.h"

#include "app/archivemodel.h"
#include "app/dropview.h"
#include "platform/shellintegration.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QFileDialog>
#include <QFileInfo>
#include <QDrag>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QAction *makeAction(QObject *parent, const QIcon &icon, const QString &text,
                    const QKeySequence &shortcut = {})
{
    auto *action = new QAction(icon, text, parent);
    if (!shortcut.isEmpty()) action->setShortcut(shortcut);
    return action;
}

// Friendly two-line label shown in QProgressDialog. First line is the phase + current item,
// second line is a byte-range + percentage (or just bytes when total is unknown).
QString formatProgressLabel(ProgressPhase phase, const QString &item, qint64 bytesDone, qint64 bytesTotal)
{
    const QLocale locale;
    QString phaseText;
    switch (phase) {
    case ProgressPhase::Collecting:  phaseText = MainWindow::tr("Collecting files…"); break;
    case ProgressPhase::Processing:  phaseText = MainWindow::tr("Processing…"); break;
    case ProgressPhase::Finalizing:  phaseText = MainWindow::tr("Saving changes…"); break;
    }
    if (bytesTotal > 0) {
        const int percent = int((bytesDone * 100) / bytesTotal);
        return phaseText + u'\n' + item
            + MainWindow::tr("\n%1 / %2 (%3%)").arg(locale.formattedDataSize(bytesDone),
                                                    locale.formattedDataSize(bytesTotal))
                                               .arg(qBound(0, percent, 100));
    }
    if (bytesDone > 0)
        return phaseText + u'\n' + item + u'\n' + locale.formattedDataSize(bytesDone);
    return phaseText + u'\n' + item;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    cleanStaleExports();
    setWindowTitle(tr("LMDB Archiver"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    resize(1120, 720);
    setMinimumSize(760, 480);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(8);

    auto *banner = new QFrame(container);
    banner->setObjectName(QStringLiteral("ArchiveBanner"));
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    auto *bannerIcon = new QLabel(banner);
    bannerIcon->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(38, 38));
    bannerLayout->addWidget(bannerIcon);
    auto *bannerText = new QVBoxLayout;
    bannerText->setSpacing(1);
    m_archiveName = new QLabel(tr("LMDB Archive Manager"), banner);
    m_archiveName->setObjectName(QStringLiteral("ArchiveName"));
    m_archiveLocation = new QLabel(tr("Create a new archive or open an existing .lmdb file."), banner);
    m_archiveLocation->setObjectName(QStringLiteral("ArchiveLocation"));
    bannerText->addWidget(m_archiveName);
    bannerText->addWidget(m_archiveLocation);
    bannerLayout->addLayout(bannerText, 1);
    layout->addWidget(banner);

    m_search = new QLineEdit(container);
    m_search->setPlaceholderText(tr("Search archive..."));
    m_search->setClearButtonEnabled(true);
    m_search->setEnabled(false);
    layout->addWidget(m_search);

    m_view = new DropView(container);
    m_model = new ArchiveModel(m_view);
    m_view->setModel(m_model);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(0, Qt::AscendingOrder);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) m_view->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    layout->addWidget(m_view, 1);
    setCentralWidget(container);

    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *newAction = makeAction(this, QIcon(QStringLiteral(":/icons/new.svg")), tr("&New Archive..."), QKeySequence::New);
    auto *openAction = makeAction(this, QIcon(QStringLiteral(":/icons/open.svg")), tr("&Open..."), QKeySequence::Open);
    newAction->setIconText(tr("New"));
    openAction->setIconText(tr("Open"));
    auto *exitAction = makeAction(this, {}, tr("E&xit"), QKeySequence::Quit);
    fileMenu->addActions({newAction, openAction});
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    m_addFilesAction = makeAction(this, QIcon(QStringLiteral(":/icons/add-file.svg")), tr("&Add File..."), QKeySequence(Qt::CTRL | Qt::Key_I));
    m_addFolderAction = makeAction(this, QIcon(QStringLiteral(":/icons/add-folder.svg")), tr("Ad&d Folder..."));
    m_addFilesAction->setIconText(tr("Add File"));
    m_addFolderAction->setIconText(tr("Add Folder"));
    auto *pasteAction = makeAction(this, {}, tr("&Paste Files"), QKeySequence::Paste);
    m_copyAction = makeAction(this, QIcon(QStringLiteral(":/icons/copy.svg")), tr("&Copy Selected to Explorer"), QKeySequence::Copy);
    m_removeAction = makeAction(this, QIcon(QStringLiteral(":/icons/delete.svg")), tr("&Delete Selected"), QKeySequence::Delete);
    m_openEntryAction = makeAction(this, QIcon(QStringLiteral(":/icons/open.svg")), tr("Open Selected"), QKeySequence(Qt::Key_Return));
    editMenu->addActions({m_addFilesAction, m_addFolderAction, pasteAction});
    editMenu->addSeparator();
    editMenu->addActions({m_copyAction, m_removeAction});

    auto *archiveMenu = menuBar()->addMenu(tr("&Archive"));
    m_extractAction = makeAction(this, QIcon(QStringLiteral(":/icons/extract.svg")), tr("&Extract Selected..."), QKeySequence(Qt::CTRL | Qt::Key_E));
    m_extractAction->setIconText(tr("Extract"));
    m_removeAction->setIconText(tr("Delete"));
    m_extractAllAction = makeAction(this, {}, tr("E&xtract All..."));
    m_verifyAction = makeAction(this, QIcon(QStringLiteral(":/icons/verify.svg")), tr("Archive &Test"));
    m_compactAction = makeAction(this, QIcon(QStringLiteral(":/icons/compact.svg")), tr("&Compact Archive"));
    auto *refreshAction = makeAction(this, style()->standardIcon(QStyle::SP_BrowserReload), tr("Refresh"), QKeySequence::Refresh);
    m_compressAction = makeAction(this, {}, tr("G&zip on Add"));
    m_compressAction->setCheckable(true);
    m_compressAction->setToolTip(tr("When enabled, files are stored compressed with standard gzip on add. The key self-describes this with a \".<hash>.gz\" marker."));
    archiveMenu->addActions({m_extractAction, m_extractAllAction, m_verifyAction, m_compactAction, refreshAction});
    archiveMenu->addSeparator();
    archiveMenu->addAction(m_compressAction);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    auto *shellAction = makeAction(this, {}, tr("Windows Explorer Integration..."));
    toolsMenu->addAction(shellAction);
    toolsMenu->addSeparator();
    // Language selection (English default, Korean optional). Applied on restart.
    auto *languageMenu = toolsMenu->addMenu(tr("Language"));
    auto *langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);
    m_englishAction = langGroup->addAction(tr("English"));
    m_koreanAction = langGroup->addAction(QStringLiteral("\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4"));  // 한국어
    for (auto *act : {m_englishAction, m_koreanAction}) {
        act->setCheckable(true);
        languageMenu->addAction(act);
    }
    const QString currentLang = QSettings().value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();
    (currentLang.compare(QStringLiteral("ko"), Qt::CaseInsensitive) == 0 ? m_koreanAction : m_englishAction)->setChecked(true);
    connect(m_englishAction, &QAction::triggered, this, [this] { switchLanguage(QStringLiteral("en")); });
    connect(m_koreanAction, &QAction::triggered, this, [this] { switchLanguage(QStringLiteral("ko")); });
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAction = makeAction(this, {}, tr("About LMDB Archiver"));
    helpMenu->addAction(aboutAction);

    auto *toolbar = addToolBar(tr("Main Toolbar"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addActions({newAction, openAction});
    toolbar->addSeparator();
    toolbar->addActions({m_addFilesAction, m_addFolderAction, m_extractAction, m_removeAction});

    m_summary = new QLabel(tr("Open or create an archive. You can drag files and folders into the window."), this);
    statusBar()->addWidget(m_summary, 1);

    connect(newAction, &QAction::triggered, this, &MainWindow::newArchive);
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseArchive);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(m_addFilesAction, &QAction::triggered, this, &MainWindow::addFiles);
    connect(m_addFolderAction, &QAction::triggered, this, &MainWindow::addFolder);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::pasteFiles);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelected);
    connect(m_openEntryAction, &QAction::triggered, this, [this] { openSelected(m_view->currentIndex()); });
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
    connect(m_extractAction, &QAction::triggered, this, &MainWindow::extractSelected);
    connect(m_extractAllAction, &QAction::triggered, this, &MainWindow::extractAll);
    connect(m_verifyAction, &QAction::triggered, this, &MainWindow::verifyArchive);
    connect(m_compactAction, &QAction::triggered, this, &MainWindow::compactArchive);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refresh);
    connect(shellAction, &QAction::triggered, this, &MainWindow::configureShell);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    connect(m_view, &DropView::localFilesDropped, this, &MainWindow::addDropped);
    connect(m_view, &DropView::archiveDragRequested, this, &MainWindow::dragSelectedOut);
    connect(m_view, &QTreeView::doubleClicked, this, &MainWindow::openSelected);
    connect(m_view, &QWidget::customContextMenuRequested, this, &MainWindow::showEntryMenu);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { m_model->setEntries(m_entries, text); m_view->expandToDepth(0); });
    connect(m_compressAction, &QAction::toggled, this, [this](bool on) {
        QSettings().setValue(QStringLiteral("archive/compressOnAdd"), on);
    });
    updateActions();

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    m_compressAction->setChecked(settings.value(QStringLiteral("archive/compressOnAdd"), false).toBool());
}

bool MainWindow::openArchive(const QString &path)
{
    QString error;
    if (!m_archive.open(path, false, &error)) { showError(error); return false; }
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — LMDB Archiver"));
    m_archiveName->setText(QFileInfo(path).fileName());
    m_archiveLocation->setText(QFileInfo(path).absoluteFilePath());
    refresh();
    return true;
}

void MainWindow::newArchive()
{
    QString path = QFileDialog::getSaveFileName(this, tr("New LMDB Archive"), {}, tr("LMDB archive (*.lmdb)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".lmdb"), Qt::CaseInsensitive)) path += QStringLiteral(".lmdb");
    const bool replaceExisting = QFileInfo::exists(path);
    QString error;
    if (!m_archive.open(path, true, &error)) { showError(error); return; }
    if (replaceExisting && !m_archive.clear(&error)) { showError(error); return; }
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — LMDB Archiver"));
    m_archiveName->setText(QFileInfo(path).fileName());
    m_archiveLocation->setText(QFileInfo(path).absoluteFilePath());
    refresh();
}

void MainWindow::chooseArchive()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open LMDB Archive"), {}, tr("LMDB archive (*.lmdb);;All files (*.*)"));
    if (!path.isEmpty()) openArchive(path);
}

void MainWindow::addFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Files to add"));
    addLocalPaths(paths);
}

void MainWindow::addFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("Folder to add"));
    if (!path.isEmpty()) addLocalPaths({path});
}

void MainWindow::addDropped(const QStringList &paths, const QString &destination) { addLocalPaths(paths, destination); }

void MainWindow::pasteFiles()
{
    QStringList paths;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    for (const QUrl &url : mime->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
    if (paths.isEmpty()) { statusBar()->showMessage(tr("No files on the clipboard to add."), 3000); return; }
    addLocalPaths(paths);
}

QStringList MainWindow::exportPaths(const QStringList &requestedPaths)
{
    QStringList paths = requestedPaths;
    std::sort(paths.begin(), paths.end(), [](const QString &left, const QString &right) {
        return left.size() < right.size();
    });
    QStringList topLevel;
    for (const QString &path : paths) {
        bool covered = false;
        for (const QString &parent : topLevel) {
            if (path == parent || path.startsWith(parent + u'/')) { covered = true; break; }
        }
        if (!covered) topLevel << path;
    }
    const QString root = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/LMDBArchiver/exports/")
        + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(root)) { showError(tr("Could not create a temporary export folder.")); return {}; }
    if (!extractPaths(topLevel, root)) {
        QDir(root).removeRecursively();
        return {};
    }
    QStringList localPaths;
    for (const QString &path : topLevel) {
        const QString local = QDir(root).absoluteFilePath(path);
        if (QFileInfo::exists(local)) localPaths << local;
    }
    return localPaths;
}

void MainWindow::copySelected()
{
    const QStringList localPaths = exportPaths(selectedPaths());
    if (localPaths.isEmpty()) return;
    QList<QUrl> urls;
    for (const QString &path : localPaths) urls << QUrl::fromLocalFile(path);
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    QApplication::clipboard()->setMimeData(mime);
    statusBar()->showMessage(tr("Copied %1 item(s). Paste in Explorer.").arg(urls.size()), 5000);
}

void MainWindow::dragSelectedOut(const QStringList &paths)
{
    const QStringList localPaths = exportPaths(paths);
    if (localPaths.isEmpty()) return;
    QList<QUrl> urls;
    for (const QString &path : localPaths) urls << QUrl::fromLocalFile(path);
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    auto *drag = new QDrag(m_view);
    drag->setMimeData(mime);
    drag->setPixmap(style()->standardIcon(QStyle::SP_FileIcon).pixmap(32, 32));
    drag->exec(Qt::CopyAction, Qt::CopyAction);
}

void MainWindow::openSelected(const QModelIndex &index)
{
    if (!index.isValid()) return;
    const QModelIndex nameIndex = index.siblingAtColumn(0);
    if (nameIndex.data(ArchiveModel::DirectoryRole).toBool()) {
        m_view->setExpanded(nameIndex, !m_view->isExpanded(nameIndex));
        return;
    }
    const QString path = nameIndex.data(ArchiveModel::PathRole).toString();
    const QStringList local = exportPaths({path});
    if (!local.isEmpty() && !QDesktopServices::openUrl(QUrl::fromLocalFile(local.first())))
        showError(tr("Could not open the file with the associated program."));
}

void MainWindow::showEntryMenu(const QPoint &position)
{
    const QModelIndex index = m_view->indexAt(position);
    if (!index.isValid()) return;
    if (!m_view->selectionModel()->isSelected(index)) m_view->setCurrentIndex(index);
    QMenu menu(this);
    menu.addAction(m_openEntryAction);
    menu.addAction(m_extractAction);
    menu.addAction(m_copyAction);
    menu.addSeparator();
    menu.addAction(m_removeAction);
    menu.exec(m_view->viewport()->mapToGlobal(position));
}

void MainWindow::cleanStaleExports()
{
    const QString exportsRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/LMDBArchiver/exports");
    QDir root(exportsRoot);
    if (!root.exists()) return;
    const QDateTime threshold = QDateTime::currentDateTime().addDays(-7);
    for (const QFileInfo &entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.lastModified() < threshold) QDir(entry.absoluteFilePath()).removeRecursively();
    }
}

bool MainWindow::addLocalPaths(const QStringList &paths, const QString &destination)
{
    if (!m_archive.isOpen() || paths.isEmpty()) return false;
    QProgressDialog dialog(tr("Adding entries to the archive..."), tr("Cancel"), 0, 0, this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(250);
    QString error;
    const bool compress = m_compressAction->isChecked();
    const bool ok = m_archive.addPaths(paths, destination, &error,
        [&dialog](const ProgressInfo &info) {
            applyProgress(dialog, info);
            return !dialog.wasCanceled();
        }, compress);
    if (!ok) { showError(error.isEmpty() ? tr("The add operation was cancelled.") : error); return false; }
    refresh();
    return true;
}

QStringList MainWindow::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &index : m_view->selectionModel()->selectedRows(0)) paths << m_model->pathForIndex(index);
    paths.removeDuplicates();
    return paths;
}

void MainWindow::removeSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;
    if (QMessageBox::question(this, tr("Delete Items"), tr("Delete the %1 selected item(s) from the archive?\nThis cannot be undone.").arg(paths.size())) != QMessageBox::Yes) return;
    QString error;
    if (!m_archive.removePaths(paths, &error)) { showError(error); return; }
    refresh();
}

void MainWindow::extractSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;
    const QString destination = QFileDialog::getExistingDirectory(this, tr("Folder to extract the selection to"));
    if (destination.isEmpty()) return;
    if (extractPaths(paths, destination)) statusBar()->showMessage(tr("Extracted the selection."), 4000);
}

void MainWindow::extractAll()
{
    if (!m_archive.isOpen()) return;
    const QString destination = QFileDialog::getExistingDirectory(this, tr("Folder to extract everything to"));
    if (destination.isEmpty()) return;
    if (extractPaths({}, destination)) statusBar()->showMessage(tr("Extracted the whole archive."), 4000);
}

bool MainWindow::extractPaths(const QStringList &paths, const QString &destination)
{
    QProgressDialog dialog(tr("Extracting entries from the archive..."), tr("Cancel"), 0, 0, this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(250);
    QString error;
    const bool ok = m_archive.extract(paths, destination, &error,
        [&dialog](const ProgressInfo &info) {
            applyProgress(dialog, info);
            return !dialog.wasCanceled();
        });
    if (!ok) showError(error.isEmpty() ? tr("The extract operation was cancelled.") : error);
    return ok;
}

void MainWindow::verifyArchive()
{
    QProgressDialog dialog(tr("Verifying archive records..."), tr("Cancel"), 0, 0, this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(250);
    QString error;
    const bool ok = m_archive.verify(&error,
        [&dialog](const ProgressInfo &info) {
            applyProgress(dialog, info);
            return !dialog.wasCanceled();
        });
    if (!ok) showError(error.isEmpty() ? tr("The verification was cancelled.") : error);
    else QMessageBox::information(this, tr("Verify Archive"), tr("All entries were read successfully."));
}

void MainWindow::compactArchive()
{
    if (QMessageBox::question(this, tr("Compact Archive"),
        tr("Remove unused LMDB pages to shrink the file?\nThe archive is unavailable during this operation.")) != QMessageBox::Yes) return;
    const qint64 before = QFileInfo(m_archive.filePath()).size();
    QProgressDialog dialog(tr("Safely rewriting the archive..."), {}, 0, 0, this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setCancelButton(nullptr);
    dialog.show();
    QApplication::processEvents();
    QString error;
    if (!m_archive.compact(&error)) { showError(error); return; }
    const qint64 after = QFileInfo(m_archive.filePath()).size();
    refresh();
    QMessageBox::information(this, tr("Compaction complete"),
        tr("Archive size: %1 → %2").arg(QLocale().formattedDataSize(before), QLocale().formattedDataSize(after)));
}

void MainWindow::refresh()
{
    if (!m_archive.isOpen()) return;
    QString error;
    m_entries = m_archive.entries(&error);
    if (!error.isEmpty()) { showError(error); return; }
    m_model->setEntries(m_entries, m_search->text());
    m_view->expandToDepth(0);
    qint64 total = 0;
    qsizetype files = 0;
    for (const auto &entry : m_entries) if (!entry.directory) { total += entry.originalSize; ++files; }
    m_summary->setText(tr("%1 files · %2 · %3").arg(files).arg(QLocale().formattedDataSize(total), m_archive.filePath()));
    m_search->setEnabled(true);
    updateActions();
}

void MainWindow::configureShell()
{
    QString error;
    if (ShellIntegration::isMachineInstalled()) {
        QMessageBox::information(this, tr("Windows Explorer Integration"),
            tr("Explorer integration is installed system-wide by the MSI package.\nTo change or remove it, manage LMDB Archiver from Windows Installed apps."));
    } else if (ShellIntegration::isInstalled()) {
        if (QMessageBox::question(this, tr("Windows Explorer Integration"), tr("Explorer integration is installed. Remove it?")) == QMessageBox::Yes) {
            if (!ShellIntegration::uninstall(&error)) showError(error); else QMessageBox::information(this, tr("Done"), tr("Explorer integration removed."));
        }
    } else if (QMessageBox::question(this, tr("Windows Explorer Integration"),
               tr("Install the .lmdb association and file/folder context menus for the current user?\nNo administrator privileges required.")) == QMessageBox::Yes) {
        if (!ShellIntegration::install(QCoreApplication::applicationFilePath(), &error)) showError(error);
        else QMessageBox::information(this, tr("Done"), tr("Explorer integration installed."));
    }
}

void MainWindow::switchLanguage(const QString &languageCode)
{
    QSettings settings;
    if (settings.value(QStringLiteral("ui/language"), QStringLiteral("en")).toString()
            .compare(languageCode, Qt::CaseInsensitive) == 0)
        return;  // nothing to do
    settings.setValue(QStringLiteral("ui/language"), languageCode);
    // The UI is built in code with tr(), so a live swap would need a full retranslate
    // pass. Restarting is simpler and bulletproof for a rarely-used setting.
    const auto choice = QMessageBox::question(
        this, tr("Language"),
        tr("The language will change after restarting the app. Restart now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (choice == QMessageBox::Yes) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
        QCoreApplication::quit();
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("About LMDB Archiver"),
        tr("<h2>LMDB Archiver</h2><p>A fast, reliable LMDB-based desktop archive manager</p>"
           "<p>Version %1 · Qt %2 · LMDB 0.9.33</p>"
           "<p>MIT License</p>").arg(QStringLiteral(LMDBARCHIVER_VERSION), QString::fromLatin1(qVersion())));
}

void MainWindow::updateActions()
{
    const bool open = m_archive.isOpen();
    const bool selected = m_view->selectionModel() && m_view->selectionModel()->hasSelection();
    m_addFilesAction->setEnabled(open); m_addFolderAction->setEnabled(open);
    m_extractAllAction->setEnabled(open); m_extractAction->setEnabled(open && selected);
    m_verifyAction->setEnabled(open);
    m_compactAction->setEnabled(open);
    m_removeAction->setEnabled(open && selected);
    m_copyAction->setEnabled(open && selected);
    m_openEntryAction->setEnabled(open && selected);
}

void MainWindow::showError(const QString &message) { QMessageBox::critical(this, tr("LMDB Archiver"), message); }

QString MainWindow::phaseLabel(ProgressPhase phase, const QString &fallback)
{
    switch (phase) {
    case ProgressPhase::Collecting:  return tr("Collecting files…");
    case ProgressPhase::Processing:  return tr("Processing…");
    case ProgressPhase::Finalizing:  return tr("Saving changes…");
    }
    return fallback;
}

void MainWindow::applyProgress(QProgressDialog &dialog, const ProgressInfo &info)
{
    dialog.setLabelText(formatProgressLabel(info.phase, info.currentItem, info.bytesDone, info.bytesTotal));
    if (info.phase == ProgressPhase::Finalizing) {
        dialog.setRange(0, 0);  // busy spinner while committing
    } else if (info.bytesTotal > 0) {
        const int percent = int((info.bytesDone * 100) / info.bytesTotal);
        dialog.setRange(0, 100);
        dialog.setValue(qBound(0, percent, 100));
    } else {
        dialog.setRange(0, 0);  // unknown total → indeterminate
    }
    QApplication::processEvents();
}
