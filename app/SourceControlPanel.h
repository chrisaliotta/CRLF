#pragma once

#include "app/SourceControl.h"

#include <QColor>
#include <QHash>
#include <QPoint>
#include <QSet>
#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Every tracked repository with its conflicts, staged changes, and working
// changes, under one commit box. The repository picked in the box above the
// message is the one Commit, Pull, Push, Fetch, and the branch button act on;
// selecting a row in the tree picks its repository. Shown left of the editor
// with the explorer.
class SourceControlPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SourceControlPanel(SourceControl *control, QWidget *parent = nullptr);

    void setToneColors(const QColor &added, const QColor &modified, const QColor &deleted, const QColor &conflicted);
    QString currentRoot() const;
    void setCurrentRoot(const QString &root);
    // Lists the branches, then opens the menu at `globalPos` once they arrive.
    void showBranchMenu(const QString &root, const QPoint &globalPos);
    // The commits that touched `path`. Opening one compares it with its parent.
    void showHistory(const QString &root, const QString &path);
    void focusMessage();

signals:
    void diffRequested(const GitDiffRequest &request);
    // An absolute path, for a conflicted file or Open File.
    void fileActivated(const QString &path);
    void closeRequested();

private:
    enum Group { Conflicts, Staged, Changes };

    void rebuild();
    void addRepository(const QString &root);
    void refreshRepositoryPicker();
    void refreshActions();
    void activate(QTreeWidgetItem *item);
    void showContextMenu(const QPoint &pos);
    void commit();
    void openBranchMenu(const QString &root, const QList<GitBranch> &branches);
    void openHistory(const QString &root, const QString &path, const QList<GitCommit> &commits);
    QToolButton *makeButton(const QString &text, const QString &toolTip);
    // The changes a row stands for: one file, or every file in a group.
    QList<GitChange> changesAt(QTreeWidgetItem *item) const;
    GitDiffRequest diffFor(const QString &root, Group group, const GitChange &change) const;

    SourceControl *m_control;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_empty = nullptr;
    QComboBox *m_repositories = nullptr;
    QPlainTextEdit *m_message = nullptr;
    QToolButton *m_commit = nullptr;
    QToolButton *m_amend = nullptr;
    QToolButton *m_branch = nullptr;
    QTreeWidget *m_tree = nullptr;
    // By GitTone.
    QColor m_colors[4];
    // Rows collapsed by the user, by "root" or "root|group", kept across rebuilds.
    QSet<QString> m_collapsed;
    QString m_branchMenuRoot;
    QPoint m_branchMenuPosition;
    QString m_historyRoot;
    QString m_historyPath;
};
