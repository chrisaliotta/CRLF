#include "io/WslPath.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>

namespace WslPath {

namespace {

const QString kWslExe = QStringLiteral("C:/Windows/System32/wsl.exe");

QByteArray run(const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.setProgram(kWslExe);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForFinished(timeoutMs))
        return {};
    return process.readAllStandardOutput();
}

bool looksLikeUtf16Le(const QByteArray &bytes)
{
    if (bytes.size() < 4)
        return false;
    int zeros = 0;
    int samples = 0;
    for (int i = 1; i < qMin(bytes.size(), 64); i += 2) {
        ++samples;
        zeros += bytes.at(i) == '\0';
    }
    return samples > 0 && zeros * 2 > samples;
}

// wsl.exe --list writes UTF-16LE when its stdout is a pipe.
QString decodeList(const QByteArray &bytes)
{
    if (bytes.startsWith("\xFF\xFE") || looksLikeUtf16Le(bytes)) {
        const auto *data = reinterpret_cast<const char16_t *>(bytes.constData());
        int count = bytes.size() / 2;
        if (count > 0 && data[0] == char16_t(0xFEFF)) {
            ++data;
            --count;
        }
        return QString::fromUtf16(data, count);
    }
    return QString::fromUtf8(bytes);
}

} // namespace

QStringList distros()
{
    const QString text = decodeList(run({QStringLiteral("--list"), QStringLiteral("--verbose")}, 8000));
    QStringList names;
    QString defaultName;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        const bool isDefault = line.startsWith(QLatin1Char('*'));
        if (isDefault)
            line = line.mid(1).trimmed();
        const QString name = line.split(QRegularExpression(QStringLiteral("\\s{2,}"))).first().trimmed();
        if (name.isEmpty() || name.compare(QLatin1String("NAME"), Qt::CaseInsensitive) == 0)
            continue;
        if (isDefault)
            defaultName = name;
        names.append(name);
    }
    if (!defaultName.isEmpty()) {
        names.removeAll(defaultName);
        names.prepend(defaultName);
    }
    return names;
}

QString home(const QString &distro)
{
    const QByteArray output = run({QStringLiteral("-d"), distro, QStringLiteral("--"),
                                   QStringLiteral("bash"), QStringLiteral("-lc"),
                                   QStringLiteral("printf %s \"$HOME\"")},
                                  20000);
    return QString::fromUtf8(output).trimmed();
}

QString toWindows(const QString &distro, const QString &linuxPath)
{
    QString expanded = linuxPath.trimmed();
    if (expanded.isEmpty() || expanded == QLatin1String("~"))
        expanded = home(distro);
    else if (expanded.startsWith(QLatin1String("~/")))
        expanded = home(distro) + expanded.mid(1);
    if (expanded.isEmpty())
        expanded = QStringLiteral("/");
    if (!expanded.startsWith(QLatin1Char('/')))
        expanded.prepend(QLatin1Char('/'));

    QString suffix = expanded.mid(1);
    suffix.replace(QLatin1Char('/'), QLatin1Char('\\'));
    QString windows = QStringLiteral("\\\\wsl.localhost\\") + distro;
    if (!suffix.isEmpty())
        windows += QLatin1Char('\\') + suffix;
    return windows;
}

LinuxPath toLinux(const QString &windowsPath)
{
    const QString clean = QDir::cleanPath(windowsPath);
    QString rest;
    const QStringList prefixes = {QStringLiteral("//wsl.localhost/"), QStringLiteral("//wsl$/")};
    for (const QString &prefix : prefixes) {
        if (clean.startsWith(prefix, Qt::CaseInsensitive)) {
            rest = clean.mid(prefix.size());
            break;
        }
    }
    if (rest.isEmpty())
        return {};

    const int slash = rest.indexOf(QLatin1Char('/'));
    LinuxPath location;
    location.distro = slash < 0 ? rest : rest.left(slash);
    location.path = slash < 0 ? QStringLiteral("/") : rest.mid(slash);
    if (location.distro.isEmpty())
        return {};
    return location;
}

bool isDistroRoot(const QString &windowsPath)
{
    const LinuxPath location = toLinux(windowsPath);
    return location.valid() && location.path == QLatin1String("/");
}

} // namespace WslPath
