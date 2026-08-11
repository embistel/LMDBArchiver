#pragma once

#include <QString>

namespace ShellIntegration {
bool install(const QString &executable, QString *error = nullptr);
bool uninstall(QString *error = nullptr);
bool isInstalled();
bool isMachineInstalled();
}
