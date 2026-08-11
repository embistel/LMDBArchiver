#include "archive/archive.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ArchiveTest final : public QObject {
    Q_OBJECT
private slots:
    void roundTripDirectory();
    void replaceAndRemove();
    void reopenArchive();
};

void ArchiveTest::roundTripDirectory()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("자료"));
    QVERIFY(QDir().mkpath(source + QStringLiteral("/nested/empty")));
    QFile text(source + QStringLiteral("/nested/안녕.txt"));
    QVERIFY(text.open(QIODevice::WriteOnly));
    QCOMPARE(text.write("LMDB Archiver\n"), qint64(14));
    text.close();
    QFile binary(source + QStringLiteral("/zeros.bin"));
    QVERIFY(binary.open(QIODevice::WriteOnly));
    QCOMPARE(binary.write(QByteArray(128 * 1024, '\0')), qint64(128 * 1024));
    binary.close();

    Archive archive;
    QString error;
    QVERIFY2(archive.open(temp.filePath(QStringLiteral("sample.lmdb")), true, &error), qPrintable(error));
    QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));
    const auto entries = archive.entries(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(entries.size(), 5);

    const QString output = temp.filePath(QStringLiteral("output"));
    QVERIFY2(archive.extract({}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/자료/nested/안녕.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("LMDB Archiver\n"));
    QVERIFY(QFileInfo::exists(output + QStringLiteral("/자료/nested/empty")));
}

void ArchiveTest::replaceAndRemove()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString path = temp.filePath(QStringLiteral("note.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("one"); file.close();
    Archive archive;
    QString error;
    QVERIFY2(archive.open(temp.filePath(QStringLiteral("update.lmdb")), true, &error), qPrintable(error));
    QVERIFY2(archive.addPaths({path}, QStringLiteral("docs"), &error), qPrintable(error));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("two-two"); file.close();
    QVERIFY2(archive.addPaths({path}, QStringLiteral("docs"), &error), qPrintable(error));
    QCOMPARE(archive.entries(&error).size(), 1);
    const QString output = temp.filePath(QStringLiteral("out"));
    QVERIFY2(archive.extract({QStringLiteral("docs/note.txt")}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/docs/note.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("two-two"));
    QVERIFY2(archive.removePaths({QStringLiteral("docs")}, &error), qPrintable(error));
    QCOMPARE(archive.entries(&error).size(), 0);
}

void ArchiveTest::reopenArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("reopen.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(1024 * 1024, 'x'));
    file.close();
    const QString archivePath = temp.filePath(QStringLiteral("다시열기.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));
    }
    Archive reopened;
    QVERIFY2(reopened.open(archivePath, false, &error), qPrintable(error));
    QCOMPARE(reopened.entries(&error).size(), 1);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

QTEST_APPLESS_MAIN(ArchiveTest)
#include "archive_test.moc"
