#include "archive/archive.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

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
        if (!archive.open(archivePath, true, &error) || !archive.addPaths(arguments, {}, &error)) return fail(error);
        QTextStream(stdout) << archivePath << Qt::endl;
        return 0;
    }

    if (arguments.isEmpty()) { printUsage(parser); return 2; }
    const QString archivePath = arguments.takeFirst();
    if (!archive.open(archivePath, false, &error)) return fail(error);

    if (command == QStringLiteral("add")) {
        if (arguments.isEmpty()) { printUsage(parser); return 2; }
        if (!archive.addPaths(arguments, parser.value(QStringLiteral("destination")), &error)) return fail(error);
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
        if (!archive.extract(arguments, output, &error)) return fail(error);
        return 0;
    }
    if (command == QStringLiteral("remove")) {
        if (arguments.isEmpty()) { printUsage(parser); return 2; }
        if (!archive.removePaths(arguments, &error)) return fail(error);
        return 0;
    }
    if (command == QStringLiteral("test")) {
        if (!archive.verify(&error)) return fail(error);
        QTextStream(stdout) << "OK" << Qt::endl;
        return 0;
    }
    if (command == QStringLiteral("compact")) {
        if (!archive.compact(&error)) return fail(error);
        return 0;
    }
    return fail(QStringLiteral("Unknown command: %1").arg(command));
}
