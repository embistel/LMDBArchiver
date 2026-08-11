#include "archive/archive.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <lmdb.h>
#include <limits>

namespace {
quint64 recordCount(const QString &archivePath)
{
    MDB_env *env = nullptr;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_stat stat{};
    if (mdb_env_create(&env) != MDB_SUCCESS) return std::numeric_limits<quint64>::max();
    const QByteArray path = QDir::toNativeSeparators(archivePath).toUtf8();
    int rc = mdb_env_open(env, path.constData(), MDB_NOSUBDIR | MDB_RDONLY, 0);
    if (rc == MDB_SUCCESS) rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_stat(txn, dbi, &stat);
    if (txn) mdb_txn_abort(txn);
    mdb_env_close(env);
    return rc == MDB_SUCCESS ? stat.ms_entries : std::numeric_limits<quint64>::max();
}

bool createLegacyArchive(const QString &archivePath, const QString &path, const QByteArray &contents)
{
    constexpr quint32 magic = 0x4C4D4441;
    constexpr quint16 version = 1;
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    const QByteArray compressed = qCompress(contents, 7);
    stream << magic << version << false << qint64(contents.size())
           << QDateTime::currentMSecsSinceEpoch() << quint32(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
           << true << compressed;

    MDB_env *env = nullptr;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    if (mdb_env_create(&env) != MDB_SUCCESS) return false;
    mdb_env_set_mapsize(env, 16 * 1024 * 1024);
    const QByteArray native = QDir::toNativeSeparators(archivePath).toUtf8();
    int rc = mdb_env_open(env, native.constData(), MDB_NOSUBDIR, 0664);
    if (rc == MDB_SUCCESS) rc = mdb_txn_begin(env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    QByteArray rawKey = QByteArrayLiteral("entry:") + path.toUtf8();
    MDB_val key{size_t(rawKey.size()), rawKey.data()};
    MDB_val value{size_t(encoded.size()), encoded.data()};
    if (rc == MDB_SUCCESS) rc = mdb_put(txn, dbi, &key, &value, 0);
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    mdb_env_close(env);
    return rc == MDB_SUCCESS;
}

bool corruptFirstChunk(const QString &archivePath)
{
    MDB_env *env = nullptr;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_cursor *cursor = nullptr;
    if (mdb_env_create(&env) != MDB_SUCCESS) return false;
    const QByteArray native = QDir::toNativeSeparators(archivePath).toUtf8();
    int rc = mdb_env_open(env, native.constData(), MDB_NOSUBDIR, 0664);
    if (rc == MDB_SUCCESS) rc = mdb_txn_begin(env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);
    QByteArray prefix("chunk:");
    MDB_val key{size_t(prefix.size()), prefix.data()};
    MDB_val value{};
    if (rc == MDB_SUCCESS) rc = mdb_cursor_get(cursor, &key, &value, MDB_SET_RANGE);
    QByteArray rawKey;
    QByteArray rawValue;
    if (rc == MDB_SUCCESS) {
        rawKey = QByteArray(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        rawValue = QByteArray(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
        if (!rawKey.startsWith(prefix) || rawValue.isEmpty()) rc = MDB_NOTFOUND;
    }
    if (cursor) { mdb_cursor_close(cursor); cursor = nullptr; }
    if (rc == MDB_SUCCESS) {
        rawValue[rawValue.size() - 1] ^= char(0x5a);
        MDB_val changedKey{size_t(rawKey.size()), rawKey.data()};
        MDB_val changedValue{size_t(rawValue.size()), rawValue.data()};
        rc = mdb_put(txn, dbi, &changedKey, &changedValue, 0);
    }
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    mdb_env_close(env);
    return rc == MDB_SUCCESS;
}
}

class ArchiveTest final : public QObject {
    Q_OBJECT
private slots:
    void roundTripDirectory();
    void replaceAndRemove();
    void reopenArchive();
    void legacyCompatibility();
    void transactionalCancellation();
    void mapGrowth();
    void clearArchive();
    void integrityVerification();
    void compactArchive();
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
    const QDateTime sourceDirectoryTime = QFileInfo(source).lastModified();

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
    const QDateTime restoredDirectoryTime = QFileInfo(output + QStringLiteral("/자료")).lastModified();
    QVERIFY(qAbs(sourceDirectoryTime.msecsTo(restoredDirectoryTime)) < 2000);
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
    archive.close();
    QCOMPARE(recordCount(temp.filePath(QStringLiteral("update.lmdb"))), quint64(0));
}

void ArchiveTest::legacyCompatibility()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archivePath = temp.filePath(QStringLiteral("legacy.lmdb"));
    const QByteArray expected("legacy format payload");
    QVERIFY(createLegacyArchive(archivePath, QStringLiteral("old/data.txt"), expected));
    Archive archive;
    QString error;
    QVERIFY2(archive.open(archivePath, false, &error), qPrintable(error));
    const auto entries = archive.entries(&error);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("old/data.txt"));
    const QString output = temp.filePath(QStringLiteral("legacy-output"));
    QVERIFY2(archive.extract({}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/old/data.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), expected);
}

void ArchiveTest::transactionalCancellation()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("cancel.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("original");
    file.close();
    const QString archivePath = temp.filePath(QStringLiteral("cancel.lmdb"));
    Archive archive;
    QString error;
    QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
    QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));

    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    for (int block = 0; block < 10; ++block) file.write(QByteArray(1024 * 1024, char('k' + block)));
    file.close();
    int addCallbacks = 0;
    error.clear();
    QVERIFY(!archive.addPaths({source}, {}, &error,
        [&addCallbacks](const QString &, qsizetype, qsizetype) { return ++addCallbacks < 3; }));

    const QString restoredRoot = temp.filePath(QStringLiteral("restored-original"));
    error.clear();
    QVERIFY2(archive.extract({}, restoredRoot, &error), qPrintable(error));
    QFile original(restoredRoot + QStringLiteral("/cancel.bin"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), QByteArray("original"));

    const QString partialRoot = temp.filePath(QStringLiteral("partial"));
    QVERIFY(QDir().mkpath(partialRoot));
    QFile protectedFile(partialRoot + QStringLiteral("/cancel.bin"));
    QVERIFY(protectedFile.open(QIODevice::WriteOnly));
    protectedFile.write("keep-me");
    protectedFile.close();
    int extractCallbacks = 0;
    error.clear();
    QVERIFY(!archive.extract({}, partialRoot, &error,
        [&extractCallbacks](const QString &, qsizetype, qsizetype) { return ++extractCallbacks < 2; }));
    QVERIFY(protectedFile.open(QIODevice::ReadOnly));
    QCOMPARE(protectedFile.readAll(), QByteArray("keep-me"));
}

void ArchiveTest::mapGrowth()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("large-random.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    quint32 state = 0x12345678u;
    QByteArray block(1024 * 1024, Qt::Uninitialized);
    for (int part = 0; part < 36; ++part) {
        for (char &byte : block) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            byte = char(state & 0xff);
        }
        QCOMPARE(file.write(block), qint64(block.size()));
    }
    file.close();
    const QString archivePath = temp.filePath(QStringLiteral("grown.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));
        const auto entries = archive.entries(&error);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().originalSize, qint64(36 * 1024 * 1024));
    }
    Archive reopened;
    QVERIFY2(reopened.open(archivePath, false, &error), qPrintable(error));
    QCOMPARE(reopened.entries(&error).size(), 1);
}

void ArchiveTest::clearArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("clear.txt"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("clear this");
    file.close();
    const QString archivePath = temp.filePath(QStringLiteral("clear.lmdb"));
    Archive archive;
    QString error;
    QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
    QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));
    QCOMPARE(archive.entries(&error).size(), 1);
    QVERIFY2(archive.clear(&error), qPrintable(error));
    QCOMPARE(archive.entries(&error).size(), 0);
    archive.close();
    QCOMPARE(recordCount(archivePath), quint64(0));
}

void ArchiveTest::integrityVerification()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("integrity.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(2 * 1024 * 1024, 'z'));
    file.close();
    const QString archivePath = temp.filePath(QStringLiteral("integrity.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));
        QVERIFY2(archive.verify(&error), qPrintable(error));
    }
    QVERIFY(corruptFirstChunk(archivePath));
    Archive corrupted;
    QVERIFY2(corrupted.open(archivePath, false, &error), qPrintable(error));
    QVERIFY(!corrupted.verify(&error));
    QVERIFY(!error.isEmpty());
}

void ArchiveTest::compactArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    QStringList sources;
    quint32 state = 0x91e10da5u;
    QByteArray block(1024 * 1024, Qt::Uninitialized);
    for (int fileIndex = 0; fileIndex < 2; ++fileIndex) {
        const QString source = temp.filePath(QStringLiteral("compact-%1.bin").arg(fileIndex));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        for (int part = 0; part < 36; ++part) {
            for (char &byte : block) {
                state ^= state << 13; state ^= state >> 17; state ^= state << 5;
                byte = char(state & 0xff);
            }
            QCOMPARE(file.write(block), qint64(block.size()));
        }
        sources << source;
    }
    const QString archivePath = temp.filePath(QStringLiteral("compact.lmdb"));
    Archive archive;
    QString error;
    QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
    QVERIFY2(archive.addPaths(sources, {}, &error), qPrintable(error));
    QVERIFY2(archive.removePaths({QStringLiteral("compact-0.bin")}, &error), qPrintable(error));
    const qint64 before = QFileInfo(archivePath).size();
    QVERIFY2(archive.compact(&error), qPrintable(error));
    const qint64 after = QFileInfo(archivePath).size();
    QVERIFY2(after < before, qPrintable(QStringLiteral("compact size did not shrink: %1 -> %2").arg(before).arg(after)));
    QVERIFY2(archive.verify(&error), qPrintable(error));
    const auto entries = archive.entries(&error);
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().path, QStringLiteral("compact-1.bin"));
}

void ArchiveTest::reopenArchive()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("reopen.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int block = 0; block < 10; ++block)
        QCOMPARE(file.write(QByteArray(1024 * 1024, char('a' + block))), qint64(1024 * 1024));
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
    const QString output = temp.filePath(QStringLiteral("streamed"));
    QVERIFY2(reopened.extract({}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/reopen.bin"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.size(), qint64(10 * 1024 * 1024));
    for (int block = 0; block < 10; ++block)
        QCOMPARE(restored.read(1024 * 1024), QByteArray(1024 * 1024, char('a' + block)));
}

QTEST_APPLESS_MAIN(ArchiveTest)
#include "archive_test.moc"
