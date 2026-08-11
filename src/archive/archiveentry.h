#pragma once

#include <QDateTime>
#include <QString>

struct ArchiveEntry {
    QString path;
    bool directory = false;
    qint64 originalSize = 0;
    qint64 storedSize = 0;
    QDateTime modified;
    quint32 permissions = 0;
};

