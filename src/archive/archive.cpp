#include "archive/archive.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <lmdb.h>
#include <miniz.h>
#include <algorithm>
#include <cerrno>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString lmdbError(int code)
{
    return QString::fromLocal8Bit(mdb_strerror(code));
}

// Splits a stored key into its parent-directory paths so the tree view can show
// virtual folders. E.g. "Roi_0/Sub/Defect_001.bmp" yields {"Roi_0", "Roi_0/Sub"}.
// The returned set is later converted into synthetic directory ArchiveEntry rows.
QSet<QString> ancestorDirectories(const QString &path)
{
    QSet<QString> result;
    const auto sections = path.split(u'/', Qt::SkipEmptyParts);
    if (sections.size() < 2) return result;
    QString prefix;
    for (int i = 0; i < sections.size() - 1; ++i) {
        if (i > 0) prefix += u'/';
        prefix += sections[i];
        result.insert(prefix);
    }
    return result;
}

// --- Optional gzip compression ------------------------------------------------
// The native schema keeps values as raw bytes. Compression is self-describing
// through the KEY: a compressed entry is stored under "<logicalPath>.<crc8>.gz"
// where <crc8> is the CRC-32 of the logical path's UTF-8 bytes (8 lowercase hex).
// The value is a standard RFC 1952 gzip stream, so a user can also gunzip a
// no-decompress extraction by hand. No metadata lives in the value itself.

// CRC-32 over a byte buffer, matching the gzip trailer and the key marker.
quint32 crc32Of(const char *data, qsizetype size)
{
    return quint32(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const mz_uint8 *>(data), size_t(size)));
}

// Returns the 8-char lowercase hex CRC-32 of the UTF-8 form of the path.
QString pathHashHex(const QString &path)
{
    const QByteArray utf8 = path.toUtf8();
    return QString::number(crc32Of(utf8.constData(), utf8.size()), 16).rightJustified(8, u'0');
}

// Builds the stored key for a compressed entry: "<logicalPath>.<crc8>.gz".
QByteArray compressedKey(const QString &logicalPath)
{
    return (logicalPath + u'.' + pathHashHex(logicalPath) + QStringLiteral(".gz")).toUtf8();
}

// If `key` is a compressed-entry key (matches "<name>.<crc8>.gz" with a valid
// CRC of <name>), writes the logical path to *logicalPath and returns true.
bool isCompressedKey(const QString &key, QString *logicalPath)
{
    constexpr qsizetype kSuffix = 12;  // ".<8hex>.gz"
    if (key.size() < kSuffix + 1 || !key.endsWith(QStringLiteral(".gz"), Qt::CaseSensitive)) return false;
    const int dot = key.size() - kSuffix;  // position of the '.' before the hash
    if (key.at(dot) != u'.') return false;
    const QString hashStr = key.mid(dot + 1, 8);
    for (const QChar c : hashStr) {
        const ushort v = c.unicode();
        const bool hex = (v >= u'0' && v <= u'9') || (v >= u'a' && v <= u'f');
        if (!hex) return false;
    }
    const QString candidate = key.left(dot);
    if (pathHashHex(candidate).compare(hashStr, Qt::CaseInsensitive) != 0) return false;
    if (logicalPath) *logicalPath = candidate;
    return true;
}

// Produces a standard gzip (RFC 1952) stream from raw bytes:
//   [10-byte header][raw deflate body][crc32 LE][isize LE]
QByteArray gzipCompress(const QByteArray &raw)
{
    size_t bodyLen = 0;
    void *body = tdefl_compress_mem_to_heap(raw.constData(), size_t(raw.size()), &bodyLen,
                                            TDEFL_DEFAULT_MAX_PROBES);
    if (!body) return {};
    const quint32 crc = crc32Of(raw.constData(), raw.size());
    QByteArray result;
    result.reserve(int(bodyLen) + 18);
    static const unsigned char kHeader[10] = {0x1f, 0x8b, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0xff};
    result.append(reinterpret_cast<const char *>(kHeader), 10);
    result.append(reinterpret_cast<const char *>(body), int(bodyLen));
    for (int shift = 0; shift < 32; shift += 8) result.append(char((crc >> shift) & 0xff));
    const quint32 isize = quint32(quint64(raw.size()) & 0xffffffffull);
    for (int shift = 0; shift < 32; shift += 8) result.append(char((isize >> shift) & 0xff));
    mz_free(body);
    return result;
}

// Parses a standard gzip stream and returns the original bytes. Accepts any
// well-formed RFC 1952 input (FLG optional fields handled), not just our output.
QByteArray gzipDecompress(const QByteArray &gz, QString *error)
{
    if (gz.size() < 18 || quint8(gz.at(0)) != 0x1f || quint8(gz.at(1)) != 0x8b) {
        if (error) *error = QStringLiteral("gzip 헤더가 올바르지 않습니다.");
        return {};
    }
    const quint8 flg = quint8(gz.at(3));
    int pos = 10;
    if (flg & 0x04) {  // FEXTRA
        if (pos + 2 > gz.size()) goto bad;
        const int xlen = quint8(gz.at(pos)) | (quint8(gz.at(pos + 1)) << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) {  // FNAME
        while (pos < gz.size() && gz.at(pos) != '\0') ++pos;
        ++pos;
    }
    if (flg & 0x10) {  // FCOMMENT
        while (pos < gz.size() && gz.at(pos) != '\0') ++pos;
        ++pos;
    }
    if (flg & 0x02) pos += 2;  // FHCRC
    if (pos + 8 > gz.size()) goto bad;
    {
        const int bodyLen = gz.size() - 8 - pos;
        size_t outLen = 0;
        void *out = tinfl_decompress_mem_to_heap(gz.constData() + pos, size_t(bodyLen), &outLen,
                                                 TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (!out) { if (error) *error = QStringLiteral("gzip 압축 해제에 실패했습니다."); return {}; }
        QByteArray result(reinterpret_cast<const char *>(out), int(outLen));
        mz_free(out);
        return result;
    }
bad:
    if (error) *error = QStringLiteral("gzip 데이터가 잘렸거나 손상되었습니다.");
    return {};
}

// Reads the ISIZE field (original size mod 2^32) from the gzip trailer without
// decompressing, so entries() can report the original size of a compressed value.
quint32 gzipOriginalSize(const QByteArray &gz)
{
    if (gz.size() < 4) return 0;
    const int base = gz.size() - 4;
    return quint32(quint8(gz.at(base))) | (quint32(quint8(gz.at(base + 1))) << 8)
         | (quint32(quint8(gz.at(base + 2))) << 16) | (quint32(quint8(gz.at(base + 3))) << 24);
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
    if (rc == MDB_SUCCESS) rc = mdb_env_set_maxdbs(m_env, 1);
    if (rc == MDB_SUCCESS) rc = mdb_env_set_maxreaders(m_env, 126);
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
    return cleanArchivePath(path).toUtf8();
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
    QSet<QString> directorySet;
    MDB_val key{}, value{};
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QString storedKey = QString::fromUtf8(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        QString logicalKey;
        const bool compressed = isCompressedKey(storedKey, &logicalKey);
        ArchiveEntry entry;
        entry.path = storedKey;  // keep the real stored key so extract/remove resolve correctly
        entry.directory = false;
        entry.storedSize = qint64(value.mv_size);
        if (compressed) {
            const QByteArray rawValue(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
            entry.originalSize = qint64(gzipOriginalSize(rawValue));
        } else {
            entry.originalSize = qint64(value.mv_size);
        }
        result.push_back(entry);
        // Virtual folders are derived from the logical path so a compressed entry
        // like "Roi_0/x.bmp.<hash>.gz" nests under "Roi_0", not a bogus hashed folder.
        const auto ancestors = ancestorDirectories(compressed ? logicalKey : storedKey);
        for (const QString &dir : ancestors) directorySet.insert(dir);
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_commit(txn);  // commit (== abort for read-only) frees the reader slot reliably on Windows
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = lmdbError(rc);
        return {};
    }
    // Emit synthetic directory rows so the tree view can show folder hierarchy.
    // Each directory sums the sizes of the entries nested beneath it for display.
    for (const QString &dir : directorySet) {
        ArchiveEntry entry;
        entry.path = dir;
        entry.directory = true;
        entry.originalSize = 0;
        entry.storedSize = 0;
        for (const ArchiveEntry &file : result) {
            if (!file.directory && (file.path == dir || file.path.startsWith(dir + u'/')))
                entry.originalSize += file.originalSize;
        }
        result.push_back(entry);
    }
    std::sort(result.begin(), result.end(), [](const ArchiveEntry &left, const ArchiveEntry &right) {
        return left.path < right.path;
    });
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
    MDB_dbi dbi = 0;
    // Tally total bytes via entries() so the progress bar has a real denominator.
    qint64 bytesTotal = 0;
    const auto all = entries(error);
    if (error && !error->isEmpty()) return false;
    for (const ArchiveEntry &entry : all)
        if (!entry.directory) bytesTotal += entry.originalSize;

    MDB_txn *txn = nullptr;
    MDB_cursor *cursor = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);

    ProgressInfo info;
    info.phase = ProgressPhase::Processing;
    info.bytesTotal = bytesTotal;
    qint64 bytesDone = 0;
    MDB_val key{}, value{};
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QString path = QString::fromUtf8(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        // The native schema stores raw bytes directly: a readable value is an intact entry.
        bytesDone += qint64(value.mv_size);
        if (progress) {
            info.currentItem = path;
            info.bytesDone = bytesDone;
            info.itemBytesDone = qint64(value.mv_size);
            info.itemBytesTotal = qint64(value.mv_size);
            if (!progress(info)) { rc = MDB_BAD_TXN; break; }
        }
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_commit(txn);  // commit (== abort for read-only) frees the reader slot reliably on Windows
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = QStringLiteral("아카이브 검사 실패: %1").arg(lmdbError(rc));
        return false;
    }
    return true;
}

bool Archive::addPaths(const QStringList &paths, const QString &destination, QString *error,
                       const Progress &progress, bool compress)
{
    if (error) error->clear();
    if (!m_env || paths.isEmpty()) return false;
    struct Pending { QString source; QString target; bool directory; };
    QList<Pending> pending;
    quint64 bytes = 0;
    ProgressInfo info;
    info.phase = ProgressPhase::Collecting;
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
                if (progress && (pending.size() % 64 == 0)) {
                    info.currentItem = root.fileName();
                    info.itemBytesDone = qint64(pending.size());
                    info.itemBytesTotal = 0;
                    if (!progress(info)) { if (error) *error = QStringLiteral("항목 추가가 취소되었습니다."); return false; }
                }
            }
        }
    }
    // Small files each consume at least one LMDB page plus B-tree overhead.
    if (!ensureCapacity(bytes + quint64(pending.size()) * 8 * 1024, error)) return false;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    const int maximumKeySize = mdb_env_get_maxkeysize(m_env);
    info = ProgressInfo{};
    info.phase = ProgressPhase::Processing;
    info.bytesTotal = qint64(bytes);
    qint64 bytesCommitted = 0;
    if (progress) {
        info.bytesDone = 0;
        if (!progress(info)) rc = MDB_BAD_TXN;
    }
    for (qsizetype i = 0; rc == MDB_SUCCESS && i < pending.size(); ++i) {
        // Directories are not stored: the native schema has no metadata records, so
        // empty folders cannot be preserved (matching the upstream LucioraEla model).
        if (pending[i].directory) {
            if (progress) {
                info.currentItem = pending[i].target;
                info.bytesDone = bytesCommitted;
                info.itemBytesDone = 0;
                info.itemBytesTotal = 0;
                if (!progress(info)) { rc = MDB_BAD_TXN; break; }
            }
            continue;
        }
        const QFileInfo infoFile(pending[i].source);
        const QString logicalPath = pending[i].target;
        const QByteArray keyBytes = compress ? compressedKey(logicalPath) : keyFor(logicalPath);
        if (keyBytes.isEmpty() || keyBytes.size() > maximumKeySize) {
            if (error) *error = QStringLiteral("아카이브 내부 경로가 너무 깁니다: %1").arg(logicalPath);
            rc = MDB_BAD_VALSIZE;
            break;
        }
        // Clear both the logical and compressed keys so re-adding in a different
        // mode (compressed <-> raw) leaves no stale orphan behind.
        const QByteArray logicalKeyBytes = keyFor(logicalPath);
        const QByteArray compressedKeyBytes = compressedKey(logicalPath);
        for (const QByteArray &clearKey : {logicalKeyBytes, compressedKeyBytes}) {
            MDB_val existingKey{size_t(clearKey.size()), const_cast<char *>(clearKey.constData())};
            const int delRc = mdb_del(txn, dbi, &existingKey, nullptr);
            if (delRc != MDB_SUCCESS && delRc != MDB_NOTFOUND) {
                if (error) *error = lmdbError(delRc);
                rc = delRc;
                break;
            }
        }
        if (rc != MDB_SUCCESS) break;

        QFile file(pending[i].source);
        if (!file.open(QIODevice::ReadOnly)) {
            if (error) *error = file.errorString();
            rc = EACCES;
            break;
        }
        const QByteArray contents = file.readAll();
        if (file.error() != QFileDevice::NoError) {
            if (error) *error = file.errorString();
            rc = EIO;
            break;
        }
        const QByteArray valueBytes = compress ? gzipCompress(contents) : contents;
        if (compress && valueBytes.isEmpty()) {
            if (error) *error = QStringLiteral("압축에 실패했습니다: %1").arg(logicalPath);
            rc = EIO;
            break;
        }
        MDB_val keyValue{size_t(keyBytes.size()), const_cast<char *>(keyBytes.constData())};
        MDB_val valueValue{size_t(valueBytes.size()), const_cast<char *>(valueBytes.constData())};
        rc = mdb_put(txn, dbi, &keyValue, &valueValue, 0);
        if (rc != MDB_SUCCESS) break;
        bytesCommitted += contents.size();
        if (progress) {
            info.currentItem = logicalPath;
            info.bytesDone = bytesCommitted;
            info.itemBytesDone = contents.size();
            info.itemBytesTotal = infoFile.size();
            if (!progress(info)) { rc = MDB_BAD_TXN; break; }
        }
    }
    if (rc == MDB_SUCCESS) {
        if (progress) {
            info = ProgressInfo{};
            info.phase = ProgressPhase::Finalizing;
            info.bytesDone = info.bytesTotal = qint64(bytes);
            if (!progress(info)) rc = MDB_BAD_TXN;
        }
        if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn);
    } else if (txn) {
        mdb_txn_abort(txn);
    }
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
        if (entry.directory) continue;  // Synthetic row — nothing to delete on disk.
        const QByteArray keyBytes = keyFor(entry.path);
        MDB_val key{size_t(keyBytes.size()), const_cast<char *>(keyBytes.constData())};
        rc = mdb_del(txn, dbi, &key, nullptr);
        if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) {
            if (error) *error = lmdbError(rc);
            break;
        }
    }
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    if (rc != MDB_SUCCESS && error) *error = QStringLiteral("항목 삭제 실패: %1").arg(lmdbError(rc));
    return rc == MDB_SUCCESS;
}

bool Archive::extract(const QStringList &archivePaths, const QString &destination, QString *error,
                      const Progress &progress, bool autoDecompress) const
{
    if (error) error->clear();
    if (!m_env) return false;
    const QString destinationRoot = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    QDir().mkpath(destinationRoot);
    // Pre-normalize selection once so both the tally pass and the extract pass agree.
    QList<QString> cleanSelection;
    cleanSelection.reserve(archivePaths.size());
    for (const QString &requested : archivePaths) cleanSelection << cleanArchivePath(requested);
    auto isSelected = [&cleanSelection](const QString &path) {
        if (cleanSelection.isEmpty()) return true;
        for (const QString &clean : cleanSelection)
            if (path == clean || path.startsWith(clean + u'/')) return true;
        return false;
    };
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif

    MDB_dbi dbi = 0;
    // Tally total bytes via entries() so the progress bar has a real denominator. Using
    // the public API (rather than a second read transaction) avoids reader-locktable
    // contention when extract() immediately follows another read operation.
    qint64 bytesTotal = 0;
    const auto all = entries(error);
    if (error && !error->isEmpty()) return false;
    for (const ArchiveEntry &entry : all) {
        if (entry.directory) continue;
        QString logicalKey;
        const bool compressed = isCompressedKey(entry.path, &logicalKey);
        const QString matchKey = compressed ? logicalKey : entry.path;
        if (!isSelected(matchKey)) continue;
        // Bytes actually written: decompressed size when auto-decompressing a gzip
        // entry, otherwise the stored (raw or gzip) size.
        if (compressed && autoDecompress) bytesTotal += entry.originalSize;
        else bytesTotal += entry.storedSize;
    }

    MDB_txn *txn = nullptr;
    MDB_cursor *cursor = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);

    ProgressInfo info;
    info.phase = ProgressPhase::Processing;
    info.bytesTotal = bytesTotal;
    qint64 bytesDone = 0;
    MDB_val key{}, value{};
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QString storedKey = QString::fromUtf8(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        QString logicalKey;
        const bool compressed = isCompressedKey(storedKey, &logicalKey);
        // Selection matches on the logical path so the user can pick a clean name
        // (e.g. "docs/note.txt") and still hit the compressed entry.
        const QString matchKey = compressed ? logicalKey : storedKey;
        if (!isSelected(matchKey)) continue;
        // Resolve the on-disk filename and bytes depending on the decompress mode.
        QString outName = storedKey;
        QByteArray fileBytes(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
        if (compressed && autoDecompress) {
            const QByteArray restored = gzipDecompress(fileBytes, error);
            if (error && !error->isEmpty()) { rc = MDB_CORRUPTED; break; }
            fileBytes = restored;
            outName = logicalKey;
        }
        const QString outputPath = QDir(destinationRoot).absoluteFilePath(outName);
        const QString normalized = QDir::cleanPath(outputPath);
        if (normalized.compare(destinationRoot, pathCase) != 0
            && !normalized.startsWith(destinationRoot + u'/', pathCase)) {
            if (error) *error = QStringLiteral("안전하지 않은 경로가 차단되었습니다: %1").arg(outName);
            rc = MDB_INVALID; break;
        }
        QDir().mkpath(QFileInfo(normalized).absolutePath());
        QSaveFile file(normalized);
        if (!file.open(QIODevice::WriteOnly)) {
            if (error) *error = file.errorString();
            rc = EACCES;
            break;
        }
        if (file.write(fileBytes) != fileBytes.size()) {
            if (error) *error = file.errorString();
            rc = EACCES;
            break;
        }
        bytesDone += fileBytes.size();
        if (progress) {
            info.currentItem = matchKey;
            info.bytesDone = bytesDone;
            info.itemBytesDone = fileBytes.size();
            info.itemBytesTotal = fileBytes.size();
            if (!progress(info)) { rc = MDB_BAD_TXN; break; }
        }
        if (!file.commit()) {
            if (error) *error = file.errorString();
            rc = EACCES;
            break;
        }
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_commit(txn);  // commit (== abort for read-only) frees the reader slot reliably on Windows
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = QStringLiteral("추출 실패: %1").arg(lmdbError(rc));
        return false;
    }
    if (progress) {
        info = ProgressInfo{};
        info.phase = ProgressPhase::Finalizing;
        info.bytesDone = info.bytesTotal = bytesTotal;
        progress(info);
    }
    return true;
}

bool Archive::isCompressed(const QString &storedKey)
{
    return isCompressedKey(storedKey, nullptr);
}

QString Archive::logicalPath(const QString &storedKey)
{
    QString logical;
    if (isCompressedKey(storedKey, &logical)) return logical;
    return storedKey;
}
