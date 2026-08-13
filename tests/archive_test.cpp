#include "archive/archive.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <lmdb.h>
#include <limits>

namespace {
// Opens the archive with a bare LMDB handle and reports the number of records.
// Useful for verifying that remove/clear operations leave a truly empty store.
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

// Reads a single raw value back from the store using a bare LMDB handle.
// Confirms the on-disk schema is plain key=string -> value=bytes with no wrappers.
QByteArray readRawValue(const QString &archivePath, const QString &key)
{
    MDB_env *env = nullptr;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    QByteArray result;
    if (mdb_env_create(&env) != MDB_SUCCESS) return result;
    const QByteArray path = QDir::toNativeSeparators(archivePath).toUtf8();
    int rc = mdb_env_open(env, path.constData(), MDB_NOSUBDIR | MDB_RDONLY, 0);
    if (rc == MDB_SUCCESS) rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    const QByteArray rawKey = key.toUtf8();
    MDB_val mdbKey{size_t(rawKey.size()), const_cast<char *>(rawKey.constData())};
    MDB_val mdbValue{};
    if (rc == MDB_SUCCESS) rc = mdb_get(txn, dbi, &mdbKey, &mdbValue);
    if (rc == MDB_SUCCESS)
        result = QByteArray(static_cast<const char *>(mdbValue.mv_data), qsizetype(mdbValue.mv_size));
    if (txn) mdb_txn_abort(txn);
    mdb_env_close(env);
    return result;
}
}

class ArchiveTest final : public QObject {
    Q_OBJECT
private slots:
    void roundTripDirectory();
    void replaceAndRemove();
    void transactionalCancellation();
    void mapGrowth();
    void clearArchive();
    void readabilityCheck();
    void compactArchive();
    void rawSchemaRoundTrip();
    void compressedRoundTrip();
    void compressedNoDecompressExtract();
    void mixedCompressedAndRaw();
    void realGzipFileUntouched();
    void lucioraElaCompatibility();
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
    // Three file records plus synthesized directory rows (자료, 자료/nested). The empty
    // folder is not preserved because the native schema stores no directory records.
    int fileCount = 0;
    for (const ArchiveEntry &entry : entries) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 2);

    const QString output = temp.filePath(QStringLiteral("output"));
    QVERIFY2(archive.extract({}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/자료/nested/안녕.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("LMDB Archiver\n"));
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
    int fileCount = 0;
    for (const ArchiveEntry &entry : archive.entries(&error)) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 1);
    const QString output = temp.filePath(QStringLiteral("out"));
    QVERIFY2(archive.extract({QStringLiteral("docs/note.txt")}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/docs/note.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray("two-two"));
    QVERIFY2(archive.removePaths({QStringLiteral("docs")}, &error), qPrintable(error));
    for (const ArchiveEntry &entry : archive.entries(&error)) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 1);  // still 1 — docs is synthetic, the only file is already gone
    archive.close();
    QCOMPARE(recordCount(temp.filePath(QStringLiteral("update.lmdb"))), quint64(0));
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
        [&addCallbacks](const ProgressInfo &) { return ++addCallbacks < 3; }));

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
    // Cancel on the first progress signal. Aborting must preserve the pre-existing
    // protected file exactly as before the extract.
    int extractCallbacks = 0;
    error.clear();
    QVERIFY(!archive.extract({}, partialRoot, &error,
        [&extractCallbacks](const ProgressInfo &) { return ++extractCallbacks < 1; }));
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
        int fileCount = 0;
        for (const ArchiveEntry &entry : entries) if (!entry.directory) ++fileCount;
        QCOMPARE(fileCount, 1);
        QCOMPARE(entries.first().originalSize, qint64(36 * 1024 * 1024));
    }
    Archive reopened;
    QVERIFY2(reopened.open(archivePath, false, &error), qPrintable(error));
    int fileCount = 0;
    for (const ArchiveEntry &entry : reopened.entries(&error)) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 1);
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
    int fileCount = 0;
    for (const ArchiveEntry &entry : archive.entries(&error)) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 1);
    QVERIFY2(archive.clear(&error), qPrintable(error));
    for (const ArchiveEntry &entry : archive.entries(&error)) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 1);
    archive.close();
    QCOMPARE(recordCount(archivePath), quint64(0));
}

void ArchiveTest::readabilityCheck()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString source = temp.filePath(QStringLiteral("readable.bin"));
    QFile file(source);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(2 * 1024 * 1024, 'z'));
    file.close();
    const QString archivePath = temp.filePath(QStringLiteral("readable.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({source}, {}, &error), qPrintable(error));
        QVERIFY2(archive.verify(&error), qPrintable(error));
    }
    Archive reopened;
    QVERIFY2(reopened.open(archivePath, false, &error), qPrintable(error));
    QVERIFY2(reopened.verify(&error), qPrintable(error));
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
    int fileCount = 0;
    for (const ArchiveEntry &entry : archive.entries(&error)) if (!entry.directory) ++fileCount;
    QCOMPARE(fileCount, 1);
    QCOMPARE(archive.entries(&error).first().path, QStringLiteral("compact-1.bin"));
}

void ArchiveTest::rawSchemaRoundTrip()
{
    // Confirms the on-disk schema is plain key=string -> value=bytes with no wrappers.
    // A bare LMDB reader (no Archiver code) must see the exact original bytes.
    // The readRawValue helper opens a second env on the same file, so the Archive must
    // be closed first to avoid Windows reader-locktable contention (MDB_BAD_RSLOT).
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString sourcePath = temp.filePath(QStringLiteral("payload.bin"));
    QByteArray payload(2048, Qt::Uninitialized);
    for (int i = 0; i < payload.size(); ++i) payload[i] = char(i * 7 + 3);
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), qint64(payload.size()));
    source.close();
    const QString archivePath = temp.filePath(QStringLiteral("schema.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({sourcePath}, QStringLiteral("data"), &error), qPrintable(error));
        archive.close();
    }
    QCOMPARE(readRawValue(archivePath, QStringLiteral("data/payload.bin")), payload);
}

void ArchiveTest::compressedRoundTrip()
{
    // Add a highly-compressible file with compress=true, then extract with
    // auto-decompress. The original bytes must be restored exactly.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString sourcePath = temp.filePath(QStringLiteral("repetitive.bin"));
    QByteArray payload(64 * 1024, 'A');
    for (int i = 0; i < payload.size(); i += 97) payload[i] = char('A' + (i % 26));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), qint64(payload.size()));
    source.close();
    const QString archivePath = temp.filePath(QStringLiteral("compressed.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({sourcePath}, QStringLiteral("docs"), &error, {}, true), qPrintable(error));
        const auto entries = archive.entries(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        int fileCount = 0;
        for (const ArchiveEntry &entry : entries) {
            if (entry.directory) continue;
            ++fileCount;
            // Stored under a ".<8hex>.gz" key; reported original size matches the source.
            QVERIFY(entry.path.endsWith(QStringLiteral(".gz")));
            QVERIFY(entry.path.startsWith(QStringLiteral("docs/repetitive.bin.")));
            QCOMPARE(entry.originalSize, qint64(payload.size()));
            QVERIFY(entry.storedSize < entry.originalSize);  // compression actually helped
        }
        QCOMPARE(fileCount, 1);
    }
    Archive archive;
    QVERIFY2(archive.open(archivePath, false, &error), qPrintable(error));
    const QString output = temp.filePath(QStringLiteral("out"));
    QVERIFY2(archive.extract({}, output, &error), qPrintable(error));  // auto-decompress defaults on
    QFile restored(output + QStringLiteral("/docs/repetitive.bin"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), payload);
}

void ArchiveTest::compressedNoDecompressExtract()
{
    // With auto-decompress OFF, a compressed entry must surface as a valid .gz file
    // whose name carries the ".<hash>.gz" marker and whose bytes start with the gzip
    // magic (1f 8b) — i.e. it is a standard, hand-gunzip-able file.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString sourcePath = temp.filePath(QStringLiteral("note.txt"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("compress me compress me compress me");
    source.close();
    const QString archivePath = temp.filePath(QStringLiteral("rawgz.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({sourcePath}, {}, &error, {}, true), qPrintable(error));
    }
    Archive archive;
    QVERIFY2(archive.open(archivePath, false, &error), qPrintable(error));
    const QString output = temp.filePath(QStringLiteral("out"));
    QVERIFY2(archive.extract({}, output, &error, {}, false), qPrintable(error));  // no auto-decompress
    const auto entries = archive.entries(&error);
    QString storedKey;
    for (const ArchiveEntry &e : entries) if (!e.directory) storedKey = e.path;
    QFile gz(output + u'/' + storedKey);
    QVERIFY(gz.open(QIODevice::ReadOnly));
    const QByteArray gzBytes = gz.readAll();
    QCOMPARE(quint8(gzBytes.at(0)), 0x1f);
    QCOMPARE(quint8(gzBytes.at(1)), 0x8b);
}

void ArchiveTest::mixedCompressedAndRaw()
{
    // A raw entry and a compressed entry of the same logical name must coexist under
    // distinct keys, and a single logical-path selection must resolve the right one.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString rawSrc = temp.filePath(QStringLiteral("a.txt"));
    const QString cmpSrc = temp.filePath(QStringLiteral("b.txt"));
    QFile(rawSrc).open(QIODevice::WriteOnly);
    {
        QFile f(cmpSrc);
        f.open(QIODevice::WriteOnly);
        f.write(QByteArray(8192, 'Z'));
    }
    const QString archivePath = temp.filePath(QStringLiteral("mixed.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({rawSrc}, QStringLiteral("raw"), &error, {}, false), qPrintable(error));
        QVERIFY2(archive.addPaths({cmpSrc}, QStringLiteral("cmp"), &error, {}, true), qPrintable(error));
    }
    Archive archive;
    QVERIFY2(archive.open(archivePath, false, &error), qPrintable(error));
    const auto entries = archive.entries(&error);
    int files = 0, gzFiles = 0;
    for (const ArchiveEntry &e : entries) {
        if (e.directory) continue;
        ++files;
        if (e.path.endsWith(QStringLiteral(".gz"))) ++gzFiles;
    }
    QCOMPARE(files, 2);
    QCOMPARE(gzFiles, 1);
    // Selecting the logical compressed path extracts its decompressed form.
    const QString output = temp.filePath(QStringLiteral("out"));
    QVERIFY2(archive.extract({QStringLiteral("cmp/b.txt")}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/cmp/b.txt"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), QByteArray(8192, 'Z'));
}

void ArchiveTest::realGzipFileUntouched()
{
    // A genuine user .gz file stored uncompressed must NOT be auto-decompressed on
    // extract, even with auto-decompress on: its key lacks the validated hash marker.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    // The file's content is irrelevant to the test — only the key (".gz" suffix
    // but no validated hash marker) determines that it must pass through untouched.
    const QString realGzSrc = temp.filePath(QStringLiteral("data.gz"));
    const QByteArray gz = QByteArray::fromHex("1f8b0800000000000000") + QByteArray(16, 'x')
                          + QByteArray::fromHex("0000000000000000");
    {
        QFile f(realGzSrc);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(gz);
    }
    const QString archivePath = temp.filePath(QStringLiteral("realgz.lmdb"));
    QString error;
    {
        Archive archive;
        QVERIFY2(archive.open(archivePath, true, &error), qPrintable(error));
        QVERIFY2(archive.addPaths({realGzSrc}, {}, &error, {}, false), qPrintable(error));
    }
    Archive archive;
    QVERIFY2(archive.open(archivePath, false, &error), qPrintable(error));
    const QString output = temp.filePath(QStringLiteral("out"));
    QVERIFY2(archive.extract({}, output, &error), qPrintable(error));  // auto-decompress on
    // The file must come out under its original name (data.gz), unchanged.
    QFile restored(output + QStringLiteral("/data.gz"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), gz);
}

void ArchiveTest::lucioraElaCompatibility()
{
    // Simulates a LucioraEla defect archive: key = "Roi_0/Defect_000123.bmp",
    // value = raw BMP-like bytes with no wrapper. The Archiver must read and
    // extract these transparently, proving byte-level schema compatibility.
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const QString archivePath = temp.filePath(QStringLiteral("wafer_001.lmdb"));

    // Write a LucioraEla-style record with a bare LMDB handle.
    const QByteArray bmpPayload = QByteArray::fromHex("424d3c0000000000000036000000")
        + QByteArray(64 - 14, '\0');  // minimal BMP-ish header + padding
    {
        MDB_env *env = nullptr;
        MDB_txn *txn = nullptr;
        MDB_dbi dbi = 0;
        QCOMPARE(mdb_env_create(&env), MDB_SUCCESS);
        QCOMPARE(mdb_env_set_mapsize(env, 16 * 1024 * 1024), MDB_SUCCESS);
        const QByteArray native = QDir::toNativeSeparators(archivePath).toUtf8();
        QCOMPARE(mdb_env_open(env, native.constData(), MDB_NOSUBDIR, 0664), MDB_SUCCESS);
        QCOMPARE(mdb_txn_begin(env, nullptr, 0, &txn), MDB_SUCCESS);
        QCOMPARE(mdb_dbi_open(txn, nullptr, MDB_CREATE, &dbi), MDB_SUCCESS);
        const QByteArray key = QByteArrayLiteral("Roi_0/Defect_000123.bmp");
        MDB_val mdbKey{size_t(key.size()), const_cast<char *>(key.constData())};
        MDB_val mdbValue{size_t(bmpPayload.size()), const_cast<char *>(bmpPayload.constData())};
        QCOMPARE(mdb_put(txn, dbi, &mdbKey, &mdbValue, 0), MDB_SUCCESS);
        QCOMPARE(mdb_txn_commit(txn), MDB_SUCCESS);
        mdb_env_close(env);
    }

    // The Archiver must see exactly one file entry with the LucioraEla key.
    Archive archive;
    QString error;
    QVERIFY2(archive.open(archivePath, false, &error), qPrintable(error));
    const auto entries = archive.entries(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    int fileCount = 0;
    for (const ArchiveEntry &entry : entries) {
        if (entry.directory) continue;
        ++fileCount;
        QCOMPARE(entry.path, QStringLiteral("Roi_0/Defect_000123.bmp"));
        QCOMPARE(entry.originalSize, qint64(bmpPayload.size()));
    }
    QCOMPARE(fileCount, 1);

    // Extraction must reproduce the original bytes verbatim — no headers, no transforms.
    const QString output = temp.filePath(QStringLiteral("extracted"));
    QVERIFY2(archive.extract({}, output, &error), qPrintable(error));
    QFile restored(output + QStringLiteral("/Roi_0/Defect_000123.bmp"));
    QVERIFY(restored.open(QIODevice::ReadOnly));
    QCOMPARE(restored.readAll(), bmpPayload);

    // A file added by the Archiver must also be readable by a bare LMDB reader,
    // i.e. the round trip preserves the LucioraEla schema in both directions.
    const QString sourcePath = temp.filePath(QStringLiteral("extra.txt"));
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("defect note");
    source.close();
    QVERIFY2(archive.addPaths({sourcePath}, QStringLiteral("Roi_1"), &error), qPrintable(error));
    archive.close();
    QCOMPARE(readRawValue(archivePath, QStringLiteral("Roi_1/extra.txt")), QByteArray("defect note"));
}

QTEST_APPLESS_MAIN(ArchiveTest)
#include "archive_test.moc"
