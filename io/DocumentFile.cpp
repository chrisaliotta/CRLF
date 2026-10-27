#include "io/DocumentFile.h"

#include <QFile>
#include <QFileInfo>

DocumentFile::DocumentFile(const QString &path)
    : m_path(path)
{
}

QString DocumentFile::fileName() const
{
    return QFileInfo(m_path).fileName();
}

QString DocumentFile::load()
{
    return decode(loadBytes());
}

void DocumentFile::save(const QString &text) const
{
    saveBytes(encode(text));
}

QByteArray DocumentFile::loadBytes() const
{
    QFile file(m_path);
    (void)file.open(QIODevice::ReadOnly);
    return file.readAll();
}

void DocumentFile::saveBytes(const QByteArray &bytes) const
{
    QFile file(m_path);
    (void)file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(bytes);
}

QString DocumentFile::decode(const QByteArray &bytes)
{
    const auto detected = QStringConverter::encodingForData(bytes);
    m_encoding = detected.value_or(QStringConverter::Utf8);
    m_hasBom = detected.has_value()
               && (m_encoding != QStringConverter::Utf8
                   || bytes.startsWith("\xEF\xBB\xBF"));

    QStringDecoder decoder(m_encoding);
    QString text = decoder(bytes);

    m_lineEnding = text.contains(QLatin1String("\r\n")) ? QStringLiteral("CRLF")
                                                        : QStringLiteral("LF");
    text.remove(QLatin1Char('\r'));
    return text;
}

QByteArray DocumentFile::encode(const QString &text) const
{
    QString out = text;
    if (m_lineEnding == QLatin1String("CRLF"))
        out.replace(QLatin1Char('\n'), QLatin1String("\r\n"));

    QStringEncoder encoder(m_encoding,
                           m_hasBom ? QStringConverter::Flag::WriteBom
                                    : QStringConverter::Flag::Default);
    return encoder(out);
}

QString DocumentFile::encodingName() const
{
    QString name = QString::fromLatin1(QStringConverter::nameForEncoding(m_encoding));
    if (m_encoding == QStringConverter::Utf8 && m_hasBom)
        name += QLatin1String(" BOM");
    return name;
}
