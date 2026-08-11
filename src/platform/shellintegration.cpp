#include "platform/shellintegration.h"

#ifdef Q_OS_WIN
#include <QDir>
#include <windows.h>
#include <shlobj.h>

namespace {
constexpr wchar_t kRoot[] = L"Software\\Classes";
constexpr wchar_t kStatePath[] = L"Software\\embistel\\LMDBArchiver";
constexpr auto kProgId = "LMDBArchiver.Archive";
constexpr auto kNoAssociation = "<none>";

bool setRawValue(const QString &path, const QString &name, const QString &value, QString *error)
{
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path.utf16()), 0, nullptr,
                                  0, KEY_WRITE, nullptr, &key, nullptr);
    if (result == ERROR_SUCCESS) {
        result = RegSetValueExW(key, name.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(name.utf16()), 0,
                               REG_SZ, reinterpret_cast<const BYTE *>(value.utf16()),
                               DWORD((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
    }
    if (result != ERROR_SUCCESS && error) *error = QStringLiteral("레지스트리 쓰기 오류: %1").arg(result);
    return result == ERROR_SUCCESS;
}

bool setValue(const QString &subkey, const QString &name, const QString &value, QString *error)
{
    return setRawValue(QString::fromWCharArray(kRoot) + u'\\' + subkey, name, value, error);
}

QString readRawValue(const QString &path, const QString &name, bool *exists = nullptr)
{
    if (exists) *exists = false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path.utf16()), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};
    DWORD type = 0;
    DWORD bytes = 0;
    LONG result = RegQueryValueExW(key, name.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(name.utf16()),
                                   nullptr, &type, nullptr, &bytes);
    QString value;
    if (result == ERROR_SUCCESS && type == REG_SZ && bytes >= sizeof(wchar_t)) {
        value.resize(qsizetype(bytes / sizeof(wchar_t)));
        result = RegQueryValueExW(key, name.isEmpty() ? nullptr : reinterpret_cast<LPCWSTR>(name.utf16()),
                                  nullptr, &type, reinterpret_cast<BYTE *>(value.data()), &bytes);
        if (!value.isEmpty() && value.back() == u'\0') value.chop(1);
    }
    RegCloseKey(key);
    if (result == ERROR_SUCCESS && exists) *exists = true;
    return result == ERROR_SUCCESS ? value : QString{};
}

bool removeRawTree(const QString &path, QString *error)
{
    const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(path.utf16()));
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND && error)
        *error = QStringLiteral("레지스트리 삭제 오류: %1").arg(result);
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool removeTree(const QString &subkey, QString *error)
{
    return removeRawTree(QString::fromWCharArray(kRoot) + u'\\' + subkey, error);
}
}

bool ShellIntegration::install(const QString &executable, QString *error)
{
    if (error) error->clear();
    const QString exe = QDir::toNativeSeparators(executable);
    const QString quoted = u'"' + exe + u'"';
    const QString associationPath = QString::fromWCharArray(kRoot) + QStringLiteral("\\.lmdb");
    bool associationExists = false;
    const QString previousAssociation = readRawValue(associationPath, {}, &associationExists);
    if (previousAssociation != QString::fromLatin1(kProgId)) {
        const QString backup = associationExists ? previousAssociation : QString::fromLatin1(kNoAssociation);
        if (!setRawValue(QString::fromWCharArray(kStatePath), QStringLiteral("PreviousLmdbAssociation"), backup, error))
            return false;
    }
    bool ok = true;
    ok &= setValue(QStringLiteral(".lmdb"), {}, QString::fromLatin1(kProgId), error);
    ok &= setValue(QStringLiteral("LMDBArchiver.Archive"), {}, QStringLiteral("LMDB Archive"), error);
    ok &= setValue(QStringLiteral("LMDBArchiver.Archive\\DefaultIcon"), {}, quoted + QStringLiteral(",0"), error);
    ok &= setValue(QStringLiteral("LMDBArchiver.Archive\\shell\\open\\command"), {}, quoted + QStringLiteral(" \"%1\""), error);
    ok &= setValue(QStringLiteral("LMDBArchiver.Archive\\shell\\extract"), {}, QStringLiteral("여기에 풀기"), error);
    ok &= setValue(QStringLiteral("LMDBArchiver.Archive\\shell\\extract\\command"), {}, quoted + QStringLiteral(" --extract-here \"%1\""), error);
    ok &= setValue(QStringLiteral("Directory\\shell\\LMDBArchiver.Pack"), {}, QStringLiteral("LMDB 아카이브로 묶기"), error);
    ok &= setValue(QStringLiteral("Directory\\shell\\LMDBArchiver.Pack"), QStringLiteral("Icon"), quoted, error);
    ok &= setValue(QStringLiteral("Directory\\shell\\LMDBArchiver.Pack\\command"), {}, quoted + QStringLiteral(" --create-from \"%1\""), error);
    ok &= setValue(QStringLiteral("*\\shell\\LMDBArchiver.Add"), {}, QStringLiteral("LMDB 아카이브에 추가..."), error);
    ok &= setValue(QStringLiteral("*\\shell\\LMDBArchiver.Add\\command"), {}, quoted + QStringLiteral(" --add-dialog \"%1\""), error);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool ShellIntegration::uninstall(QString *error)
{
    if (error) error->clear();
    const QString associationPath = QString::fromWCharArray(kRoot) + QStringLiteral("\\.lmdb");
    bool associationExists = false;
    const QString currentAssociation = readRawValue(associationPath, {}, &associationExists);
    bool backupExists = false;
    const QString backup = readRawValue(QString::fromWCharArray(kStatePath),
                                        QStringLiteral("PreviousLmdbAssociation"), &backupExists);
    bool ok = true;
    if (associationExists && currentAssociation == QString::fromLatin1(kProgId)) {
        if (backupExists && backup != QString::fromLatin1(kNoAssociation))
            ok &= setValue(QStringLiteral(".lmdb"), {}, backup, error);
        else
            ok &= removeTree(QStringLiteral(".lmdb"), error);
    }
    ok &= removeTree(QStringLiteral("LMDBArchiver.Archive"), error);
    ok &= removeTree(QStringLiteral("Directory\\shell\\LMDBArchiver.Pack"), error);
    ok &= removeTree(QStringLiteral("*\\shell\\LMDBArchiver.Add"), error);
    ok &= removeRawTree(QString::fromWCharArray(kStatePath), error);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool ShellIntegration::isInstalled()
{
    HKEY key = nullptr;
    const LONG result = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\LMDBArchiver.Archive", 0, KEY_READ, &key);
    if (key) RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
#else
bool ShellIntegration::install(const QString &, QString *error) { if (error) *error = QStringLiteral("Windows에서만 지원됩니다."); return false; }
bool ShellIntegration::uninstall(QString *error) { if (error) *error = QStringLiteral("Windows에서만 지원됩니다."); return false; }
bool ShellIntegration::isInstalled() { return false; }
#endif
