#pragma once

#include <QDialog>

class FileIconTheme;

// The folder-icon keys and ${command} names used while editing a .crlf file.
class TemplateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TemplateDialog(const FileIconTheme *icons, QWidget *parent = nullptr);
};
