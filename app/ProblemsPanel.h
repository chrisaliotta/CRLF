#pragma once

#include "editor/Diagnostic.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

// Directory, then file, then one row per span. See docs/diagnostics.md.
class ProblemsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProblemsPanel(QWidget *parent = nullptr);

    void setProjectDirectory(const QString &directory);
    // Replaces the rows for `key`. An empty list removes the file.
    // `reveal` asks the strip to open when the list is not empty.
    void setFile(const QString &key, const QString &path, const QString &title, quintptr editorId,
                 const QList<Diagnostic> &spans, bool reveal = true);
    void removeFile(const QString &key);
    // Keeps the spans and moves the file to the group its new path belongs in.
    void retarget(const QString &oldKey, const QString &newKey, const QString &path, const QString &title,
                  quintptr editorId);
    void clear();
    int spanCount() const;

signals:
    void diagnosticActivated(const QString &path, quintptr editorId, int line, int column);
    void spansChanged();
    // A setFile call stored one or more spans.
    void showRequested();

private:
    QTreeWidgetItem *directoryItem(const QString &directory);
    QTreeWidgetItem *fileItem(const QString &key) const;
    void removeItem(QTreeWidgetItem *file);
    QString directoryFor(const QString &path) const;
    QString fileNameFor(const QString &path, const QString &title) const;
    QList<Diagnostic> spansOf(QTreeWidgetItem *file) const;

    QTreeWidget *m_tree = nullptr;
    QString m_projectDirectory;
};
