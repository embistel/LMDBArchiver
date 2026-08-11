#pragma once

#include "archive/archiveentry.h"

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <functional>

struct MDB_env;

class Archive final {
public:
    using Progress = std::function<bool(const QString &, qsizetype, qsizetype)>;

    Archive();
    ~Archive();
    Archive(const Archive &) = delete;
    Archive &operator=(const Archive &) = delete;

    bool open(const QString &filePath, bool create, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_env != nullptr; }
    QString filePath() const { return m_filePath; }

    QList<ArchiveEntry> entries(QString *error = nullptr) const;
    bool addPaths(const QStringList &paths, const QString &destination = {},
                  QString *error = nullptr, const Progress &progress = {});
    bool removePaths(const QStringList &archivePaths, QString *error = nullptr);
    bool extract(const QStringList &archivePaths, const QString &destination,
                 QString *error = nullptr, const Progress &progress = {}) const;

private:
    bool ensureCapacity(quint64 incomingBytes, QString *error);
    static QByteArray keyFor(const QString &path);
    static QString cleanArchivePath(const QString &path);

    MDB_env *m_env = nullptr;
    QString m_filePath;
};

