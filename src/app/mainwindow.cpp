#include "app/mainwindow.h"

#include "app/archivemodel.h"
#include "app/dropview.h"
#include "platform/shellintegration.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressDialog>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QAction *makeAction(QObject *parent, const QIcon &icon, const QString &text,
                    const QKeySequence &shortcut = {})
{
    auto *action = new QAction(icon, text, parent);
    if (!shortcut.isEmpty()) action->setShortcut(shortcut);
    return action;
}
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("LMDB Archiver"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.svg")));
    resize(1120, 720);
    setMinimumSize(760, 480);

    auto *container = new QWidget(this);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 10, 12, 8);
    layout->setSpacing(8);

    m_search = new QLineEdit(container);
    m_search->setPlaceholderText(tr("아카이브에서 검색..."));
    m_search->setClearButtonEnabled(true);
    m_search->setEnabled(false);
    layout->addWidget(m_search);

    m_view = new DropView(container);
    m_model = new ArchiveModel(m_view);
    m_view->setModel(m_model);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(0, Qt::AscendingOrder);
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) m_view->header()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    layout->addWidget(m_view, 1);
    setCentralWidget(container);

    auto *fileMenu = menuBar()->addMenu(tr("파일(&F)"));
    auto *newAction = makeAction(this, style()->standardIcon(QStyle::SP_FileIcon), tr("새 아카이브(&N)..."), QKeySequence::New);
    auto *openAction = makeAction(this, style()->standardIcon(QStyle::SP_DialogOpenButton), tr("열기(&O)..."), QKeySequence::Open);
    auto *exitAction = makeAction(this, {}, tr("끝내기(&X)"), QKeySequence::Quit);
    fileMenu->addActions({newAction, openAction});
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    auto *editMenu = menuBar()->addMenu(tr("편집(&E)"));
    m_addFilesAction = makeAction(this, style()->standardIcon(QStyle::SP_FileDialogNewFolder), tr("파일 추가(&A)..."), QKeySequence(Qt::CTRL | Qt::Key_I));
    m_addFolderAction = makeAction(this, style()->standardIcon(QStyle::SP_DirIcon), tr("폴더 추가(&D)..."));
    auto *pasteAction = makeAction(this, {}, tr("클립보드의 파일 추가(&P)"), QKeySequence::Paste);
    m_removeAction = makeAction(this, style()->standardIcon(QStyle::SP_TrashIcon), tr("선택 항목 삭제(&R)"), QKeySequence::Delete);
    editMenu->addActions({m_addFilesAction, m_addFolderAction, pasteAction});
    editMenu->addSeparator();
    editMenu->addAction(m_removeAction);

    auto *archiveMenu = menuBar()->addMenu(tr("아카이브(&A)"));
    m_extractAction = makeAction(this, style()->standardIcon(QStyle::SP_DialogSaveButton), tr("선택 항목 풀기(&E)..."), QKeySequence(Qt::CTRL | Qt::Key_E));
    m_extractAllAction = makeAction(this, {}, tr("모두 풀기(&X)..."));
    auto *refreshAction = makeAction(this, style()->standardIcon(QStyle::SP_BrowserReload), tr("새로 고침"), QKeySequence::Refresh);
    archiveMenu->addActions({m_extractAction, m_extractAllAction, refreshAction});

    auto *toolsMenu = menuBar()->addMenu(tr("도구(&T)"));
    auto *shellAction = makeAction(this, {}, tr("Windows 탐색기 통합..."));
    toolsMenu->addAction(shellAction);
    auto *helpMenu = menuBar()->addMenu(tr("도움말(&H)"));
    auto *aboutAction = makeAction(this, {}, tr("LMDB Archiver 정보"));
    helpMenu->addAction(aboutAction);

    auto *toolbar = addToolBar(tr("주 도구 모음"));
    toolbar->setObjectName(QStringLiteral("MainToolbar"));
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addActions({newAction, openAction});
    toolbar->addSeparator();
    toolbar->addActions({m_addFilesAction, m_addFolderAction, m_extractAction, m_removeAction});

    m_summary = new QLabel(tr("아카이브를 열거나 새로 만드세요. 파일과 폴더를 창으로 끌어 놓을 수 있습니다."), this);
    statusBar()->addWidget(m_summary, 1);

    connect(newAction, &QAction::triggered, this, &MainWindow::newArchive);
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseArchive);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);
    connect(m_addFilesAction, &QAction::triggered, this, &MainWindow::addFiles);
    connect(m_addFolderAction, &QAction::triggered, this, &MainWindow::addFolder);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::pasteFiles);
    connect(m_removeAction, &QAction::triggered, this, &MainWindow::removeSelected);
    connect(m_extractAction, &QAction::triggered, this, &MainWindow::extractSelected);
    connect(m_extractAllAction, &QAction::triggered, this, &MainWindow::extractAll);
    connect(refreshAction, &QAction::triggered, this, &MainWindow::refresh);
    connect(shellAction, &QAction::triggered, this, &MainWindow::configureShell);
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    connect(m_view, &DropView::localFilesDropped, this, &MainWindow::addDropped);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateActions);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { m_model->setEntries(m_entries, text); m_view->expandToDepth(0); });
    updateActions();

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
}

bool MainWindow::openArchive(const QString &path)
{
    QString error;
    if (!m_archive.open(path, false, &error)) { showError(error); return false; }
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — LMDB Archiver"));
    refresh();
    return true;
}

void MainWindow::newArchive()
{
    QString path = QFileDialog::getSaveFileName(this, tr("새 LMDB 아카이브"), {}, tr("LMDB 아카이브 (*.lmdb)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".lmdb"), Qt::CaseInsensitive)) path += QStringLiteral(".lmdb");
    QString error;
    if (!m_archive.open(path, true, &error)) { showError(error); return; }
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" — LMDB Archiver"));
    refresh();
}

void MainWindow::chooseArchive()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("LMDB 아카이브 열기"), {}, tr("LMDB 아카이브 (*.lmdb);;모든 파일 (*.*)"));
    if (!path.isEmpty()) openArchive(path);
}

void MainWindow::addFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("추가할 파일"));
    addLocalPaths(paths);
}

void MainWindow::addFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, tr("추가할 폴더"));
    if (!path.isEmpty()) addLocalPaths({path});
}

void MainWindow::addDropped(const QStringList &paths, const QString &destination) { addLocalPaths(paths, destination); }

void MainWindow::pasteFiles()
{
    QStringList paths;
    const QMimeData *mime = QApplication::clipboard()->mimeData();
    for (const QUrl &url : mime->urls()) if (url.isLocalFile()) paths << url.toLocalFile();
    if (paths.isEmpty()) { statusBar()->showMessage(tr("클립보드에 추가할 파일이 없습니다."), 3000); return; }
    addLocalPaths(paths);
}

bool MainWindow::addLocalPaths(const QStringList &paths, const QString &destination)
{
    if (!m_archive.isOpen() || paths.isEmpty()) return false;
    QProgressDialog dialog(tr("항목을 아카이브에 추가하는 중..."), tr("취소"), 0, paths.size(), this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setMinimumDuration(250);
    QString error;
    const bool ok = m_archive.addPaths(paths, destination, &error,
        [&dialog](const QString &path, qsizetype current, qsizetype total) {
            dialog.setMaximum(int(total)); dialog.setValue(int(current)); dialog.setLabelText(path);
            QApplication::processEvents(); return !dialog.wasCanceled();
        });
    if (!ok) { showError(error.isEmpty() ? tr("추가 작업이 취소되었습니다.") : error); return false; }
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
    if (QMessageBox::question(this, tr("항목 삭제"), tr("선택한 %1개 항목을 아카이브에서 삭제할까요?\n이 작업은 되돌릴 수 없습니다.").arg(paths.size())) != QMessageBox::Yes) return;
    QString error;
    if (!m_archive.removePaths(paths, &error)) { showError(error); return; }
    refresh();
}

void MainWindow::extractSelected()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return;
    const QString destination = QFileDialog::getExistingDirectory(this, tr("선택 항목을 풀 폴더"));
    if (destination.isEmpty()) return;
    QString error;
    if (!m_archive.extract(paths, destination, &error)) showError(error);
    else statusBar()->showMessage(tr("선택 항목을 풀었습니다."), 4000);
}

void MainWindow::extractAll()
{
    if (!m_archive.isOpen()) return;
    const QString destination = QFileDialog::getExistingDirectory(this, tr("모든 항목을 풀 폴더"));
    if (destination.isEmpty()) return;
    QString error;
    if (!m_archive.extract({}, destination, &error)) showError(error);
    else statusBar()->showMessage(tr("아카이브를 모두 풀었습니다."), 4000);
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
    m_summary->setText(tr("%1개 파일 · %2 · %3").arg(files).arg(QLocale().formattedDataSize(total), m_archive.filePath()));
    m_search->setEnabled(true);
    updateActions();
}

void MainWindow::configureShell()
{
    QString error;
    if (ShellIntegration::isInstalled()) {
        if (QMessageBox::question(this, tr("Windows 탐색기 통합"), tr("탐색기 통합이 설치되어 있습니다. 제거할까요?")) == QMessageBox::Yes) {
            if (!ShellIntegration::uninstall(&error)) showError(error); else QMessageBox::information(this, tr("완료"), tr("탐색기 통합을 제거했습니다."));
        }
    } else if (QMessageBox::question(this, tr("Windows 탐색기 통합"),
               tr("현재 사용자 계정에 .lmdb 연결과 파일·폴더 우클릭 메뉴를 설치할까요?\n관리자 권한은 필요하지 않습니다.")) == QMessageBox::Yes) {
        if (!ShellIntegration::install(QCoreApplication::applicationFilePath(), &error)) showError(error);
        else QMessageBox::information(this, tr("완료"), tr("탐색기 통합을 설치했습니다."));
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, tr("LMDB Archiver 정보"),
        tr("<h2>LMDB Archiver</h2><p>빠르고 안정적인 LMDB 기반 데스크톱 아카이브 관리자</p>"
           "<p>버전 %1 · Qt %2 · LMDB 0.9.33</p>"
           "<p>MIT 라이선스</p>").arg(QStringLiteral(LMDBARCHIVER_VERSION), QString::fromLatin1(qVersion())));
}

void MainWindow::updateActions()
{
    const bool open = m_archive.isOpen();
    const bool selected = m_view->selectionModel() && m_view->selectionModel()->hasSelection();
    m_addFilesAction->setEnabled(open); m_addFolderAction->setEnabled(open);
    m_extractAllAction->setEnabled(open); m_extractAction->setEnabled(open && selected);
    m_removeAction->setEnabled(open && selected);
}

void MainWindow::showError(const QString &message) { QMessageBox::critical(this, tr("LMDB Archiver"), message); }

