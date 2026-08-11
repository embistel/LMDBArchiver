#include "archive/archive.h"

#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <lmdb.h>
#include <cerrno>

namespace {
constexpr quint32 kMagic = 0x4C4D4441; // LMDA
constexpr quint16 kFormatVersion = 1;
constexpr auto kPrefix = "entry:";

QString lmdbError(int code)
{
    return QString::fromLocal8Bit(mdb_strerror(code));
}

QByteArray encodeEntry(const ArchiveEntry &entry, const QByteArray &contents)
{
    const QByteArray compressed = entry.directory ? QByteArray{} : qCompress(contents, 7);
    const bool useCompressed = !entry.directory && compressed.size() < contents.size();
    QByteArray value;
    QDataStream stream(&value, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << kMagic << kFormatVersion << entry.directory << entry.originalSize
           << entry.modified.toMSecsSinceEpoch() << entry.permissions << useCompressed
           << (useCompressed ? compressed : contents);
    return value;
}

bool decodeEntry(const QString &path, const QByteArray &value, ArchiveEntry *entry,
                 QByteArray *contents, QString *error)
{
    QDataStream stream(value);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 magic = 0;
    quint16 version = 0;
    bool compressed = false;
    QByteArray payload;
    qint64 modified = 0;
    stream >> magic >> version >> entry->directory >> entry->originalSize >> modified
           >> entry->permissions >> compressed >> payload;
    if (stream.status() != QDataStream::Ok || magic != kMagic || version != kFormatVersion) {
        if (error) *error = QStringLiteral("'%1' 항목의 데이터 형식이 손상되었습니다.").arg(path);
        return false;
    }
    entry->path = path;
    entry->storedSize = value.size();
    entry->modified = QDateTime::fromMSecsSinceEpoch(modified);
    if (contents) {
        *contents = compressed ? qUncompress(payload) : payload;
        if (!entry->directory && contents->size() != entry->originalSize) {
            if (error) *error = QStringLiteral("'%1' 항목의 압축 데이터를 복원할 수 없습니다.").arg(path);
            return false;
        }
    }
    return true;
}

QString nativePathForLmdb(const QString &path)
{
    return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
}
}

Archive::Archive() = default;
Archive::~Archive() { close(); }

bool Archive::open(const QString &filePath, bool create, QString *error)
{
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
    while (initialMapSize < existingSize + 32ull * 1024 * 1024) initialMapSize *= 2;
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
    return QByteArray(kPrefix) + cleanArchivePath(path).toUtf8();
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
        if (!rawKey.startsWith(kPrefix)) continue;
        const QString path = QString::fromUtf8(rawKey.mid(qstrlen(kPrefix)));
        const QByteArray rawValue(static_cast<const char *>(value.mv_data), qsizetype(value.mv_size));
        ArchiveEntry entry;
        if (!decodeEntry(path, rawValue, &entry, nullptr, error)) { rc = MDB_CORRUPTED; break; }
        result.push_back(entry);
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_abort(txn);
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS && error && error->isEmpty()) *error = lmdbError(rc);
    return result;
}

bool Archive::addPaths(const QStringList &paths, const QString &destination, QString *error,
                       const Progress &progress)
{
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
    if (!ensureCapacity(bytes + quint64(pending.size()) * 1024, error)) return false;
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    int rc = mdb_txn_begin(m_env, nullptr, 0, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    for (qsizetype i = 0; rc == MDB_SUCCESS && i < pending.size(); ++i) {
        if (progress && !progress(pending[i].target, i, pending.size())) { rc = MDB_BAD_TXN; break; }
        const QFileInfo info(pending[i].source);
        QByteArray contents;
        if (!pending[i].directory) {
            QFile file(pending[i].source);
            if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); rc = MDB_BAD_VALSIZE; break; }
            contents = file.readAll();
            if (contents.size() != info.size()) { if (error) *error = file.errorString(); rc = MDB_BAD_VALSIZE; break; }
        }
        ArchiveEntry entry{pending[i].target, pending[i].directory, info.size(), 0,
                           info.lastModified(), quint32(info.permissions())};
        QByteArray keyBytes = keyFor(entry.path);
        QByteArray valueBytes = encodeEntry(entry, contents);
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
        QByteArray keyBytes = keyFor(entry.path);
        MDB_val key{size_t(keyBytes.size()), keyBytes.data()};
        rc = mdb_del(txn, dbi, &key, nullptr);
        if (rc != MDB_SUCCESS && rc != MDB_NOTFOUND) break;
        rc = MDB_SUCCESS;
    }
    if (rc == MDB_SUCCESS) rc = mdb_txn_commit(txn); else if (txn) mdb_txn_abort(txn);
    if (rc != MDB_SUCCESS && error) *error = QStringLiteral("항목 삭제 실패: %1").arg(lmdbError(rc));
    return rc == MDB_SUCCESS;
}

bool Archive::extract(const QStringList &archivePaths, const QString &destination, QString *error,
                      const Progress &progress) const
{
    if (!m_env) return false;
    const QString destinationRoot = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    QDir().mkpath(destinationRoot);
    MDB_txn *txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_cursor *cursor = nullptr;
    int rc = mdb_txn_begin(m_env, nullptr, MDB_RDONLY, &txn);
    if (rc == MDB_SUCCESS) rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc == MDB_SUCCESS) rc = mdb_cursor_open(txn, dbi, &cursor);
    MDB_val key{}, value{};
    qsizetype index = 0;
    while (rc == MDB_SUCCESS && (rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT)) == MDB_SUCCESS) {
        const QByteArray rawKey(static_cast<const char *>(key.mv_data), qsizetype(key.mv_size));
        if (!rawKey.startsWith(kPrefix)) continue;
        const QString path = QString::fromUtf8(rawKey.mid(qstrlen(kPrefix)));
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
        ArchiveEntry entry;
        QByteArray contents;
        if (!decodeEntry(path, rawValue, &entry, &contents, error)) { rc = MDB_CORRUPTED; break; }
        if (entry.directory) {
            if (!QDir().mkpath(normalized)) { rc = EACCES; break; }
        } else {
            QDir().mkpath(QFileInfo(normalized).absolutePath());
            QSaveFile file(normalized);
            if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
                if (error) *error = file.errorString(); rc = EACCES; break;
            }
            QFile::setPermissions(normalized, QFileDevice::Permissions(entry.permissions));
            QFile fileForTime(normalized);
            if (fileForTime.open(QIODevice::ReadWrite)) fileForTime.setFileTime(entry.modified, QFileDevice::FileModificationTime);
        }
    }
    if (cursor) mdb_cursor_close(cursor);
    if (txn) mdb_txn_abort(txn);
    if (rc != MDB_NOTFOUND && rc != MDB_SUCCESS) {
        if (error && error->isEmpty()) *error = QStringLiteral("추출 실패: %1").arg(lmdbError(rc));
        return false;
    }
    return true;
}
