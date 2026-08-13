#include "archive/archive.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
int fail(const QString &message)
{
    QTextStream stream(stderr);
    stream << message << Qt::endl;
    return 1;
}

void printUsage(QCommandLineParser &parser)
{
    QTextStream stream(stderr);
    stream << parser.helpText() << Qt::endl
           << "Commands:" << Qt::endl
           << "  create <archive.lmdb> <path>..." << Qt::endl
           << "  add <archive.lmdb> <path>... [--destination <internal-path>]" << Qt::endl
           << "  list <archive.lmdb>" << Qt::endl
           << "  extract <archive.lmdb> <output-directory> [internal-path]..." << Qt::endl
           << "  remove <archive.lmdb> <internal-path>..." << Qt::endl;
    stream << "  test <archive.lmdb>" << Qt::endl;
    stream << "  compact <archive.lmdb>" << Qt::endl;
}

bool stderrIsTty()
{
#ifdef Q_OS_WIN
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
}

// Returns a CLI progress callback that prints a single updating line to stderr
// (carriage-return refreshed) when stderr is a TTY. When not a TTY it is a no-op
// so redirected output stays clean.
Archive::Progress makeCliProgress()
{
    if (!stderrIsTty()) return {};
    return [](const ProgressInfo &info) {
        QString line;
        const QString item = info.currentItem.isEmpty() ? QString() : QStringLiteral(" · ") + info.currentItem;
        if (info.bytesTotal > 0) {
            const int percent = int((info.bytesDone * 100) / info.bytesTotal);
            line = QStringLiteral("%1% · %2/%3%4")
                       .arg(qBound(0, percent, 100))
                       .arg(QLocale().formattedDataSize(info.bytesDone),
                            QLocale().formattedDataSize(info.bytesTotal), item);
        } else if (info.bytesDone > 0) {
            line = QLocale().formattedDataSize(info.bytesDone) + item;
        } else {
            line = item.trimmed();
        }
        QString phaseTag;
        switch (info.phase) {
        case ProgressPhase::Collecting:  phaseTag = QStringLiteral("[scan] "); break;
        case ProgressPhase::Finalizing:  phaseTag = QStringLiteral("[commit] "); break;
        case ProgressPhase::Processing:  break;
        }
        QTextStream(stderr) << u'\r' << phaseTag << line.leftJustified(60, u' ', true);
        return true;
    };
}

void finishCliProgress()
{
    if (stderrIsTty()) QTextStream(stderr) << Qt::endl;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("LMDBArchiverCLI"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LMDBARCHIVER_VERSION));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("LMDB Archiver command-line interface"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringList{QStringLiteral("d"), QStringLiteral("destination")},
                      QStringLiteral("Internal destination for added paths"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("compress"),
                      QStringLiteral("Compress added files with standard gzip (keys gain a \".<hash>.gz\" marker)")});
    parser.addOption({QStringLiteral("no-decompress"),
                      QStringLiteral("On extract, leave gzip-compressed entries as .gz files instead of decompressing")});
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("Operation to perform"));
    parser.addPositionalArgument(QStringLiteral("arguments"), QStringLiteral("Arguments for the operation"), QStringLiteral("[arguments...]"));
    parser.process(application);

    QStringList arguments = parser.positionalArguments();
    if (arguments.isEmpty()) { printUsage(parser); return 2; }
    const QString command = arguments.takeFirst().toLower();
    QString error;
    Archive archive;

    if (command == QStringLiteral("create")) {
        if (arguments.size() < 2) { printUsage(parser); return 2; }
        const QString archivePath = QFileInfo(arguments.takeFirst()).absoluteFilePath();
        if (QFileInfo::exists(archivePath)) return fail(QStringLiteral("Refusing to overwrite existing archive: %1").arg(archivePath));
        const auto progress = makeCliProgress();
        const bool compress = parser.isSet(QStringLiteral("compress"));
        if (!archive.open(archivePath, true, &error) || !archive.addPaths(arguments, {}, &error, progress, compress)) return fail(error);
        finishCliProgress();
        QTextStream(stdout) << archivePath << Qt::endl;
        return 0;
    }

    if (arguments.isEmpty()) { printUsage(parser); return 2; }
    const QString archivePath = arguments.takeFirst();
    if (!archive.open(archivePath, false, &error)) return fail(error);

    if (command == QStringLiteral("add")) {
        if (arguments.isEmpty()) { printUsage(parser); return 2; }
        const auto progress = makeCliProgress();
        const bool compress = parser.isSet(QStringLiteral("compress"));
        if (!archive.addPaths(arguments, parser.value(QStringLiteral("destination")), &error, progress, compress)) return fail(error);
        finishCliProgress();
        return 0;
    }
    if (command == QStringLiteral("list")) {
        const auto entries = archive.entries(&error);
        if (!error.isEmpty()) return fail(error);
        QTextStream output(stdout);
        for (const ArchiveEntry &entry : entries)
            output << (entry.directory ? QStringLiteral("d") : QStringLiteral("f")) << '\t'
                   << entry.originalSize << '\t' << entry.storedSize << '\t' << entry.path << Qt::endl;
        return 0;
    }
    if (command == QStringLiteral("extract")) {
        if (arguments.isEmpty()) { printUsage(parser); return 2; }
        const QString output = arguments.takeFirst();
        const auto progress = makeCliProgress();
        const bool autoDecompress = !parser.isSet(QStringLiteral("no-decompress"));
        if (!archive.extract(arguments, output, &error, progress, autoDecompress)) return fail(error);
        finishCliProgress();
        return 0;
    }
    if (command == QStringLiteral("remove")) {
        if (arguments.isEmpty()) { printUsage(parser); return 2; }
        if (!archive.removePaths(arguments, &error)) return fail(error);
        return 0;
    }
    if (command == QStringLiteral("test")) {
        const auto progress = makeCliProgress();
        if (!archive.verify(&error, progress)) return fail(error);
        finishCliProgress();
        QTextStream(stdout) << "OK" << Qt::endl;
        return 0;
    }
    if (command == QStringLiteral("compact")) {
        if (!archive.compact(&error)) return fail(error);
        return 0;
    }
    return fail(QStringLiteral("Unknown command: %1").arg(command));
}
