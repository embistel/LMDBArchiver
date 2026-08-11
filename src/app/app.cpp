#include "app/app.h"

#include "app/mainwindow.h"
#include "archive/archive.h"
#include "platform/shellintegration.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

namespace {
QString uniqueArchivePath(const QString &source)
{
    QString candidate = QFileInfo(source).absoluteFilePath() + QStringLiteral(".lmdb");
    for (int i = 2; QFileInfo::exists(candidate); ++i)
        candidate = QFileInfo(source).absoluteFilePath() + QStringLiteral(" (%1).lmdb").arg(i);
    return candidate;
}

int createFrom(const QString &source)
{
    const QString output = uniqueArchivePath(source);
    Archive archive;
    QString error;
    if (!archive.open(output, true, &error) || !archive.addPaths({source}, {}, &error)) {
        QMessageBox::critical(nullptr, QObject::tr("LMDB Archiver"), error); return 1;
    }
    QMessageBox::information(nullptr, QObject::tr("LMDB Archiver"), QObject::tr("아카이브를 만들었습니다.\n%1").arg(output));
    return 0;
}

int extractHere(const QString &archivePath)
{
    const QFileInfo info(archivePath);
    const QString destination = info.absoluteDir().absoluteFilePath(info.completeBaseName());
    Archive archive;
    QString error;
    if (!archive.open(archivePath, false, &error) || !archive.extract({}, destination, &error)) {
        QMessageBox::critical(nullptr, QObject::tr("LMDB Archiver"), error); return 1;
    }
    QMessageBox::information(nullptr, QObject::tr("LMDB Archiver"), QObject::tr("아카이브를 풀었습니다.\n%1").arg(destination));
    return 0;
}
}

int App::run(QApplication &application)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("LMDB 기반 아카이브 관리자"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("archive"), QObject::tr("열 .lmdb 파일"), QStringLiteral("[archive]"));
    parser.addOption({QStringLiteral("create-from"), QObject::tr("파일 또는 폴더로 아카이브 생성"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("extract-here"), QObject::tr("아카이브를 같은 위치에 풀기"), QStringLiteral("archive")});
    parser.addOption({QStringLiteral("add-dialog"), QObject::tr("대상 아카이브를 선택해 경로 추가"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("install-shell"), QObject::tr("Windows 탐색기 통합 설치")});
    parser.addOption({QStringLiteral("uninstall-shell"), QObject::tr("Windows 탐색기 통합 제거")});
    parser.process(application);

    if (parser.isSet(QStringLiteral("create-from"))) return createFrom(parser.value(QStringLiteral("create-from")));
    if (parser.isSet(QStringLiteral("extract-here"))) return extractHere(parser.value(QStringLiteral("extract-here")));
    if (parser.isSet(QStringLiteral("install-shell")) || parser.isSet(QStringLiteral("uninstall-shell"))) {
        QString error;
        const bool ok = parser.isSet(QStringLiteral("install-shell"))
            ? ShellIntegration::install(QCoreApplication::applicationFilePath(), &error)
            : ShellIntegration::uninstall(&error);
        if (!ok) QMessageBox::critical(nullptr, QObject::tr("LMDB Archiver"), error);
        return ok ? 0 : 1;
    }
    if (parser.isSet(QStringLiteral("add-dialog"))) {
        const QString archivePath = QFileDialog::getOpenFileName(nullptr, QObject::tr("추가할 LMDB 아카이브"), {}, QObject::tr("LMDB 아카이브 (*.lmdb)"));
        if (archivePath.isEmpty()) return 0;
        Archive archive;
        QString error;
        if (!archive.open(archivePath, false, &error) || !archive.addPaths({parser.value(QStringLiteral("add-dialog"))}, {}, &error)) {
            QMessageBox::critical(nullptr, QObject::tr("LMDB Archiver"), error); return 1;
        }
        return 0;
    }

    MainWindow window;
    const QStringList positional = parser.positionalArguments();
    if (!positional.isEmpty()) window.openArchive(positional.first());
    window.show();
    return application.exec();
}

