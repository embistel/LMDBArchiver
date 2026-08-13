#pragma once

#include "archive/archiveentry.h"

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <functional>

struct MDB_env;

enum class ProgressPhase { Collecting, Processing, Finalizing };

struct ProgressInfo {
    ProgressPhase phase = ProgressPhase::Processing;
    QString currentItem;
    qint64 bytesDone = 0;
    qint64 bytesTotal = 0;        // 0 == unknown (caller shows indeterminate)
    qint64 itemBytesDone = 0;
    qint64 itemBytesTotal = 0;
};

class Archive final {
public:
    using Progress = std::function<bool(const ProgressInfo &)>;

    Archive();
    ~Archive();
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    bool open(const QString &filePath, bool create, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_env != nullptr; }
    QString filePath() const { return m_filePath; }

    QList<ArchiveEntry> entries(QString *error = nullptr) const;
    bool clear(QString *error = nullptr);
    bool compact(QString *error = nullptr);
    bool verify(QString *error = nullptr, const Progress &progress = {}) const;
    bool addPaths(const QStringList &paths, const QString &destination = {},
                  QString *error = nullptr, const Progress &progress = {}, bool compress = false);
    bool removePaths(const QStringList &archivePaths, QString *error = nullptr);
    bool extract(const QStringList &archivePaths, const QString &destination,
                 QString *error = nullptr, const Progress &progress = {},
                 bool autoDecompress = true) const;

    // Compression key helpers (self-describing ".<hash>.gz" marker on the key).
    // isCompressed reports whether a stored key carries the marker; logicalPath
    // returns the clean display name (marker stripped for compressed entries).
    static bool isCompressed(const QString &storedKey);
    static QString logicalPath(const QString &storedKey);

private:
    bool ensureCapacity(quint64 incomingBytes, QString *error);
    static QByteArray keyFor(const QString &path);
    static QString cleanArchivePath(const QString &path);

    MDB_env *m_env = nullptr;
    QString m_filePath;
};
