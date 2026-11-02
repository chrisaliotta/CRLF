#pragma once

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QString>

// A VS Code file-icon theme. Definitions may point at an image (SVG or PNG)
// or at a glyph in a font shipped next to the JSON, which is how Seti is
// published. A file matches a file name, then a file extension, then the
// language id from icons/languages.json. Icons are cached per definition.
// One icon a theme associates with a key. `kind` is folder, file name,
// extension, language, or default.
struct IconOption
{
    QString key;
    QString kind;
    QIcon icon;
};

class FileIconTheme
{
public:
    static QList<FileIconTheme> loadInstalled();

    QString name;
    // expanded selects folderExpanded when the row is a directory that is open.
    QIcon iconFor(const QString &fileName, bool isDir, bool expanded = false) const;
    // Every key this theme draws: folders, file names, extensions, languages,
    // and the default file and folder icons.
    QList<IconOption> icons() const;

private:
    struct Definition {
        QString iconPath;
        QString fontFamily;
        QString character;
        QColor color;
        bool font = false;
    };

    static FileIconTheme fromJsonFile(const QString &path);
    QString definitionId(const QString &fileName, bool isDir, bool expanded) const;
    QIcon iconForId(const QString &id) const;
    QIcon render(const Definition &definition) const;
    void appendIcons(QList<IconOption> &rows, const QString &kind, const QHash<QString, QString> &map) const;

    QHash<QString, Definition> m_definitions;
    QHash<QString, QString> m_fileNames;
    QHash<QString, QString> m_fileExtensions;
    QHash<QString, QString> m_languageIds;
    QHash<QString, QString> m_folderNames;
    QHash<QString, QString> m_folderNamesExpanded;
    QString m_file;
    QString m_folder;
    QString m_folderExpanded;
    int m_fontPercent = 100;
    mutable QHash<QString, QIcon> m_cache;
};
