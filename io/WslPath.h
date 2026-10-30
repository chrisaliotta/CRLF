#pragma once

#include <QString>
#include <QStringList>

// Windows UNC paths stay inside the app. A Linux path exists only at the
// folder picker and when a command is handed to wsl.exe.
namespace WslPath {

struct LinuxPath
{
    QString distro;
    QString path;

    bool valid() const { return !distro.isEmpty(); }
};

// Installed distro names. The default distro is first.
QStringList distros();

// Absolute Linux home for `distro`, or empty when WSL cannot answer.
QString home(const QString &distro);

// `~` and `~/...` expand to that distro's home. The result is a
// \\wsl.localhost path QFile can open.
QString toWindows(const QString &distro, const QString &linuxPath);

// Empty when `windowsPath` is not under \\wsl.localhost or \\wsl$.
// The distro root's Linux path is "/".
LinuxPath toLinux(const QString &windowsPath);

bool isDistroRoot(const QString &windowsPath);

} // namespace WslPath
