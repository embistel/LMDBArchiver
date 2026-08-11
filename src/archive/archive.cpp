#include "archive/archive.h"

#include <QDataStream>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUuid>
#include <lmdb.h>
#include <algorithm>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr quint32 kMagic = 0x4C4D4441; // LMDA
constexpr quint16 kLegacyFormatVersion = 1;
constexpr quint16 kFormatVersion = 2;
constexpr qsizetype kChunkSize = 4 * 1024 * 1024;
constexpr auto kEntryPrefix = "entry:";
constexpr auto kChunkPrefix = "chunk:";

struct EntryHeader {
    ArchiveEntry entry;
    quint16 version = 0;
    quint32 chunkCount = 0;
    bool legacyCompressed = false;
    QByteArray legacyPayload;
};

QString lmdbError(int code)
{
    return QString::fromLocal8Bit(mdb_strerror(code));
}

QByteArray encodeEntry(const ArchiveEntry &entry, quint32 chunkCount)
{
    QByteArray value;
    QDataStream stream(&value, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kMagic << kFormatVersion << entry.directory << entry.originalSize
           << entry.modified.toMSecsSinceEpoch() << entry.permissions << chunkCount;
    return value;
}

QByteArray encodeChunk(const QByteArray &contents)
{
    const QByteArray compressed = qCompress(contents, 7);
    const bool useCompressed = compressed.size() < contents.size();
    QByteArray value;
    QDataStream stream(&value, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << useCompressed << quint32(contents.size())
           << QCryptographicHash::hash(contents, QCryptographicHash::Sha256)
           << (useCompressed ? compressed : contents);
    return value;
}

bool decodeHeader(const QString &path, const QByteArray &value, EntryHeader *header, QString *error)
{
    QDataStream stream(value);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    qint64 modified = 0;
    stream >> magic >> header->version >> header->entry.directory >> header->entry.originalSize
           >> modified >> header->entry.permissions;
    if (header->version == kLegacyFormatVersion)
        stream >> header->legacyCompressed >> header->legacyPayload;
    else if (header->version == kFormatVersion)
        stream >> header->chunkCount;
    if (stream.status() != QDataStream::Ok || magic != kMagic
        || (header->version != kLegacyFormatVersion && header->version != kFormatVersion)) {
        if (error) *error = QStringLiteral("'%1' 항목의 데이터 형식이 손상되었습니다.").arg(path);
        return false;
    }
    header->entry.path = path;
    header->entry.storedSize = value.size();
    header->entry.modified = QDateTime::fromMSecsSinceEpoch(modified);
    return true;
}

bool decodeChunk(const QString &path, quint32 index, const QByteArray &value,
                 QByteArray *contents, QString *error)
{
    QDataStream stream(value);
    stream.setVersion(QDataStream::Qt_6_0);
    bool compressed = false;
    quint32 originalSize = 0;
    QByteArray expectedHash;
    QByteArray payload;
    stream >> compressed >> originalSize >> expectedHash >> payload;
    *contents = compressed ? qUncompress(payload) : payload;
    if (stream.status() != QDataStream::Ok || contents->size() != qsizetype(originalSize)
        || expectedHash.size() != 32
        || QCryptographicHash::hash(*contents, QCryptographicHash::Sha256) != expectedHash) {
        if (error) *error = QStringLiteral("'%1'의 %2번 데이터 청크가 손상되었습니다.").arg(path).arg(index);
        return false;
    }
    return true;
}

QByteArray chunkKeyFor(const QString &path, quint32 index)
{
    QByteArray key(kChunkPrefix);
    key += QDir::fromNativeSeparators(path).toUtf8();
    key += '\0';
    key += QByteArray::number(index, 16).rightJustified(8, '0');
    return key;
}

bool deleteStoredEntry(MDB_txn *txn, MDB_dbi dbi, const QString &path, QString *error)
{
    QByteArray entryKey = QByteArray(kEntryPrefix) + QDir::fromNativeSeparators(path).toUtf8();
    MDB_val key{size_t(entryKey.size()), entryKey.data()};
    MDB_val value{};
    int rc = mdb_get(txn, dbi, &key, &value);
    if (rc == MDB_NOTFOUND) return true;
    if (rc != MDB_SUCCESS) {
        if (error) *error = lmdbError(rc);
        return false;
    }
    const QByteArray rawValue(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
    EntryHeader header;
    if (!decodeHeader(path, rawValue, &header, error)) return false;
    if (header.version == kFormatVersion) {
        for (quint32 index = 0; index < header.chunkCount; ++index) {
            QByteArray chunkKey = chunkKeyFor(path, index);
            MDB_val chunkKeyValue{size_t(chunkKey.size()), chunkKey.data()};
            rc = mdb_del(txn, dbi, &chunkKeyValue, nullptr);
            if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
                if (error) *error = lmdbError(rc);
                return false;
            }
        }
    }
    rc = mdb_del(txn, dbi, &key, nullptr);
    if (rc != MDB_SUCCESS && error) *error = lmdbError(rc);
    return rc == MDB_SUCCESS;
}

QString nativePathForLmdb(const QString &path)
{
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}

bool replaceWithBackup(const QString &target, const QString &replacement,
                       const QString &backup, QString *error)
{
#ifdef Q_OS_WIN
    const QString nativeTarget = QDir::toNativeSeparators(target);
    const QString nativeReplacement = QDir::toNativeSeparators(replacement);
    const QString nativeBackup = QDir::toNativeSeparators(backup);
    if (ReplaceFileW(reinterpret_cast<LPCWSTR>(nativeTarget.utf16()),
                     reinterpret_cast<LPCWSTR>(nativeReplacement.utf16()),
                     reinterpret_cast<LPCWSTR>(nativeBackup.utf16()),
                     REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) return true;
    if (error) *error = QStringLiteral("아카이브 파일 교체 실패: Windows 오류 %1").arg(GetLastError());
    return false;
#else
    if (!QFile::rename(target, backup)) {
        if (error) *error = QStringLiteral("기존 아카이브를 백업할 수 없습니다.");
        return false;
    }
    if (QFile::rename(replacement, target)) return true;
    QFile::rename(backup, target);
    if (error) *error = QStringLiteral("정리된 아카이브로 교체할 수 없습니다.");
    return false;
#endif
}
}

Archive::Archive() = default;
Archive::~Archive() { close(); }

bool Archive::open(const QString &filePath, bool create, QString *error)
{
    if (error) error->clear();
    close();
    const QFileInfo info(filePath);
    if (!create && !info.isFile()) {
        if (error) *error = QStringLiteral("아카이브를 찾을 수 없습니다: %1").arg(filePath);
        return false;
    }
    QDir().mkpath(info.absolutePath());
    int rc = mdb_env_create(&m_env);
    if (rc == MDB_SUCCESS) rc = mdb_env_set_maxdbs(m_env, 2);
    const quint64 existingSize = info.exists() ? quint64(info.size()) : 0;
    quint64 initialMapSize = 64ull * 1024 * 1024;
    while (initialMapSize < existingSize) initialMapSize *= 2;
    if (rc == MDB_SUCCESS) rc = mdb_env_set_mapsize(m_env, size_t(initialMapSize));
    const QByteArray native = nativePathForLmdb(filePath).toUtf8();
    if (rc == MDB_SUCCESS) rc = mdb_env_open(m_env, native.constData(), MDB_NOSUBDIR, 0664);
    if (rc != MDB_SUCCESS) {
        if (error) *error = QStringLiteral("LMDB를 열 수 없습니다: %1").arg(lmdbError(rc));
        close();
        return false;
    }
    m_filePath = info.absoluteFilePath();
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    if (rc != MDB_SUCCESS) {
        if (error) *error = QStringLiteral("아카이브 초기화 실패: %1").arg(lmdbError(rc));
        close();
        return false;
    }
    return true;
}

void Archive::close()
{
    if (m_env) mdb_env_close(m_env);
    m_env = nullptr;
    m_filePath.clear();
}

QString Archive::cleanArchivePath(const QString &path)
{
    QString result = QDir::fromNativeSeparators(path).trimmed();
    while (result.startsWith(u'/')) result.remove(0, 1);
    result = QDir::cleanPath(result);
    if (result == QStringLiteral(".") || result.startsWith(QStringLiteral("../")) || result == QStringLiteral(".."))
        return {};
    return result;
}

QByteArray Archive::keyFor(const QString &path)
{
    return QByteArray(kEntryPrefix) + cleanArchivePath(path).toUtf8();
}

bool Archive::ensureCapacity(quint64 incomingBytes, QString *error)
{
    MDB_envinfo info{};
    MDB_stat stat{};
    int rc = mdb_env_info(m_env, &info);
    if (rc == MDB_SUCCESS) rc = mdb_env_stat(m_env, &stat);
    if (rc != MDB_SUCCESS) {
        if (error) *error = lmdbError(rc);
        return false;
    }
    const quint64 used = quint64(info.me_last_pgno + 1) * stat.ms_psize;
    const quint64 needed = used + incomingBytes + 32ull * 1024 * 1024;
    if (needed <= info.me_mapsize) return true;
    quint64 mapSize = info.me_mapsize;
    while (mapSize < needed) mapSize *= 2;
    rc = mdb_env_set_mapsize(m_env, size_t(mapSize));
    if (rc != MDB_SUCCESS && error) *error = QStringLiteral("아카이브 용량 확장 실패: %1").arg(lmdbError(rc));
    return rc == MDB_SUCCESS;
}

QList<ArchiveEntry> Archive::entries(QString *error) const
{
    if (error) error->clear();
    QList<ArchiveEntry> result;
    if (!m_env) return result;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_cursor *cursor = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);
    MDB_val key{}, value{};
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QByteArray rawKey(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        if (!rawKey.startsWith(kEntryPrefix)) continue;
        const QString path = QString::fromUtf8(rawKey.mid(qstrlen(kEntryPrefix)));
        const QByteArray rawValue(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
        EntryHeader header;
        if (!decodeHeader(path, rawValue, &header, error)) { rc = MDB_CORRUPTED; break; }
        if (header.version == kFormatVersion) {
            for (quint32 index = 0; index < header.chunkCount; ++index) {
                QByteArray chunkKey = chunkKeyFor(path, index);
                MDB_val chunkKeyValue{size_t(chunkKey.size()), chunkKey.data()};
                MDB_val chunkValue{};
                rc = mdb_get(txn, dbi, &chunkKeyValue, &chunkValue);
                if (rc != MDB_SUCCESS) {
                    if (error) *error = QStringLiteral("'%1'의 %2번 데이터 청크가 없습니다.").arg(path).arg(index);
                    break;
                }
                header.entry.storedSize += qint64(chunkValue.mv_size);
            }
            if (rc != MDB_SUCCESS) break;
        }
        result.push_back(header.entry);
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_abort(txn);
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS && error && error->isEmpty()) *error = lmdbError(rc);
    return result;
}

bool Archive::clear(QString *error)
{
    if (!m_env) return false;
    if (error) error->clear();
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_drop(txn, dbi, 0);
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    if (rc != MDB_SUCCESS && error) *error = QStringLiteral("아카이브 초기화 실패: %1").arg(lmdbError(rc));
    return rc == MDB_SUCCESS;
}

bool Archive::compact(QString *error)
{
    if (error) error->clear();
    if (!m_env) return false;
    const QString original = m_filePath;
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString temporary = original + QStringLiteral(".compact-") + token;
    const QString backup = original + QStringLiteral(".backup-") + token;
    const QByteArray nativeTemporary = nativePathForLmdb(temporary).toUtf8();
    int rc = mdb_env_copy2(m_env, nativeTemporary.constData(), MDB_CP_COMPACT);
    if (rc != MDB_SUCCESS) {
        QFile::remove(temporary);
        if (error) *error = QStringLiteral("아카이브 정리 복사 실패: %1").arg(lmdbError(rc));
        return false;
    }
    close();
    QString replaceError;
    if (!replaceWithBackup(original, temporary, backup, &replaceError)) {
        QFile::remove(temporary);
        QString ignored;
        open(original, false, &ignored);
        if (error) *error = replaceError;
        return false;
    }
    QString reopenError;
    if (!open(original, false, &reopenError)) {
#ifdef Q_OS_WIN
        QString rollbackError;
        replaceWithBackup(original, backup, temporary, &rollbackError);
#else
        QFile::remove(original);
        QFile::rename(backup, original);
#endif
        QString ignored;
        open(original, false, &ignored);
        QFile::remove(temporary);
        if (error) *error = QStringLiteral("정리 후 아카이브 재개방 실패: %1").arg(reopenError);
        return false;
    }
    QFile::remove(backup);
    QFile::remove(temporary);
    return true;
}

bool Archive::verify(QString *error, const Progress &progress) const
{
    if (error) error->clear();
    if (!m_env) return false;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_cursor *cursor = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);
    MDB_val key{}, value{};
    qsizetype entryIndex = 0;
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QByteArray rawKey(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        if (!rawKey.startsWith(kEntryPrefix)) continue;
        const QString path = QString::fromUtf8(rawKey.mid(qstrlen(kEntryPrefix)));
        if (progress && !progress(path, entryIndex++, -1)) { rc = MDB_BAD_TXN; break; }
        const QByteArray rawValue(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
        EntryHeader header;
        if (!decodeHeader(path, rawValue, &header, error)) { rc = MDB_CORRUPTED; break; }
        if (header.entry.directory) continue;
        qint64 verifiedSize = 0;
        if (header.version == kLegacyFormatVersion) {
            const QByteArray contents = header.legacyCompressed
                ? qUncompress(header.legacyPayload) : header.legacyPayload;
            verifiedSize = contents.size();
        } else {
            for (quint32 chunkIndex = 0; chunkIndex < header.chunkCount; ++chunkIndex) {
                if (progress && !progress(path, entryIndex, -1)) { rc = MDB_BAD_TXN; break; }
                QByteArray chunkKey = chunkKeyFor(path, chunkIndex);
                MDB_val chunkKeyValue{size_t(chunkKey.size()), chunkKey.data()};
                MDB_val chunkValue{};
                rc = mdb_get(txn, dbi, &chunkKeyValue, &chunkValue);
                if (rc != MDB_SUCCESS) {
                    if (error) *error = QStringLiteral("'%1'의 %2번 데이터 청크가 없습니다.").arg(path).arg(chunkIndex);
                    break;
                }
                const QByteArray rawChunk(static_cast<const char *>(chunkValue.mv_data), qsizetype(chunkValue.mv_size));
                QByteArray contents;
                if (!decodeChunk(path, chunkIndex, rawChunk, &contents, error)) { rc = MDB_CORRUPTED; break; }
                verifiedSize += contents.size();
            }
        }
        if (rc != MDB_SUCCESS) break;
        if (verifiedSize != header.entry.originalSize) {
            if (error) *error = QStringLiteral("'%1' 항목의 검증 크기가 일치하지 않습니다.").arg(path);
            rc = MDB_CORRUPTED;
            break;
        }
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_abort(txn);
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = QStringLiteral("아카이브 검사 실패: %1").arg(lmdbError(rc));
        return false;
    }
    return true;
}

bool Archive::addPaths(const QStringList &paths, const QString &destination, QString *error,
                       const Progress &progress)
{
    if (error) error->clear();
    if (!m_env || paths.isEmpty()) return false;
    struct Pending { QString source; QString target; bool directory; };
    QList<Pending> pending;
    quint64 bytes = 0;
    for (const QString &sourcePath : paths) {
        QFileInfo root(sourcePath);
        if (!root.exists()) { if (error) *error = QStringLiteral("경로가 없습니다: %1").arg(sourcePath); return false; }
        const QString base = cleanArchivePath(destination + u'/' + root.fileName());
        pending.push_back({root.absoluteFilePath(), base, root.isDir()});
        if (root.isFile()) bytes += quint64(root.size());
        if (root.isDir()) {
            QDirIterator it(root.absoluteFilePath(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QFileInfo child = it.fileInfo();
                const QString relative = QDir(root.absoluteFilePath()).relativeFilePath(child.absoluteFilePath());
                pending.push_back({child.absoluteFilePath(), cleanArchivePath(base + u'/' + relative), child.isDir()});
                if (child.isFile()) bytes += quint64(child.size());
            }
        }
    }
    // Small files and directories each consume at least one LMDB page plus B-tree overhead.
    if (!ensureCapacity(bytes + quint64(pending.size()) * 8 * 1024, error)) return false;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    const int maximumKeySize = mdb_env_get_maxkeysize(m_env);
    for (qsizetype i = 0; rc == MDB_SUCCESS && i < pending.size(); ++i) {
        if (progress && !progress(pending[i].target, i, pending.size())) { rc = MDB_BAD_TXN; break; }
        const QFileInfo info(pending[i].source);
        ArchiveEntry entry{pending[i].target, pending[i].directory, info.size(), 0,
                           info.lastModified(), quint32(info.permissions())};
        QByteArray keyBytes = keyFor(entry.path);
        if (keyBytes.size() > maximumKeySize || (!entry.directory && chunkKeyFor(entry.path, 0).size() > maximumKeySize)) {
            if (error) *error = QStringLiteral("아카이브 내부 경로가 너무 깁니다: %1").arg(entry.path);
            rc = MDB_BAD_VALSIZE;
            break;
        }
        if (!deleteStoredEntry(txn, dbi, entry.path, error)) { rc = MDB_BAD_TXN; break; }

        quint32 chunkCount = 0;
        qint64 bytesRead = 0;
        if (!entry.directory) {
            QFile file(pending[i].source);
            if (!file.open(QIODevice::ReadOnly)) {
                if (error) *error = file.errorString();
                rc = EACCES;
                break;
            }
            while (!file.atEnd()) {
                if (progress && !progress(entry.path, i, pending.size())) { rc = MDB_BAD_TXN; break; }
                const QByteArray contents = file.read(kChunkSize);
                if (contents.isEmpty() && file.error() != QFileDevice::NoError) {
                    if (error) *error = file.errorString();
                    rc = EIO;
                    break;
                }
                if (contents.isEmpty()) break;
                QByteArray chunkKey = chunkKeyFor(entry.path, chunkCount);
                QByteArray chunkValueBytes = encodeChunk(contents);
                MDB_val chunkKeyValue{size_t(chunkKey.size()), chunkKey.data()};
                MDB_val chunkValue{size_t(chunkValueBytes.size()), chunkValueBytes.data()};
                rc = mdb_put(txn, dbi, &chunkKeyValue, &chunkValue, 0);
                if (rc != MDB_SUCCESS) break;
                bytesRead += contents.size();
                ++chunkCount;
            }
            if (rc != MDB_SUCCESS) break;
            if (bytesRead != info.size()) {
                if (error) *error = QStringLiteral("보관 중 파일 크기가 변경되었습니다: %1").arg(pending[i].source);
                rc = MDB_BAD_VALSIZE;
                break;
            }
        }
        QByteArray valueBytes = encodeEntry(entry, chunkCount);
        MDB_val key{size_t(keyBytes.size()), keyBytes.data()};
        MDB_val value{size_t(valueBytes.size()), valueBytes.data()};
        rc = mdb_put(txn, dbi, &key, &value, 0);
    }
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    if (rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = QStringLiteral("항목 추가 실패: %1").arg(lmdbError(rc));
        return false;
    }
    return true;
}

bool Archive::removePaths(const QStringList &archivePaths, QString *error)
{
    if (error) error->clear();
    if (!m_env) return false;
    const auto all = entries(error);
    if (error && !error->isEmpty()) return false;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    for (const ArchiveEntry &entry : all) {
        bool remove = false;
        for (const QString &requested : archivePaths) {
            const QString clean = cleanArchivePath(requested);
            if (entry.path == clean || entry.path.startsWith(clean + u'/')) { remove = true; break; }
        }
        if (!remove) continue;
        if (!deleteStoredEntry(txn, dbi, entry.path, error)) { rc = MDB_BAD_TXN; break; }
    }
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    if (rc != MDB_SUCCESS && error) *error = QStringLiteral("항목 삭제 실패: %1").arg(lmdbError(rc));
    return rc == MDB_SUCCESS;
}

bool Archive::extract(const QStringList &archivePaths, const QString &destination, QString *error,
                      const Progress &progress) const
{
    if (error) error->clear();
    if (!m_env) return false;
    const QString destinationRoot = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    QDir().mkpath(destinationRoot);
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_cursor *cursor = nullptr;
    QList<QPair<QString, ArchiveEntry>> extractedDirectories;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);
    MDB_val key{}, value{};
    qsizetype index = 0;
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QByteArray rawKey(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        if (!rawKey.startsWith(kEntryPrefix)) continue;
        const QString path = QString::fromUtf8(rawKey.mid(qstrlen(kEntryPrefix)));
        bool selected = archivePaths.isEmpty();
        for (const QString &requested : archivePaths) {
            const QString clean = cleanArchivePath(requested);
            if (path == clean || path.startsWith(clean + u'/')) { selected = true; break; }
        }
        if (!selected) continue;
        if (progress && !progress(path, index++, -1)) { rc = MDB_BAD_TXN; break; }
        const QString outputPath = QDir(destinationRoot).absoluteFilePath(path);
        const QString normalized = QDir::cleanPath(outputPath);
#ifdef Q_OS_WIN
        const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
        const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
        if (normalized.compare(destinationRoot, pathCase) != 0
            && !normalized.startsWith(destinationRoot + u'/', pathCase)) {
            if (error) *error = QStringLiteral("안전하지 않은 경로가 차단되었습니다: %1").arg(path);
            rc = MDB_INVALID; break;
        }
        const QByteArray rawValue(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
        EntryHeader header;
        if (!decodeHeader(path, rawValue, &header, error)) { rc = MDB_CORRUPTED; break; }
        if (header.entry.directory) {
            if (!QDir().mkpath(normalized)) { rc = EACCES; break; }
            extractedDirectories.push_back({normalized, header.entry});
        } else {
            QDir().mkpath(QFileInfo(normalized).absolutePath());
            QSaveFile file(normalized);
            if (!file.open(QIODevice::WriteOnly)) {
                if (error) *error = file.errorString();
                rc = EACCES;
                break;
            }
            qint64 restoredSize = 0;
            if (header.version == kLegacyFormatVersion) {
                const QByteArray contents = header.legacyCompressed
                    ? qUncompress(header.legacyPayload) : header.legacyPayload;
                if (contents.size() != header.entry.originalSize) {
                    if (error) *error = QStringLiteral("'%1' 항목의 압축 데이터를 복원할 수 없습니다.").arg(path);
                    rc = MDB_CORRUPTED;
                } else if (file.write(contents) != contents.size()) {
                    if (error) *error = file.errorString();
                    rc = EACCES;
                } else {
                    restoredSize = contents.size();
                }
            } else {
                for (quint32 chunkIndex = 0; rc == MDB_SUCCESS && chunkIndex < header.chunkCount; ++chunkIndex) {
                    if (progress && !progress(path, index, -1)) { rc = MDB_BAD_TXN; break; }
                    QByteArray chunkKey = chunkKeyFor(path, chunkIndex);
                    MDB_val chunkKeyValue{size_t(chunkKey.size()), chunkKey.data()};
                    MDB_val chunkValue{};
                    rc = mdb_get(txn, dbi, &chunkKeyValue, &chunkValue);
                    if (rc != MDB_SUCCESS) {
                        if (error) *error = QStringLiteral("'%1'의 %2번 데이터 청크가 없습니다.").arg(path).arg(chunkIndex);
                        break;
                    }
                    const QByteArray rawChunk(static_cast<const char *>(chunkValue.mv_data), qsizetype(chunkValue.mv_size));
                    QByteArray contents;
                    if (!decodeChunk(path, chunkIndex, rawChunk, &contents, error)) { rc = MDB_CORRUPTED; break; }
                    if (file.write(contents) != contents.size()) {
                        if (error) *error = file.errorString();
                        rc = EACCES;
                        break;
                    }
                    restoredSize += contents.size();
                }
            }
            if (rc == MDB_SUCCESS && restoredSize != header.entry.originalSize) {
                if (error) *error = QStringLiteral("'%1' 항목의 복원 크기가 일치하지 않습니다.").arg(path);
                rc = MDB_CORRUPTED;
            }
            if (rc != MDB_SUCCESS) break;
            if (!file.commit()) {
                if (error) *error = file.errorString();
                rc = EACCES;
                break;
            }
            QFile fileForTime(normalized);
            if (fileForTime.open(QIODevice::ReadWrite)) fileForTime.setFileTime(header.entry.modified, QFileDevice::FileModificationTime);
            QFile::setPermissions(normalized, QFileDevice::Permissions(header.entry.permissions));
        }
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_abort(txn);
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = QStringLiteral("추출 실패: %1").arg(lmdbError(rc));
        return false;
    }
    std::sort(extractedDirectories.begin(), extractedDirectories.end(),
              [](const auto &left, const auto &right) { return left.first.size() > right.first.size(); });
    for (const auto &directory : extractedDirectories) {
        QFile directoryForTime(directory.first);
        if (directoryForTime.open(QIODevice::ReadOnly))
            directoryForTime.setFileTime(directory.second.modified, QFileDevice::FileModificationTime);
        QFile::setPermissions(directory.first, QFileDevice::Permissions(directory.second.permissions));
    }
    return true;
}
