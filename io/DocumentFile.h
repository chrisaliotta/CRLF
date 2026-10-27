#pragma once

#include <QString>
#include <QStringConverter>

// Owns the on-disk identity of one document: path, encoding, and line-ending
// convention. Text handed to the rest of the application is always
// LF-normalized; save() converts back. The raw-byte API serves the hex
// editor, which must round-trip content exactly.
class DocumentFile
{
public:
    DocumentFile() = default;
    explicit DocumentFile(const QString &path);

    QString load();
    void save(const QString &text) const;

    QByteArray loadBytes() const;
    void saveBytes(const QByteArray &bytes) const;
    // Detects encoding and line endings from the bytes (updating this
    // file's state) and returns LF-normalized text.
    QString decode(const QByteArray &bytes);
    // The exact bytes save() would write for this text.
    QByteArray encode(const QString &text) const;

    QString path() const { return m_path; }
    void setPath(const QString &path) { m_path = path; }
    QString fileName() const;

    QString encodingName() const;
    QString lineEndingName() const { return m_lineEnding; }

private:
    QString m_path;
    QStringConverter::Encoding m_encoding = QStringConverter::Utf8;
    bool m_hasBom = false;
    QString m_lineEnding = QStringLiteral("CRLF");
};
