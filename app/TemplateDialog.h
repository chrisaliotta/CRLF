#pragma once

#include "io/ProjectCommands.h"

#include <QDialog>
#include <QIcon>
#include <QList>

class FileIconTheme;
class QComboBox;
class QLineEdit;
class QListWidget;
class QWidget;

// Catalog of installed icon keys, and a form for new file, extension, and
// folder mappings stored in .crlf.
class TemplateDialog : public QDialog
{
    Q_OBJECT

public:
    TemplateDialog(const QList<FileIconTheme> &themes, const ProjectIcons &icons,
                   const QString &fallbackTheme, QWidget *parent = nullptr);

    ProjectIcons icons() const { return m_icons; }

private:
    void fillIconKeys();
    void addMapping();
    void removeMapping();
    void refreshMappings();

    struct CatalogIcon {
        QString theme;
        QString key;
        QString kind;
        QIcon icon;
    };

    QList<FileIconTheme> m_themes;
    QList<CatalogIcon> m_catalog;
    QList<CatalogIcon> m_glyphs;
    ProjectIcons m_icons;
    QString m_fallbackTheme;
    QComboBox *m_theme = nullptr;
    QComboBox *m_kind = nullptr;
    QLineEdit *m_expression = nullptr;
    QComboBox *m_iconTheme = nullptr;
    QListWidget *m_iconKey = nullptr;
    QListWidget *m_openKey = nullptr;
    QLineEdit *m_iconSearch = nullptr;
    QWidget *m_openRow = nullptr;
    QListWidget *m_mappings = nullptr;
};
